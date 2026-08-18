#!/usr/bin/env python3
"""
m1rpc.py — minimal M1 (T-1000) binary-RPC CLI client over USB CDC.

The M1's USB is in RPC/VCP mode, so the text CLI is not directly on the serial
port; CLI commands ride the binary RPC as CLI_EXEC (0x60) frames and the output
comes back as CLI_RESP (0x61). This tool drives that path, mainly to exercise
the `espnow` command family (M1 Link over ESP-NOW remote-trigger PoC).

Wire format (mirrors m1_csrc/m1_rpc.c exactly):
    [0xAA][CMD][SEQ][LEN_LO][LEN_HI][PAYLOAD...][CRC_LO][CRC_HI]
    CRC-16, init 0xFFFF, MSB-first, table-driven, over CMD..end-of-payload
    (the 0xAA sync byte is NOT included). The CRC table is the firmware's own
    non-standard table (46 entries in 0x40-0x7F differ from CRC-16/CCITT), so it
    is reproduced verbatim below — a stock CCITT table will fail on payloads.

Note: CLI_EXEC copies into a 64-byte buffer, so the command string must be
<= 63 chars. And a BadUSB trigger makes the RECEIVING unit re-enumerate as USB
HID, dropping its own RPC link — see the test recipe in the repo docs.

Usage:
    python3 tools/m1rpc.py --port /dev/cu.usbmodemXXXX "espnow info"
    python3 tools/m1rpc.py --port /dev/cu.usbmodemXXXX --timeout 65 "espnow listen 60"
    python3 tools/m1rpc.py --list      # list candidate serial ports
"""

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial required: pip install pyserial")

SYNC = 0xAA
CMD_PING = 0x01
CMD_PONG = 0x02
CMD_SCREEN_FRAME = 0x12
CMD_SCREEN_CAPTURE = 0x13
CMD_BUTTON_CLICK = 0x22
CMD_CLI_EXEC = 0x60
CMD_CLI_RESP = 0x61
CMD_NACK = 0x07

SCREEN_W, SCREEN_H = 128, 64
BUTTONS = {"ok": 0, "up": 1, "left": 2, "right": 3, "down": 4, "back": 5}

# Firmware's exact CRC-16 table (m1_csrc/m1_rpc.c s_crc16_table[256]).
CRC16_TABLE = [
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x4864, 0x5845, 0x6826, 0x7807, 0x08E0, 0x18C1, 0x28A2, 0x38C3,
    0xC92C, 0xD90D, 0xE96E, 0xF94F, 0x89A8, 0x9989, 0xA9EA, 0xB9CB,
    0x5A15, 0x4A34, 0x7A57, 0x6A76, 0x1A91, 0x0AB0, 0x3AD3, 0x2AF2,
    0xDB1D, 0xCB3C, 0xFB5F, 0xEB7E, 0x9B99, 0x8BB8, 0xBBDB, 0xABFA,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBBBA,
    0x4A55, 0x5A74, 0x6A17, 0x7A36, 0x0AD1, 0x1AF0, 0x2A93, 0x3AB2,
    0xFD0E, 0xED2F, 0xDD4C, 0xCD6D, 0xBD8A, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
]


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        idx = ((crc >> 8) ^ b) & 0xFF
        crc = ((crc << 8) ^ CRC16_TABLE[idx]) & 0xFFFF
    return crc


def build_frame(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    body = bytes([cmd, seq, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF]) + payload
    crc = crc16(body)  # over CMD..end-of-payload, excluding the sync byte
    return bytes([SYNC]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def read_frame(ser, timeout: float):
    """Read one RPC frame. Returns (cmd, seq, payload) or None on timeout."""
    deadline = time.time() + timeout
    # sync
    while time.time() < deadline:
        b = ser.read(1)
        if b == bytes([SYNC]):
            break
    else:
        return None
    hdr = ser.read(4)
    if len(hdr) < 4:
        return None
    cmd, seq, lo, hi = hdr[0], hdr[1], hdr[2], hdr[3]
    length = lo | (hi << 8)
    payload = ser.read(length)
    crc_bytes = ser.read(2)
    if len(payload) < length or len(crc_bytes) < 2:
        return None
    got = crc_bytes[0] | (crc_bytes[1] << 8)
    want = crc16(hdr + payload)
    if got != want:
        sys.stderr.write(f"[warn] CRC mismatch cmd=0x{cmd:02X} got=0x{got:04X} want=0x{want:04X}\n")
    return cmd, seq, payload


def resync(ser):
    """PING/PONG handshake — the firmware parser is stateful; align to a frame
    boundary before sending payload frames."""
    for _ in range(5):
        ser.reset_input_buffer()
        ser.write(build_frame(CMD_PING, 0))
        ser.flush()
        for _ in range(20):
            fr = read_frame(ser, 0.3)
            if fr and fr[0] == CMD_PONG:
                return True
    return False


def cli_exec(ser, command: str, timeout: float) -> str:
    if len(command) > 63:
        sys.stderr.write(f"[warn] command is {len(command)} chars; firmware truncates at 63\n")
    seq = 1
    payload = command.encode() + b"\x00"  # firmware expects a null-terminated string
    ser.write(build_frame(CMD_CLI_EXEC, seq, payload))
    ser.flush()
    # Wait for the matching CLI_RESP (skip unsolicited frames like debug logs).
    deadline = time.time() + timeout
    while time.time() < deadline:
        fr = read_frame(ser, max(0.2, deadline - time.time()))
        if not fr:
            continue
        cmd, rseq, pl = fr
        if cmd == CMD_CLI_RESP:
            return pl.rstrip(b"\x00").decode(errors="replace")
        if cmd == CMD_NACK:
            return f"[NACK] err=0x{pl[0]:02X}" if pl else "[NACK]"
    return "[timeout waiting for CLI_RESP]"


CMD_FILE_LIST = 0x30
CMD_FILE_LIST_RESP = 0x31


def file_list(ser, path, timeout=5.0):
    """List a directory. Returns [(is_dir, size, name), ...] or None."""
    ser.reset_input_buffer()
    ser.write(build_frame(CMD_FILE_LIST, 1, path.encode()))
    ser.flush()
    deadline = time.time() + timeout
    while time.time() < deadline:
        fr = read_frame(ser, max(0.2, deadline - time.time()))
        if fr and fr[0] == CMD_FILE_LIST_RESP:
            pl = fr[2]
            i = pl.find(b"\x00") + 1        # skip [path + null]
            out = []
            while i + 9 <= len(pl):
                is_dir = pl[i]; size = int.from_bytes(pl[i+1:i+5], "little")
                i += 9                        # is_dir(1)+size(4)+date(2)+time(2)
                end = pl.find(b"\x00", i)
                if end < 0: break
                out.append((is_dir, size, pl[i:end].decode(errors="replace")))
                i = end + 1
            return out
    return None


def click_button(ser, name):
    bid = BUTTONS.get(name.lower())
    if bid is None:
        sys.exit(f"unknown button {name!r}; choices: {', '.join(BUTTONS)}")
    ser.write(build_frame(CMD_BUTTON_CLICK, 1, bytes([bid])))
    ser.flush()


def screencap(ser, timeout=3.0):
    """Request one screen frame; return the 1024-byte u8g2 framebuffer."""
    ser.reset_input_buffer()
    ser.write(build_frame(CMD_SCREEN_CAPTURE, 1))
    ser.flush()
    deadline = time.time() + timeout
    while time.time() < deadline:
        fr = read_frame(ser, max(0.2, deadline - time.time()))
        if fr and fr[0] == CMD_SCREEN_FRAME:
            return fr[2]
    return None


def render_ascii(fb):
    """u8g2 tile buffer (page-major, bit0=top) -> ASCII, 2 rows per char via
    half-block glyphs so it fits in ~32 lines."""
    def px(x, y):
        if not (0 <= x < SCREEN_W and 0 <= y < SCREEN_H):
            return 0
        byte = fb[(y // 8) * SCREEN_W + x]
        return (byte >> (y % 8)) & 1
    glyph = {(0, 0): " ", (1, 0): "▀", (0, 1): "▄", (1, 1): "█"}
    lines = ["+" + "-" * SCREEN_W + "+"]
    for y in range(0, SCREEN_H, 2):
        row = "".join(glyph[(px(x, y), px(x, y + 1))] for x in range(SCREEN_W))
        lines.append("|" + row + "|")
    lines.append("+" + "-" * SCREEN_W + "+")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="M1 RPC CLI client")
    ap.add_argument("command", nargs="?", help='CLI command, e.g. "espnow info"')
    ap.add_argument("--port", "-p", help="serial port (e.g. /dev/cu.usbmodemXXXX)")
    ap.add_argument("--baud", "-b", type=int, default=115200)
    ap.add_argument("--timeout", "-t", type=float, default=8.0,
                    help="seconds to wait for CLI_RESP (raise for 'espnow listen')")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--button", help="inject a button click: " + "/".join(BUTTONS))
    ap.add_argument("--screencap", action="store_true", help="capture the screen (ASCII)")
    ap.add_argument("--ls", help="list a directory on the SD card (e.g. 0:/IR)")
    args = ap.parse_args()

    if args.list:
        for p in list_ports.comports():
            print(f"{p.device}\t{p.description}")
        return

    if not args.port:
        ap.error("--port is required (or use --list)")

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        if not resync(ser):
            sys.exit("could not handshake (no PONG) — is the M1 in RPC mode?")
        if args.ls:
            entries = file_list(ser, args.ls)
            if entries is None:
                sys.exit(f"no FILE_LIST_RESP for {args.ls}")
            for is_dir, size, name in entries:
                print(f"{'d' if is_dir else '-'} {size:>8}  {name}")
            return
        if args.button:
            click_button(ser, args.button)
            time.sleep(0.15)
        if args.screencap:
            fb = screencap(ser)
            if fb and len(fb) >= SCREEN_W * SCREEN_H // 8:
                print(render_ascii(fb))
            else:
                sys.exit(f"no screen frame (got {0 if not fb else len(fb)} bytes)")
        if args.command:
            out = cli_exec(ser, args.command, args.timeout)
            print(out, end="" if out.endswith("\n") else "\n")
        if not (args.button or args.screencap or args.command):
            ap.error("give a command, --button, and/or --screencap")


if __name__ == "__main__":
    main()
