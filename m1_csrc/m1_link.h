/* See COPYING.txt for license details. */

/*
 * m1_link.h
 *
 * M1 Link — device-to-device wireless link between M1 units over the Si4463
 * sub-GHz radio (915 MHz FSK packet mode).
 *
 * Phase 0 (this file): radio bring-up + raw fixed-length packet TX/RX plus a
 * beacon/listen spike used to prove the air interface. Framing, addressing and
 * reliable transport arrive in later phases — see documentation/m1_link_design.md
 * and plan.md.
 *
 * M1 Project
 */

#ifndef M1_LINK_H_
#define M1_LINK_H_

#include "m1_compile_cfg.h"

#ifdef M1_APP_LINK_ENABLE

#include <stdint.h>
#include <stdbool.h>

/* On-air parameters for the Phase-0 spike. The Si4463 FSK config is fixed
 * packet length = 0x19 (25) bytes, so every frame is padded/truncated to this. */
#define M1_LINK_CHANNEL        0u
#define M1_LINK_PKT_LEN        25u    /* radio WDS RADIO_PACKET_LENGTH (0x19) */
#define M1_LINK_TX_POWER       127u   /* max PA level (matches sub-ghz "Max") */

/* ---- Phase 1: frame header + reliable unicast ---- */

/* Frame (fits one 25-byte radio packet):
 *   off size field
 *   0   1    proto/ver  (M1_LINK_PROTO_VER)
 *   1   1    type/flags (type in low 3 bits, ACK-req = bit3)
 *   2   2    src_id (LE)
 *   4   2    dst_id (LE, 0xFFFF = broadcast)
 *   6   1    seq        (message id; same across all fragments of a message)
 *   7   1    frag       (high nibble = index, low nibble = total, 1..15)
 *   8   1    len        (this fragment's payload length, 0..16)
 *   9   16   payload
 */
#define M1_LINK_PROTO_VER      0x31u
#define M1_LINK_HDR_LEN        9u
#define M1_LINK_FRAG_PAYLOAD   (M1_LINK_PKT_LEN - M1_LINK_HDR_LEN)          /* 16 */
#define M1_LINK_MAX_FRAGS      15u
#define M1_LINK_MAX_MSG        (M1_LINK_FRAG_PAYLOAD * M1_LINK_MAX_FRAGS)   /* 240 */
#define M1_LINK_ID_BROADCAST   0xFFFFu

#define M1_LINK_TYPE_MASK      0x07u
#define M1_LINK_TYPE_DATA      0x01u
#define M1_LINK_TYPE_ACK       0x02u
#define M1_LINK_TYPE_HELLO     0x03u
#define M1_LINK_TYPE_PING      0x04u
#define M1_LINK_TYPE_PONG      0x05u
#define M1_LINK_TYPE_LOCATE    0x06u
#define M1_LINK_FLAG_ACKREQ    0x08u
#define M1_LINK_FLAG_ENC       0x20u   /* payload (whole message) is AES-encrypted */
#define M1_LINK_FLAG_FILE      0x40u   /* DATA payload is a file-transfer PDU      */

/* File-transfer PDU op codes (first byte of a FLAG_FILE message payload):
 *   OFFER: [op=1][size:4 LE][name...]        (name = remaining bytes)
 *   CHUNK: [op=2][offset:4 LE][data...]
 *   DONE:  [op=3]
 * A file transfer reuses the reliable DATA path (fragmentation + ARQ + AES). */
#define M1_LINK_FILE_OP_OFFER  1u
#define M1_LINK_FILE_OP_CHUNK  2u
#define M1_LINK_FILE_OP_DONE   3u
#define M1_LINK_FILE_CHUNK     192u    /* data bytes per CHUNK message */
#define M1_LINK_FILE_NAME_MAX  40u

/* ---- Phase 4: configuration (callsign / passphrase / channel / power) ---- */
#define M1_LINK_CHAN_MAX       9u      /* channels 0..9 */
/* Each channel is its own absolute frequency, spaced far wider than the RX
 * bandwidth so channels are genuinely isolated (not just perturbed). ch0..ch9
 * = 915..924 MHz, all within the 902-928 ISM band. */
#define M1_LINK_CH_BASE_HZ     915000000UL
#define M1_LINK_CH_STEP_HZ     1000000UL
#define M1_LINK_CFG_PATH       "0:/M1LINK.CFG"

typedef struct {
    char    callsign[14];   /* friendly name in HELLO beacons */
    char    passphrase[21]; /* shared secret; empty = no encryption */
    uint8_t channel;        /* 0..M1_LINK_CHAN_MAX */
    uint8_t tx_power_idx;   /* 0..3 -> {10,40,80,127} */
} m1_link_cfg_t;

/* Access / persist the live config. cfg_apply() re-derives the AES key from the
 * passphrase and must be called after any change. load() falls back to sane
 * defaults (callsign "M1-xxxx", no passphrase, channel 0, max power). */
m1_link_cfg_t *m1_link_cfg(void);
void m1_link_cfg_defaults(void);
void m1_link_cfg_load(void);
void m1_link_cfg_save(void);
void m1_link_cfg_apply(void);
bool m1_link_encrypted(void);   /* true when a passphrase is set */

/* CLI/test helper to set config fields (NULL / negative = leave unchanged). */
void m1_link_cfg_set(const char *callsign, const char *passphrase,
                     int channel, int tx_power_idx, bool persist);

/* ARQ tuning */
#define M1_LINK_ACK_TIMEOUT_MS 120u
#define M1_LINK_MAX_RETRY      4u

/*
 * Bring the Si4463 up in 915 MHz FSK packet mode for the link.
 * Loads the real FSK packet config (hardware preamble/sync/CRC-16), selects the
 * 915 front-end and clears interrupts. Exclusive with the sub-GHz scan/replay
 * app — the caller must own the radio. Returns true on success.
 */
bool m1_link_radio_bringup(void);

/*
 * Transmit one fixed-length packet. Up to M1_LINK_PKT_LEN payload bytes are
 * sent; shorter buffers are zero-padded. Blocks until PACKET_SENT or timeout.
 * Returns true on a confirmed send.
 */
bool m1_link_tx_raw(const uint8_t *buf, uint8_t len);

/* Arm the receiver to acquire one packet on the link channel. */
void m1_link_rx_arm(void);

/*
 * Poll for a received packet (non-blocking). Returns the byte count (>0) when a
 * CRC-valid packet arrived and re-arms RX; 0 when nothing is ready. On success
 * fills buf (caller provides >= M1_LINK_PKT_LEN bytes) and, if non-NULL, *rssi
 * with the latched RSSI (raw Si4463 units).
 */
uint8_t m1_link_rx_poll(uint8_t *buf, int8_t *rssi);

/* ---- Phase-0 spike entry points (driven from the `link` CLI command) ---- */

/* Beacon `count` fixed-length packets, each carrying an incrementing sequence
 * byte, `period_ms` apart. Prints per-packet status to the console. */
void m1_link_spike_beacon(uint32_t count, uint32_t period_ms);

/* Listen for up to `seconds`, printing each received packet's sequence byte,
 * RSSI and a short hex dump. */
void m1_link_spike_listen(uint32_t seconds);

/* Print radio part info / link config for a quick sanity check. */
void m1_link_spike_info(void);

/* RX diagnostic: arm RX and periodically report chip state, RSSI and modem
 * preamble/sync-detect flags (run opposite a beacon to localize RX breakage). */
void m1_link_spike_rxdiag(uint32_t seconds);

/* ---- Phase 1 API ---- */

/* This unit's 16-bit short address (stable, derived from the STM32 UID). */
uint16_t m1_link_my_id(void);

/* Reliable send with stop-and-wait ARQ, fragmenting messages up to
 * M1_LINK_MAX_MSG bytes across multiple packets (each fragment ACKed for
 * unicast). For dst == broadcast fragments are sent once, unacknowledged.
 * Returns true when the whole message is delivered (or broadcast sent). */
bool m1_link_send(uint16_t dst, const uint8_t *data, uint16_t len);

/* CLI spike: send `text` to dst (hex short id, or 0xFFFF broadcast), report ACK. */
void m1_link_spike_send(uint16_t dst, const char *text);

/* CLI spike: listen for `seconds`, reassemble + print received messages and
 * auto-ACK unicast fragments addressed to us. */
void m1_link_spike_recv(uint32_t seconds);

/* CLI spike: for `seconds`, periodically broadcast a HELLO beacon (our id+name)
 * and listen, printing discovered peers. Run on both units to find each other. */
void m1_link_spike_scan(uint32_t seconds);

/* ---- Phase 5: ping / locate ---- */

/* Ping a peer: send PING, wait for its PONG. Returns round-trip time in ms
 * (>=0) or -1 on timeout. If peer_rssi != NULL, *peer_rssi = the dBm at which
 * the peer heard our ping (from the PONG payload). */
int  m1_link_ping(uint16_t dst, int8_t *peer_rssi);

/* Locate a peer: ask it to beep + flash its LED so it can be found. ACK'd.
 * Returns true if the peer acknowledged. */
bool m1_link_locate(uint16_t dst);

/* CLI spikes for ping/locate. */
void m1_link_spike_ping(uint16_t dst);
void m1_link_spike_locate(uint16_t dst);

/* ---- Phase 5: capture file sharing ---- */

/* Reliable send with extra frame flags OR'd in (e.g. M1_LINK_FLAG_FILE).
 * m1_link_send() is this with extra=0. */
bool m1_link_send_flags(uint16_t dst, const uint8_t *data, uint16_t len, uint8_t extra);

/* Action the receiver takes once a file transfer completes. SAVE just stores it
 * (routed by extension). The others store it in the payload's folder AND run it
 * (only when the transfer arrived encrypted). Exec values match M1_LINK_TRIG_*. */
#define M1_LINK_FILE_ACT_SAVE   0u

/* Send a file from SD (path) to a peer: OFFER + CHUNKs + DONE, each reliably.
 * `action` is M1_LINK_FILE_ACT_SAVE or an M1_LINK_TRIG_* exec type.
 * Blocks until done. Returns true on success. If sent != NULL, *sent = bytes. */
bool m1_link_send_file(uint16_t dst, const char *path, uint8_t action, uint32_t *sent);

/* CLI spike: send a file to dst (action = save). */
void m1_link_spike_sendfile(uint16_t dst, const char *path);

/* ---- Phase 5: remote trigger ---- */

/* Payload type a trigger asks the peer to run (first byte of the trigger PDU).
 * BadUSB and Bad-BT both live in 0:/BadUSB and both use .txt, so the type is
 * explicit rather than inferred from the name. */
#define M1_LINK_TRIG_SUB     1u   /* 0:/SUBGHZ/<name>  -> sub-GHz replay      */
#define M1_LINK_TRIG_BADUSB  2u   /* 0:/BadUSB/<name>  -> USB HID (DuckyScript) */
#define M1_LINK_TRIG_BADBT   3u   /* 0:/BadUSB/<name>  -> BLE HID (DuckyScript) */
#define M1_LINK_TRIG_IR      4u   /* 0:/IR/<name>      -> IR transmit          */

/* Send one of OUR payload files (full `path`) to a peer AND have it run there:
 * the file is transferred into the payload's folder on the peer and executed
 * (sub-GHz replay / BadUSB / Bad-BT / IR) when the transfer completes.
 * REQUIRES a shared passphrase on both units (exec only happens when the
 * transfer arrives encrypted). Returns true if the whole transfer was ACK'd. */
bool m1_link_trigger(uint16_t dst, uint8_t ptype, const char *path);

/* CLI spike: send + remote-run a payload file. */
void m1_link_spike_trigger(uint16_t dst, uint8_t ptype, const char *path);

/* ---- Phase 3: reusable receive engine for the on-device UI ---- */

typedef enum {
    M1_LINK_RX_NONE = 0,   /* nothing (or an ACK / non-matching frame) */
    M1_LINK_RX_MESSAGE,    /* a complete DATA message was reassembled  */
    M1_LINK_RX_HELLO,      /* a HELLO beacon was received              */
    M1_LINK_RX_PONG,       /* a PONG reply to our PING (*rssi = link)  */
    M1_LINK_RX_LOCATE,     /* a LOCATE request (we buzzed + flashed)   */
    M1_LINK_RX_FILE_START, /* incoming file transfer began (buf=name)  */
    M1_LINK_RX_FILE_DATA,  /* a file chunk was written (progress)      */
    M1_LINK_RX_FILE_END,   /* incoming file complete (buf=saved path)  */
    M1_LINK_RX_TRIGGER     /* a peer asked us to replay a saved signal */
} m1_link_rx_kind_t;

/* Progress of an in-flight incoming file transfer (bytes). */
uint32_t m1_link_file_rx_total(void);
uint32_t m1_link_file_rx_done(void);

/* Reset the fragment-reassembly state (call when starting a UI session). */
void m1_link_rx_reset(void);

/* Non-blocking: process at most one received packet. On MESSAGE/HELLO fills
 * *from, buf (NUL-terminated, up to buflen) and *outlen, *rssi. Handles
 * reassembly and auto-ACK of unicast frames addressed to us internally. */
m1_link_rx_kind_t m1_link_rx_process(uint16_t *from, char *buf, uint16_t buflen,
                                     uint16_t *outlen, int8_t *rssi);

/* Broadcast one HELLO beacon (our id + "M1-xxxx" name), then re-arm RX. */
void m1_link_send_hello(void);

/* On-device Chat UI — the "M1 Link" menu app entry point. */
void m1_link_app_run(void);

#endif /* M1_APP_LINK_ENABLE */
#endif /* M1_LINK_H_ */
