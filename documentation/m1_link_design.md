<!-- See COPYING.txt for license details. -->

# M1 Link — Device-to-Device Wireless Protocol (Design + Plan)

**Goal:** let two (or more) M1 units talk to each other directly, over the radios
they already carry, with no PC or phone in the middle.

**Decision (this doc):** M1 ↔ M1 over the **Si4463 sub-GHz transceiver**, 915 MHz
FSK. First user-facing feature: **text messaging / pager ("M1 Chat")**, chosen
because it exercises the entire stack and every other feature (capture sharing,
remote trigger, presence) is a specialization of it.

---

## 1. Why sub-GHz FSK (and not BLE/WiFi)

| Option | Range | Throughput | Both ends ours? | New C6 firmware? |
|--------|-------|-----------|-----------------|------------------|
| **Si4463 915 FSK** | **long (100s of m LOS)** | low (~few kbps) | **yes** | no |
| BLE via ESP32-C6 | short (~10 m) | high | phone-compatible | no (AT+BLE* exists) |
| WiFi/ESP-NOW via C6 | medium | highest | yes | **yes** (add AT cmd) |

Sub-GHz wins for M1↔M1: license-free ISM, long range, and **we own both ends of
the air interface**, so we control the whole protocol. Low bandwidth is fine for
messaging and small capture transfers.

## 2. What the hardware already gives us

Confirmed in-tree (`Sub_Ghz/m1_sub_ghz_api.c`, `.h`, `si446x_cmd.h`):

- **FSK config header** already exists:
  `Sub_Ghz/radio_config/m1_sub_ghz_915_rxtx_spc25_bwauto_baud106_dev200_e1_10_fsk.h`
- **`radio_init_rx_tx(band, MODEM_MOD_TYPE_2FSK, reset)`** — init in FSK packet mode.
- **`SI446x_Start_Tx(channel, buf, len)`** — resets TX FIFO, writes payload,
  fires `START_TX`. **Complete TX path.**
- **`SI446x_Start_Rx(channel)`** — arms the receiver.
- **`SI446x_Get_IntStatus()`** + `si446x_nIRQ_active` flag — `PACKET_SENT`,
  `PACKET_RX`, `CRC_ERROR` bits are all defined.
- **`SI446x_Get_ModemStatus()`** — RSSI, for listen-before-talk + link quality.
- Packet Handler does **CRC-16 in hardware** → integrity is free.

**One missing primitive:** there is no C wrapper that reads the RX FIFO.
`SI446X_CMD_ID_READ_RX_FIFO` (0x77) is defined but unused. We add
`SI446x_Read_RxFiFo(len, buf)` mirroring the existing `SI446x_Write_TxFiFo()`
(~15 lines). This is the only radio-driver gap.

**Constraints to design around:**

- **Half-duplex** — one transceiver; cannot RX and TX at once. Everything is
  turn-based (stop-and-wait).
- **Packet length** — `RADIO_PACKET_LEN_MAX = 25` is baked into the WDS-generated
  config ("must match the one set in WDS"). We design the frame to fit **≤25
  bytes on air** and fragment anything larger. (A larger/variable-length config
  can be regenerated from Silicon Labs WDS later if we want bigger frames.)
- **Shared `hspi2` bus with NFC** (CS-arbitrated) — the link's RX task must not
  starve NFC; already handled by the existing CS scheme, but the link task must
  cooperate.
- The current sub-GHz app owns the radio in **OOK raw-sample** mode. The link
  needs the radio in **FSK packet** mode → the two features are **mutually
  exclusive** and must arbitrate ownership of the radio.

## 3. Protocol stack

```
┌─────────────────────────────────────────────┐
│ APP    M1 Chat · capture share · remote trig │
├─────────────────────────────────────────────┤
│ TRANSPORT  stop-&-wait ARQ · fragmentation   │
├─────────────────────────────────────────────┤
│ FRAME/MAC  proto/ver · type · src/dst · seq  │
├─────────────────────────────────────────────┤
│ PHY  Si4463 915 FSK · preamble/sync/CRC (HW) │
└─────────────────────────────────────────────┘
```

### 3.1 Frame layout (fits one 25-byte FIFO packet)

Radio PH adds preamble, sync word, and CRC-16 automatically — the bytes below are
the FIFO payload only.

```
off  size  field
0    1     proto/ver   0x31 = 'M1 Link' v1  (sanity + version gate)
1    1     type/flags  bits[2:0] msg type (HELLO/DATA/ACK/FRAG/PING)
                        bit3 ACK-req   bit4 more-fragments   bit5 encrypted
2    2     src_id      16-bit short address (hash of STM32 96-bit UID)
4    2     dst_id      16-bit; 0xFFFF = broadcast
6    1     seq         per-source rolling sequence number
7    1     frag        high nibble = index, low nibble = total (1..15)
8..24 ≤17  payload     up to ~17 bytes/frame
```

- **Short IDs** are derived once from the STM32 unique ID and shown to the user as
  a 4-hex "callsign" (e.g. `A3F1`). No manual address config needed.
- **CRC** is the radio's hardware CRC-16 — frames that fail CRC never reach us.

### 3.2 Transport

- **Unicast = stop-and-wait ARQ.** TX a DATA frame with ACK-req set, flip to RX,
  wait for a matching ACK (src/dst/seq) within `T_ack` (~generous, half-duplex
  turnaround). On timeout, retransmit up to `N_retry` with randomized backoff.
- **Broadcast** (dst `0xFFFF`, e.g. HELLO) is unacknowledged.
- **Fragmentation:** payloads larger than one frame are split across frames using
  the `frag` field + more-fragments flag, reassembled by (src_id, seq). A
  ~200-char message ≈ 12 fragments.

### 3.3 Channel access

Listen-before-talk: read RSSI via `SI446x_Get_ModemStatus()` before TX; if the
channel is busy, random backoff. Sufficient for a handful of devices in range.

### 3.4 Discovery / pairing

- **HELLO beacon** (broadcast, low duty) carries short ID + friendly name →
  builds a "nearby devices" list, reusing the list-UI pattern already used by
  `bluetooth_scan()` / `zigbee_scan()`.
- **Pairing (optional):** both users type a shared passphrase → derive a key.

### 3.5 Security (deferred to Phase 4, not blocking first demo)

`m1_crypto.c` already exists. Add optional **AES-CTR/GCM** with a pre-shared key
derived from a passphrase, plus replay protection via seq + session nonce. v1
ships plaintext + hardware CRC so we can demo the link first, then layer crypto.

## 4. Phased plan

Each phase is independently testable and leaves `main` buildable.

### Phase 0 — Radio spike (GO/NO-GO gate)
- Add `SI446x_Read_RxFiFo()` to `m1_sub_ghz_api.c/.h`.
- New module `m1_csrc/m1_link.c` + `.h`.
- Bring up FSK packet mode: one M1 TXs a fixed 8-byte packet every 1 s; the other
  RXs and logs payload + RSSI + CRC-pass rate over CLI.
- **Gate:** reliable packets at useful range → proceed. This de-risks everything.

### Phase 1 — Framing + reliable unicast
- Frame header (§3.1); short IDs from STM32 UID.
- Stop-and-wait ARQ with retry/timeout/backoff.
- CLI: `link send <dst> <text>` device→device round-trip with ACK.

### Phase 2 — Fragmentation + discovery
- Fragment/reassemble arbitrary-length payloads.
- HELLO beacon + nearby-device list.

### Phase 3 — M1 Chat UI (first shipped feature)
- Main-menu entry **"M1 Link"** → device list → conversation view.
- Compose via `m1_virtual_kb.c`; notify via buzzer + RGB LED; keep recent
  messages in RAM (persist to SD later).

### Phase 4 — Hardening + security
- CSMA/LBT tuning, AES via `m1_crypto.c`, config screen (callsign, channel, key),
  duty-cycle/power review.

### Phase 5 — Extensions
- Capture sharing: send saved `.sub` / `.nfc` / `.ir` files over the fragmented
  transport. Remote trigger: one M1 fires another's saved signal / pings / locates.

## 5. Radio ownership / task model

- Dedicated FreeRTOS **link task**: state machine (IDLE → RX-listen → TX → wait-ACK),
  driven by `si446x_nIRQ_active` + a timeout.
- **Mutual exclusion** with the sub-GHz scan/replay app: only one may hold the
  radio (FSK-packet vs OOK-raw). Add an ownership guard; entering M1 Link tears
  down any active sub-GHz raw session and re-inits the radio in FSK mode, and
  vice-versa.

## 6. Risks & open items

| Risk | Mitigation |
|------|------------|
| Half-duplex TX→RX turnaround too slow to catch ACK | generous ACK window; measure in Phase 0 |
| 25-byte WDS packet len caps frame size | design for ≤25B + fragment; regenerate a larger WDS config only if needed |
| Radio contention with NFC on hspi2 | cooperate with existing CS arbitration; keep RX polling bounded |
| Radio ownership vs sub-GHz raw app | explicit ownership guard + re-init on entry/exit |
| FCC Part 15 / duty cycle at 915 MHz | low duty by design; beacon rate limited |

## 7. Open questions for the owner

1. **Callsign vs friendly name** — auto 4-hex ID only, or also a user-set name?
2. **Range target** — same-room demo, or "across the building" (drives TX power +
   antenna expectations)?
3. **Encryption in v1** or defer to Phase 4?
4. **Two devices only**, or design addressing for a small **mesh/relay** from the
   start? (Frame already has src/dst; relay is additive.)
