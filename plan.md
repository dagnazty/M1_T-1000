# M1 Link — Implementation Plan & Tracker

Device-to-device wireless link between M1 units over the Si4463 sub-GHz radio
(915 MHz FSK packet mode). Full protocol/design rationale lives in
[`documentation/m1_link_design.md`](documentation/m1_link_design.md); this file
tracks execution.

**Decisions locked:** M1 ↔ M1 · sub-GHz Si4463 · first feature = text messaging
("M1 Chat"). Open questions (range target, v1 encryption, mesh vs 2-device,
callsign naming) are answered before Phase 1 — none block Phase 0.

---

## Status legend
`[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked

---

## Phase 0 — Radio spike (GO/NO-GO gate)   `[x] PASSED`

Prove FSK packet TX/RX works between two M1s before building anything on top.

- [x] Confirm radio primitives exist; identify the one gap (RX-FIFO read)
- [x] Find the FSK-vs-OOK 915 config collision (FSK header is guarded out today)
- [x] Add `SI446x_Read_RxFiFo()` primitive + `PH_PEND` / RX-FIFO-count accessors
- [x] New module `m1_link.c/.h`: FSK packet bring-up, `tx_raw`, `rx_poll`
- [x] CLI harness: `link beacon|listen|info` for the two-device bring-up test
- [x] Compile clean on host toolchain (`./build.sh`)
- [x] **On real hardware (2 units on USB, driven over RPC `CLI_EXEC`):**
  - `link info`: radio brings up in 915 FSK packet mode — `part=0x4463 rev=0x22`,
    "SI446x patch applied", radio OK. ✔
  - `link beacon`: TX confirms `PACKET_SENT` for every packet (`tx OK`). ✔
  - `link listen`: **receives 0 packets.** -> TX works, RX does not.
- [x] **Fix RX and re-run gate** — **PASSED: 20/20 packets, seq 0-19, 0 loss,
      0 dupes.** Payload `[4D 31 <seq> A5 A5 ...]` == what was transmitted.

**Gate: PASSED → proceed to Phase 1.**

### RX-not-receiving investigation + FIX (real hardware)
- Units inches apart, so NOT range. Antenna switch (RFSW1/2) verified correct.
- `rxdiag` proved: with high-duty TX, receiver RSSI spikes (~96 raw vs ~55 noise)
  → RF path works BOTH ways. But modem never detected preamble/sync (`modem=0x00`)
  → the failure was **demodulation**, not RF.
- Root cause: the WDS 915 FSK config is a raw/direct-mode config —
  `PREAMBLE_TX_LENGTH=0`, `SYNC_CONFIG=0x80` (SKIP_TX) with zero sync bits,
  `PKT_CRC_CONFIG=0` and zeroed field lengths. It radiates energy carrying no
  preamble/sync for the receiver to lock onto.
- **Fix (in `m1_link_radio_bringup`):** after `ConfigInit`, override the packet
  handler via new `SI446x_Set_Property()`:
  - PREAMBLE (grp 0x10): TX_LENGTH=8, CONFIG=0x31 (standard 1010).
  - SYNC (grp 0x11): CONFIG=0x01 (TX on, 2 bytes), sync word = 0x2D 0xD4.
  - PKT (grp 0x12): fixed length, no CRC, single 25-byte FIELD_1 + RX_FIELD_1.
- `link rxdiag [s]` added as the RX localization tool (kept for future tuning).
- Known minor: RSSI now read from CURR_RSSI (config doesn't latch RSSI at sync,
  so LATCH_RSSI read 0 → bogus dBm). Fixed in source (not yet reflashed).

### Transport notes (how the test is driven)
- USB CDC is in **VCP/RPC mode** (RPC enabled), so the text CLI is not on USB.
  Drive the `link` commands via RPC `CLI_EXEC` (0x60) → `CLI_RESP` (0x61).
- The firmware's RPC `s_crc16_table` is **non-standard** (46 entries in the
  0x40-0x7F range differ from correct CRC-16/CCITT). Clients MUST use the
  firmware's exact table (qMonstatek does). Empty-payload frames like PING pass
  by luck (never index 0x40-0x7F); payload frames fail unless the real table is used.
- The RPC byte-parser is stateful across feeds — send a PING and wait for PONG to
  resync to a frame boundary before sending payload frames.
- Scratchpad tooling: `m1rpc.py` (RPC client, correct CRC table), `link_test.py`
  (concurrent beacon/listen), `flash.py` (inactive-bank flash over RPC — works but
  reset a unit at ~12%; unreliable, prefer the owner's normal flash method).

### Phase 0 notes / findings
- Existing sub-GHz app TXes in **direct/raw mode** (GPIO2-streamed), never the
  FIFO packet path — and loads the **OOK** 915 config even for "FSK". So the link
  must load the real FSK packet config (own translation unit) to get hardware
  preamble/sync/CRC-16. This is why `m1_link.c` includes the FSK header directly.
- Radio is **exclusive**: M1 Link vs the sub-GHz scan/replay app cannot both own
  the Si4463. Spike assumes the user isn't running a sub-GHz scan concurrently;
  Phase 4 adds a formal ownership guard.
- Packet framing for the spike is **fixed 25-byte** payloads (matches the WDS
  `RADIO_PACKET_LENGTH=0x19`); our own length/format lives inside that in Phase 1.

---

## Phase 1 — Framing + reliable unicast   `[x] PASSED`
- [x] Frame header (proto/ver 0x31, type/flags, src/dst 16-bit IDs LE, seq, len,
      17-byte payload) — `M1_LINK_*` in m1_link.h, build/parse in m1_link.c
- [x] Short IDs from STM32 UID (HAL_GetUIDw0/1/2 folded to 16-bit; 0/FFFF remapped)
- [x] Stop-and-wait ARQ (`m1_link_send`): ACK-req, wait ACK, retry x4, backoff;
      receiver auto-ACKs unicast + dedups retransmits; broadcast = fire-and-forget
- [x] CLI: `link id`, `link send <hexid|FFFF> <text>`, `link recv [s]`
- [x] **Hardware test PASSED** (units 6C4D / 6A30):
  - B→A "hello from B" → B: "delivered (ACK received)", A: RX seq=1 UNICAST. ✔
  - A→B "reply from A" → delivered + ACK (symmetric, both directions). ✔
  - send to wrong id 1234 → "NO ACK after 4 retries"; receiver ignored it
    (retry/failure + address filtering both correct). ✔
  - RSSI now sane (~-99 dBm) after CURR_RSSI fix.

## Phase 2 — Fragmentation + discovery   `[x] PASSED`
- [x] Header adds `frag` byte (idx<<4 | total); 16-byte payload/fragment, up to
      15 frags = 240-byte messages. Per-fragment stop-and-wait ARQ (ACK echoes
      seq+frag). Receiver reassembles by (src,seq) with a 2-slot context table.
- [x] HELLO beacon (type 0x03) + `link scan [s]`: broadcasts our id+name every 1s
      and lists discovered peers. `link send`/`recv` now fragment/reassemble.
- [x] **Hardware test PASSED** (units 6C4D/6A30):
  - 44-byte message: `frags=3`, all ACKed, reassembled **byte-perfect** (integrity OK).
  - `link scan` on both: each discovered the other (`6A30 "M1-6A30"` /
    `6C4D "M1-6C4D"`) via HELLO. ✔

## Phase 3 — M1 Chat UI   `[x] PASSED (owner-tested on both devices: messaging works)`
- [x] Menu entry "M1 Link" under Sub-GHz (m1_menu.c). App = `m1_link_app.c`.
- [x] Live peer list (from HELLO beacons) + "[Broadcast]" entry; up/down/OK nav.
      Fix: peer list drew 4 rows (4th at y=54 collided with the bottom bar at
      y=51 -> unreadable); now 3 visible rows with scroll offset, like Settings.
- [x] Conversation view: last messages (`<` recv / `>` sent), OK=compose.
- [x] Compose via the FULL keyboard `m1_vkb_get_filename` (abc/ABC/#$% pages;
      NOT the hex-only `m1_vkbs_get_data`). Caps at 20 chars/msg (kb limit).
      Buzzer notify on RX.
- [x] Reusable RX engine `m1_link_rx_process` (reassembly + auto-ACK) shared by UI.
- [x] Owner-tested on both devices: messaging works (RPC verify not needed).
      Scratchpad tool `m1ui.py` (button-inject + screen-capture) available if wanted.

## Phase 4 — Hardening + security   `[~]` built, awaiting hardware test
- [x] **AES-256-CBC encryption**: shared passphrase -> 32-byte key via salted
      MD5 KDF (`link_derive_key`). Whole message encrypted (IV+PKCS7) then
      fragmented; `M1_LINK_FLAG_ENC` bit; RX decrypts after reassembly, drops on
      wrong key. Max plaintext 208B. (KDF is MD5-based, not PBKDF2 — noted.)
- [x] **Listen-before-talk**: `link_lbt_wait` arms RX + backs off while
      CURR_RSSI >= busy threshold before each DATA TX (ACKs/HELLOs skip it).
- [x] **Channels**: runtime `channel` 0..9 (was hard-coded 0); TX power 0..3.
- [x] **Settings screen** (in-app: `[Settings]` list entry): callsign, passphrase,
      channel, TX power — persisted to `0:/M1LINK.CFG`, loaded on first bring-up.
- [x] CLI test hooks: `link cfg|key <p>|chan <n>|name <s>|save` (lets me verify
      encryption + channels over RPC without the UI).
- [ ] **Hardware test:** same-key encrypted send decrypts; mismatched key drops;
      different channel = no comms  ← needs Phase-4 build flashed
- [ ] Deferred: radio ownership guard vs sub-GHz app; replay-window protection.

## Phase 5 — Extensions   `[~]` (order: ping/locate -> capture sharing -> remote trigger)
- [x] **Ping / Locate** built: new frame types PING/PONG/LOCATE; `m1_link_ping`
      (round-trip ms + peer-heard dBm from PONG payload), `m1_link_locate`
      (peer beeps via m1_buzzer + flashes LED, ACK'd). Auto-responses in
      `rx_process`. UI: peer -> actions menu (Message/Ping/Locate) with result
      card. CLI: `link ping <id>` / `link locate <id>`.
  - [x] **Hardware test PASSED**: ping = `PONG rtt=6 ms`; locate = peer acked + beeped.
- [x] **Capture sharing** built: file transfer rides the reliable DATA path via a
      new `M1_LINK_FLAG_FILE` bit (reuses fragmentation + ARQ + AES for free).
      PDU = OFFER([size][name]) / CHUNK([offset][data], 192B) / DONE. Receiver
      writes to SD in `rx_process`, routed by extension (.sub->SUBGHZ, .nfc->NFC,
      .ir->IR, else LINK_RX). Sender `m1_link_send_file` (OFFER+CHUNKs+DONE).
      UI: peer actions -> "Send File" -> type picker -> storage_browse -> progress
      card; incoming files logged + buzzer in the app loop. CLI: `link sendfile`.
  - [ ] **Hardware test** (needs this build flashed): send a .sub A->B, verify it
        lands in B's 0:/SUBGHZ and matches byte-for-byte.
- [x] **Remote trigger (multi-payload)** built: `M1_LINK_FLAG_TRIG` DATA carrying
      [type][name]. Types: SUB (`0:/SUBGHZ` -> sub_ghz_replay_flipper_file + radio
      re-bringup), BADUSB (`0:/BadUSB` -> badusb_execute_file), BADBT
      (`0:/BadUSB` -> badbt_execute_file, un-static'd), IR (`0:/IR` ->
      new ir_universal_play_file). Safety: only honored ENCRYPTED (shared
      passphrase) + ACKed BEFORE running (no double-fire). UI: peer -> Trigger ->
      pick type -> browse dir -> send. CLI: `link trigger <id> <sub|badusb|badbt|ir> <name>`.
  - [ ] **Hardware test** (needs v0.3.0 flashed): with same passphrase, trigger a
        peer's saved .sub and confirm it transmits; without passphrase it's ignored.

### Release
- [x] Version bumped 0.2.3 -> **0.3.0** (`m1_t1000_version.h` + `CMakeLists.txt`);
      Monstatek FW_VERSION_* and C3 revision left untouched. CHANGELOG updated.

---

## Files touched (Phase 0)
- `Sub_Ghz/m1_sub_ghz_api.c` / `.h` — `SI446x_Read_RxFiFo` + accessors, exposed FIFO/config funcs
- `m1_csrc/m1_link.c` / `.h` — the link module (new)
- `m1_csrc/m1_compile_cfg.h` — `M1_APP_LINK_ENABLE` flag
- `Core/Src/cli_app.c` — `link` CLI command
- `cmake/m1_01/CMakeLists.txt` — build `m1_link.c`
