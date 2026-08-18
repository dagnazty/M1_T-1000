# Changelog

All notable changes to the M1 T-1000 firmware will be documented in this file.

## [Unreleased]

## [0.4.0] - 2026-08-16

### Added
- **ESP Link — remote trigger over ESP32 (ESP-NOW).** New top-level "ESP Link"
  menu app: discover peers (HELLO), pair, set a shared passphrase, and remotely
  trigger a peer over 2.4 GHz ESP-NOW. The actual payload is transferred
  (AES-256 encrypted, per-fragment ACK'd) and the peer writes + runs it:
  - **BadUSB** — DuckyScript is transferred, written to `0:/BadUSB`, and executed
    (USB switches to HID). Verified end-to-end.
  - **IR** — a single button is extracted from a `.ir` remote and transferred;
    the peer plays that one signal from `0:/IR`. Verified end-to-end.
  - Encrypted-only + pairing gates; a wrong/absent passphrase is dropped.
  - CLI `espnow` (on/off/info/key/scan/pair/send/listen) and host tool
    `tools/m1rpc.py`. Companion T-800 firmware provides the `AT+M1ESPNOW*` set.
- Sub-GHz remote trigger is temporarily shelved from ESP Link (its replay runs a
  blocking UI loop that doesn't fit the receive path yet).

### Notes
- Large payloads currently transfer at ~1.3 s/fragment; fine for typical scripts
  and parsed IR. A per-fragment speed-up (SPI-AT return-on-OK) is the open item.

## [0.3.11] - 2026-08-16

### Added
- **ESP-NOW link Phase 4: IR + Sub-GHz remote triggers.** The remote-trigger /
  payload-transfer now handles `.ir` and `.sub` files alongside BadUSB. The
  receiver routes by payload type — writes to `0:/IR` (`ir_universal_play_file`)
  or `0:/SUBGHZ` (`sub_ghz_replay_flipper_file`) as well as `0:/BadUSB`
  (`badusb_execute_file`). Sender: `espnow send` infers the type from the file
  extension (`.sub`/`.ir`/else BadUSB); the on-device ESP Link app adds a
  BadUSB / IR / Sub-GHz type picker before the file browser.

## [0.3.10] - 2026-08-16

### Changed
- **ESP-NOW commands 3x faster (3s -> 1s each).** `spi_AT_send_recv` always
  waits its full (whole-second) timeout for the custom `AT+M1ESPNOW*` commands
  because the SPI-AT layer doesn't match their `OK` back to the caller; dropped
  the per-command timeout from 3s to the 1s API minimum. A multi-fragment
  payload transfer is now ~1s/fragment. (Sub-second would require fixing the
  OK-terminator matching in the shared SPI-AT transport — deferred.)

## [0.3.9] - 2026-08-16

### Fixed
- **ESP-NOW payload transfer: fragment size 160 -> 32 bytes.** A 160-byte chunk
  made the `AT+M1ESPNOWSEND=...,"<hex>"` command ~375 chars, exceeding the
  ESP-AT command-line length cap, so every fragment failed. Chunks are now 32
  bytes (command stays well under the cap). The ESP trigger queue was deepened
  (8 -> 32) and the M1 now drains it fully each poll, so the extra fragments
  don't overflow. Requires the matching T-800 ESP firmware (`ENOW_FILE_CHUNK`).

## [0.3.8] - 2026-08-15

### Added
- **`espnow send <mac> <name|path> [remotename]` CLI** — scriptable payload
  transfer + run (mirrors the UI's Trigger BadUSB), so the whole flow can be
  driven over RPC for testing. A bare name reads `0:/BadUSB/<name>`; a path with
  `/` is used as-is; remote filename defaults to the basename.
- RPC `CLI_EXEC` command buffer raised 64 → 160 bytes so full paths fit.

## [0.3.7] - 2026-08-15

### Added
- **ESP-NOW remote trigger now sends the actual payload.** Previously only the
  file *name* crossed the link (the peer had to already own the script). Now the
  sender reads the DuckyScript, splits it into ≤160-byte fragments, and transfers
  them (AES-encrypted, per-fragment ACK) via `AT+M1ESPNOWSEND`; the receiver
  reassembles, writes `0:/BadUSB/<name>`, and runs it.

### Fixed
- **Received triggers never executed (no USB→HID switch).** Two causes: (1) the
  async `+ESPNOWRX` URC was flushed by the SPI-AT response channel before the M1
  polled it — replaced with a reliable, UID-matched `AT+M1ESPNOWRX?` query that
  drains a queued-trigger buffer on the ESP; (2) running BadUSB from the RPC/CLI
  task couldn't switch USB out from under its own CDC channel — the receiver now
  runs from the **ESP Link → Listen** screen (main UI task), where the HID switch
  works. `m1_esp_link_rx_poll()` reassembles + executes from that task.

## [0.3.6] - 2026-08-15

### Fixed
- **ESP-NOW remote trigger never actually fired the BadUSB on the receiver.**
  The trigger was delivered as an async `+ESPNOWRX` URC, but the SPI-AT response
  channel flushes stale/unsolicited data before each command and only returns
  UID-matched responses — so a one-shot trigger arriving between polls was
  discarded and `badusb_execute_file()` was never called (no USB→HID switch,
  "nothing happened"). Now the ESP **buffers** received triggers and the M1
  **polls `AT+M1ESPNOWRX?`** (a UID-matched query) to drain them reliably.
- **Trigger execution now runs in the UI/main task.** Added a **"Listen for
  triggers"** mode to the ESP Link app; `badusb_execute_file()` runs there, so
  the USB→HID switch works (the RPC CLI path can't switch USB out from under its
  own CDC channel). `m1_esp_link_rx_poll()` exposes one poll+dispatch cycle.

## [0.3.5] - 2026-08-15

### Added
- **ESP-NOW link Phase 3: on-device UI.** New "ESP Link" app under the Sub-GHz
  menu (`m1_esp_link_app.c`): brings ESP-NOW up, scans for peers (HELLO), shows
  a live peer list, and per peer offers **Trigger BadUSB** (browse `0:/BadUSB`,
  pick a script, send it encrypted) and **Pair**. `[Set passphrase]` enters the
  shared key via the on-screen keyboard; `[Rescan]` re-discovers. UI-facing
  `m1_esp_link_*` helpers added (scan-collect, enable/key/pair/trigger returning
  status).

## [0.3.4] - 2026-08-15

### Added
- **ESP-NOW link Phase 1+2: encryption + discovery/pairing.**
  - **App-layer AES-256-CBC** over the whole trigger payload, keyed by the
    shared passphrase (SHA-256 KDF on the ESP; ESP-NOW link-layer encryption
    stays off — no per-peer LMK pairing needed). A 2-byte plaintext magic
    detects a wrong/absent key on decrypt.
  - **Encrypted-only execution:** a keyed receiver drops unencrypted triggers,
    and drops encrypted ones that fail to decrypt (wrong passphrase); the M1
    additionally refuses to run any trigger reported as unencrypted.
  - **Pairing allowlist:** `espnow pair <mac>` — once any peer is paired, only
    listed MACs may trigger this unit. `espnow pair` (no arg) lists them.
    Discovery via `espnow scan` (HELLO beacons → `+ESPNOWPEER`).
  - New M1 CLI: `espnow pair`; `espnow key ''` now clears the key. Requires the
    matching T-800 ESP firmware (`AT+M1ESPNOWPAIR`, AES payload).

## [0.3.3] - 2026-08-15

### Fixed
- **ESP-NOW `espnow trig`/`key` sent unquoted string args, which the ESP32
  rejected.** ESP-AT requires string parameters to be double-quoted (like
  `AT+CWJAP="ssid","pass"`); the M1 sent the MAC and file name bare, so
  `esp_at_get_para_as_str` failed and the ESP returned `ERROR` before ever
  transmitting. Now quoted: `AT+M1ESPNOWTRIG="<mac>",<ptype>,"<name>"` and
  `AT+M1ESPNOWKEY="<pass>"`. **Verified on hardware: A→B trigger now completes
  the full DATA→ACK round-trip (`ack=1`) — Phase 0 transport gate passed.**

## [0.3.2] - 2026-08-15

### Fixed
- **ESP-NOW `espnow trig` mangled its MAC/name arguments.** The CLI handler
  passed the raw `FreeRTOS_CLIGetParameter` pointers (which are not
  null-terminated at the token boundary) straight into the AT command, so the
  MAC field swallowed the rest of the line and the ESP rejected the frame. The
  `trig` and `key` handlers now copy each parameter out by its returned length.

## [0.3.1] - 2026-08-15

### Added
- **M1 Link over ESP32 (ESP-NOW) — remote-trigger PoC (Phase 0 spike).** Second
  transport for the remote-trigger feature already shipped over the Si4463
  sub-GHz radio, now carried over the ESP32-C6's 2.4 GHz radio via ESP-NOW:
  - New `espnow` CLI command (`on/off/info/key/scan/trig/listen`) and module
    `m1_csrc/m1_esp_link.c`, driving the ESP's `AT+M1ESPNOW*` commands over SPI
    and parsing the `+ESPNOWRX:` / `+ESPNOWPEER:` events.
  - Receiver runs an inbound BADUSB trigger via `badusb_execute_file()`.
  - Requires the companion T-800 (ESP32-C6) firmware with the `AT+M1ESPNOW*`
    command family. Design: `esp32-at-monstatek-m1/docs/ESPNOW_LINK_DESIGN.md`.
  - Host bring-up tool `tools/m1rpc.py` (binary-RPC CLI_EXEC client).
  - Gated by `M1_APP_ESPNOW_LINK_ENABLE`.

### Changed
- Version bumped to **0.3.1**.

## [0.3.0] - 2026-08-12

### Added
- **M1 Link — device-to-device wireless between M1 units over the Si4463
  sub-GHz radio (915 MHz FSK packet mode).** A full messaging + sharing stack,
  reachable from the Sub-GHz menu as "M1 Link":
  - **Reliable transport:** framed packets (proto/ver, type, 16-bit src/dst
    short IDs derived from the STM32 UID, seq), hardware CRC-16, and
    stop-and-wait ARQ (per-fragment ACK, retry, backoff). Messages up to
    240 bytes are fragmented and reassembled; each fragment is acknowledged.
  - **Discovery:** HELLO beacons build a live nearby-peer list (name + RSSI),
    so peers are found without typing hex IDs.
  - **Encryption (optional):** AES-256-CBC over the whole message, keyed by a
    shared passphrase (salted-MD5 KDF). Units with the wrong/no passphrase
    silently drop traffic. Applies to messages and file transfers alike.
  - **On-device Chat UI:** peer list → actions (Message / Ping / Locate /
    Send File), conversation view, compose via the full keyboard, buzzer on
    receive.
  - **Ping / Locate:** ping reports round-trip time and the dBm at which the
    peer heard you; locate makes a peer beep + flash its LED to find it.
  - **Remote trigger:** send one of YOUR payloads to a paired peer and run it
    there — sub-GHz `.sub` replay, **BadUSB** / **Bad-BT** DuckyScripts, or an
    `.ir` file. The file is transferred into the payload's folder on the peer
    (`0:/SUBGHZ`, `0:/BadUSB`, `0:/IR`) and executed when the transfer
    completes. **Runs only when the transfer arrives encrypted** (shared
    passphrase), and after the final ACK so it can't double-fire. Peer →
    actions → Trigger → type → pick a file.
  - **Capture sharing:** send a saved `.sub` / `.nfc` / `.ir` file to a peer
    (chunked, ACKed, encrypted if a passphrase is set). Received files are
    routed by extension into `0:/SUBGHZ`, `0:/NFC`, `0:/IR`.
  - **Settings** (persisted to `0:/M1LINK.CFG`): callsign, passphrase,
    channel, TX power.
  - **Channels** (0–9) — each is its own absolute frequency 1 MHz apart
    (915–924 MHz), far wider than the RX bandwidth, so channels are genuinely
    isolated "rooms": same channel to talk, different channels can't hear each
    other.
  - **Listen-before-talk** collision avoidance before each transmit.
  - CLI (debug console): `link info|beacon|listen|rxdiag|id|send|recv|scan|
    ping|locate|sendfile|trigger|key|chan|name|save`.
  - Design notes: `documentation/m1_link_design.md`; plan/tracker: `plan.md`.

### Fixed
- **Sub-GHz record could only capture once per boot.** The record view's destroy
  handler never freed the capture ring buffers allocated by
  `sub_ghz_ring_buffers_init()`, so they leaked on every record session; the
  next record failed to `malloc` ("MEM_ERROR") until a reboot. The buffers are
  now freed when the record view is destroyed. (Pre-existing bug, unrelated to
  M1 Link.)

### Changed
- Version bumped to **0.3.0** for the M1 Link feature.

## [0.2.3] - 2026-08-11

### Added
- **Infrared → Custom Remotes: build, learn, replay, and edit custom IR
  remotes on-device.** User remotes are standard Flipper `.ir` files at
  `0:/IR/*.ir`:
  - Create a named, empty remote via the on-screen keyboard (name sanitized to
    a FAT-legal filename and de-duplicated).
  - Learn buttons from the IR receiver. IRMP-decodable signals are stored as
    parsed buttons; signals IRMP cannot decode fall back to a **raw** capture
    (mark/space edge stream), so undecodable remotes can still be saved. Raw
    captures are finalized on the inter-frame gap and noise-filtered.
  - Replay through the shared button-list engine (parsed and raw both transmit).
  - Edit buttons: **rename** and **delete** (delete is confirmation-gated), via
    an atomic read-all → temp → rename rewrite that preserves every other
    button.
  - See `documentation/custom_remotes.md`.
- **New Universal Remote categories:** `Bluray/` (Sony, Samsung), `Monitor/`
  (universal power), `LEDs/` (24-key RGB strip), and `Streaming/` (Apple TV,
  Roku) `.ir` sets under `ir_database/`.
- **Host `.ir` tooling:** `tools/host_test/validate_ir` plus a `validate.sh`
  regression sweep (round-trip unit suite + validation of every shipped `.ir`).

### Changed
- Bumped the T-1000 firmware version to `0.2.3`.

## [0.2.1] - 2026-06-29

### Fixed
- **Post-flash blank-screen boot path** — firmware update and bank-swap resets
  now keep a visible reboot handoff screen up, put GPIO-controlled peripherals
  into the same known state as Power -> Reboot, and force a bounded software
  reset if the option-byte reload reset does not fire promptly. This should
  prevent stock/non-RGB screen units from sitting blank after flashing release
  firmware.
- Added a small boot-recovery breadcrumb for bank-swap/CRC fallback paths so
  serial diagnostics can show whether early boot swapped banks, fell back to
  DFU, or had to force a reset after option-byte launch returned.

### Changed
- Bumped the T-1000 firmware version to `0.2.1`.

## [0.2.0] - 2026-06-22

### Added
- **NFC → Detect Reader: on-device MIFARE Classic key recovery (mfkey32v2)** —
  after capturing reader authentication nonces, the M1 now recovers the sector
  key **on the device itself**, no PC tool required. It pairs two captured
  auth attempts for the same sector/key type and runs a memory-bounded
  Crypto-1 (Crapto-1) attack, showing a `Cracking N/16` progress screen with
  BACK to cancel. The recovered key is displayed and saved to
  `NFC/keys_<UID>.txt`, and also exported as a Proxmark-ready dictionary
  `NFC/keys_<UID>.dic` (bare 12-hex key per line) for the M1+Proxmark workflow
  (`hf mf fchk -f …`). The raw `NFC/mfkey_nonces.txt` dump is still written as
  a fallback for desktop tools. Recovery takes roughly 2–3 minutes on-device
  and the recovered key is cryptographically self-verified before it is shown.

- **RFID → Diagnostics screen** — on-screen self-diagnostics (ported from
  da-pingwing's `m1_diag`): shows the last reset cause (BOR/IWDG/SFT/POR) and
  the T5577 write phase reached, stored in `.noinit` RAM so it survives a
  brownout/watchdog/fault reset. Lets a silent reset during an RFID write be
  diagnosed on the device, no serial adapter needed.

### Fixed
- **LF RFID → T5577 write produced a wrong (but valid) clone.** The T5577 bit
  timing was out of spec: a "1" bit's gap-to-gap was 74 field clocks vs the
  64 Tc datasheet maximum, so the chip latched it wrong and the cloned EM4100
  read back as a consistent incorrect value. Retuned to in-spec timing (24/56
  Tc) and switched the field gap to actively drive the coil pin low (no DC
  short / brownout). Fix courtesy of **da-pingwing** — see Credits.
- **RTC persistence** — the RTC calendar is no longer reset during firmware
  flashes, resets, or standby/power-cycle paths while the backup domain remains
  powered. Boot now keeps an already-valid calendar and records RTC
  initialization in a high backup register away from firmware update state.
- **LF RFID → Read stuck on "Reading" / no detection** — `lfrfid_read_hw_deinit()`
  could touch TIM3/TIM5 registers while their RCC clocks were gated, raising a
  bus fault → HardFault (which the fault handler spins on, freezing the read
  screen). Now enables those timer clocks defensively before access. Fix
  courtesy of **da-pingwing** — see Credits.
- **On-device MIFARE Crypto-1 cipher corrected.** The software Crypto-1 in
  `mfc_crypto1.c` used non-standard filter tables and PRNG, so authenticated
  reads never worked. Replaced the core (filter/LFSR/init/PRNG) with the
  canonical Crapto-1, host-validated by a round-trip against the mfkey32
  recovery. (Authenticated read/write still needs the encrypted-parity framing,
  tracked separately.)
- **Idle responsiveness on battery** — tickless idle suspended the HAL tick
  (TIM6) without correcting it on wake, so `HAL_GetTick()` froze during sleep
  and UI/timeout pacing crawled when the device idled unplugged. The HAL tick is
  now left running through idle sleep, keeping it accurate.
- **ESP32-C6 SPI link re-init leak** — re-initializing the SPI AT layer (after a
  C6 firmware update or a forced re-init) leaked a duplicate SPI control task
  plus its queues/buffers each time; the RTOS objects/task are now created once.

### Changed
- Bumped the T-1000 firmware version to `0.2.0`.
- **Flash savings (~59 KB)** — switched the FatFs OEM code page from 932
  (Japanese Shift-JIS) to 437 (US), dropping two large Unicode conversion
  tables. ASCII/Latin long filenames are unaffected.
- Updated the built-in M1/T-1000 logo bitmaps across the splash/menu sizes.

### Credits
- **da-pingwing** (github.com/da-pingwing/M1_T-1000_RFID, "Monstatek M1 RFID
  Patch", GPL-3.0) — diagnosis and fix for the T5577 write timing, and the
  `m1_diag` reset-cause / write-phase diagnostics.
- **noproto/FlipperMfkey** (GPLv3) — the memory-bounded Crapto-1 recovery the
  on-device mfkey32 solver is ported from.

### Notes
- The mfkey32 solver is a self-contained port of the memory-bounded Crapto-1
  recovery (noproto/FlipperMfkey, GPLv3), running in a fixed ~110 KB working
  area with no heap use. It is independent of the existing software Crypto-1
  cipher used for card auth/read.

## [0.1.5] - 2026-06-11

### Added
- **System -> ESP32 update -> Verify Image** — validates a selected ESP32-C6
  firmware image before flashing by checking the `.bin` type, same-folder
  `.bin.md5` companion file, image size/alignment, and computed MD5.

### Changed
- ESP32-C6 firmware flashing now requires the expected `.bin` + `.bin.md5` pair
  on SD card. The `.md5` file may be raw 32-character hex or standard md5sum
  format, and must match the selected image before flashing proceeds. Legacy
  `name.md5` companions are still accepted as a fallback.
- ESP32-C6 firmware image filenames and `.bin`/`.md5` extension casing are now
  accepted case-insensitively.
- `WiFi 2.4G -> Stats` now falls back to standard ESP AT mode/IP/MAC queries
  when the detailed custom ESP32-C6 stats command is unavailable, and displays
  idle zero-value link fields as unavailable.
- The ESP32 Link diagnostics app now uses the same WiFi stats fallback and
  zero-value normalization as the main WiFi stats screen.
- The File Tools app now has a self-contained Card Info panel with filesystem,
  total/free capacity, cluster/sector size, volume label, and refresh support.
- The Hex Viewer app now shows page/total position, handles empty files more
  clearly, and clamps page/row navigation safely at EOF.
- The System Dashboard app now includes a heap/watchdog health page and more
  readable SD free-space formatting.
- The Clock app now labels comparison pages as local-time offsets, supports
  half-hour offsets, and carries weekday/date rollover into offset views.
- The Dab Timer app now tracks completed sessions while keeping the adjustable
  countdown, pause, and alert flow.
- The DVD Logo app now has a reset control alongside speed/trail controls and
  bounce/corner stats.
- The Stock Backlight app now saves changed LP5814 brightness settings when
  exiting the app.
- The RGB Backlight app now shows a compact live on/brightness/reactive state
  above the control rows.
- The ESP32 Link diagnostics app now records how old the last result is on the
  status page.
- The File Tools app now shows SD free-space percentage in the menu and Card
  Info panel.
- The Hex Viewer ASCII preview now treats only printable 7-bit ASCII as text,
  keeping binary/high-bit bytes displayed safely as dots.
- FatFs OEM code page switched from 932 (Japanese Shift-JIS) to 437 (US),
  dropping the two ~30 KB Unicode conversion tables and freeing ~59 KB of
  flash (bank usage down from ~79% to ~71.5%). Long filenames are unaffected
  for ASCII/Latin names; only 8.3 short-name conversion of Japanese-named SD
  files is lost.
- Bumped the T-1000 firmware version to `0.1.5`.

### Fixed
- **Idle power draw** — the CPU now actually sleeps when idle. Tickless idle
  was enabled (`configUSE_TICKLESS_IDLE == 2`) but the live
  `vPortSuppressTicksAndSleep()` was an empty stub (the real one in
  `m1_low_power.c` was compiled out by `M1_MYTICKLESS_USE_RTC`), so the core
  busy-spun at full speed whenever the system was idle. Now true tickless idle
  (`configUSE_TICKLESS_IDLE == 1`) uses the FreeRTOS CM33 port implementation:
  the RTOS tick is suppressed for the whole expected idle period (up to ~223 ms
  per `WFI`) and the TIM6 HAL timebase is paused around the sleep so it cannot
  wake the core every 1 ms. ISR latency is unaffected.
- **ESP32 link re-init leak** — re-initializing the SPI AT layer after an
  ESP32-C6 firmware update (or a forced re-init from the ESP32 Link app)
  leaked a duplicate SPI control task plus its queues, semaphores, and
  buffers every time. The RTOS objects and task are now created once and
  re-init only flushes the stale session state and re-syncs the slave.
- **ESP32-C6 idle power-off** — after leaving a WiFi/BT/802.15.4 feature the
  C6 used to stay fully powered forever. It now powers off automatically after
  60 s of no use; re-entry within the window is still instant, and after a
  power-off the next feature entry transparently waits for the C6 to boot and
  answer `AT` again (no more racing the boot). Power is never cut while an
  ESP32 firmware update is in progress.

## [0.1.4] - 2026-06-10

### Added
- **WiFi → Offensive Tools → Deauth All** — scans for nearby APs, then broadcast-
  deauthenticates every one of them with channel hopping; start/stop, no target entry
- **WiFi → Offensive Tools → Evil Twin** — open rogue AP with a DNS-hijack captive
  portal; editable SSID and channel, start/stop
- **Bluetooth → BLE Spam** — floods Apple / Google / Microsoft "device nearby"
  advertisements (vendor selectable: All / Apple / Google / Microsoft)
- **Animated main-menu logo** — the M1 owl idly bounces DVD-screensaver style
  inside its panel, pauses for a beat, then slides off the left edge and returns
  on a loop. Purely cosmetic: it animates only while the menu sits idle, and any
  keypress instantly restores the static menu
- **Sub-GHz → RSSI Meter scrolling graph** — upgraded the RSSI Meter with a 128-sample
  rolling history timeline graph showing signal strength over time alongside the
  live numeric decibel/bar display
- **Sub-GHz → Spectrum Analyzer Peak Hold** — added a persistent peak hold (max hold)
  trace (drawn as single dots above the live scan bars) that records and retains the
  maximum signal level seen on each frequency; automatically resets when center frequency,
  span, or band changes
- **GPIO → USB-UART Bridge** — transparent VCP-to-UART bridge routing USB CDC data
  directly to USART1 (Pins 12/TX and 13/RX) at host-selected baud rates; automatically
  provides 3.3V target power on Pin 9 and displays real-time TX/RX traffic counters
- **GPIO → Pin Map** — graphical dual-column pin header layout displaying real-time
  logic states (HIGH/LOW) and supporting on-the-fly pin mode configuration (Pull-Up,
  Pull-Down, Floating)

### Fixed
- **Bluetooth reliability** — Bad-BT, Bluetooth Advertise, and BT Info no longer
  freeze or fail when first opened. The ESP32-C6 bring-up now waits for the
  coprocessor to actually answer `AT` after a reset instead of a blind 200 ms delay,
  so commands no longer race the C6's boot (added a readiness handshake in
  `esp32_main_init()` and after `AT+RST`)
- **BT Info** firmware-version readout is more robust to the multi-line `AT+GMR`
  reply splitting across SPI transfers

### Notes
- The new WiFi/BLE attacks require matching ESP32-C6 AT firmware
  (`AT+M1DEAUTHALL`, `AT+M1EVILTWIN`, `AT+M1BLESPAM`). Reflash the C6 factory image
  if these report "unknown command".

## [0.1.3] - 2026-06-09

### Added
- **RGB Backlight mod support** (SK6805) with a full control menu:
  - Color modes, animations (Static, Breathe, Color Cycle, Strobe, Fade) and brightness
  - **Custom RGB color editor** — set an exact R/G/B color, saved to SD
  - **Reactive lighting** — drives the RGB mod from live system state: battery-level
    color (green → amber → red), a pulse while charging, and a flash on notifications
  - Settings persist and restore on boot from either entry point
- **IR Universal Power-Off (TV-B-Gone)** — blast every TV power code from
  `IR/TV/Universal_Power.ir` to switch off nearby televisions, with progress and abort
- **IR Power Off A/V** — same blaster extended to soundbars, receivers, and projectors
- **NFC NDEF Writer** — write a URL or Text record to an NTAG/Type-2 tag (NFC Tools menu)
- **NFC recovered-key report** — after a MIFARE Classic dictionary read, view the key
  (A/B) recovered for each sector on screen and save it to `NFC/<UID>_keys.txt`

### Fixed
- **Power Off / Reboot screen** device icon no longer overflows the frame or the caption
- **Main menu icons** resized so they sit inside their rows (no border bleed / clipping)
- **"M1" panel label** no longer sits on the box border
- **RGB Backlight menu** label and brightness-readout inconsistencies; "Color Cycle"
  now animates on solid colors

## [0.1.2]

### Fixed
- Infrared transmit fix
- Restored missing firmware functions

## [0.1.1] - 2026-04-17

### Added
- **WiFi Offensive Tools** menu with 6 functions:
  - Deauth Flood
  - PMKID Capture  
  - Handshake Capture
  - Beacon Spam
  - Karma Attack
  - Probe Sniff
- **Attack List** to save scan targets for reuse
- **Virtual keyboard** with MAC address formatting (`AABBCCDDEEFF` → `AA:BB:CC:DD:EE:FF`)
- **Maximum power NFC carrier** (40% modulation - ST25R3916 hardware maximum)
- **Long duration NFC tests** (up to 60 seconds)

### Fixed
- **WiFi Offensive Tools menu crash** when opening
- **Attack List deauth failure** 
- **Virtual keyboard cursor** not moving when typing
- **MAC address input** without colon key on keyboard
- **NFC false positives** in scan results

### Improved
- **WiFi reliability** with ESP32 readiness checks
- **SPI communication** with automatic retry logic
- **NFC signal strength** at maximum hardware capability
- **Menu stability** across all functions

## [0.1.0] - Initial Release

Base firmware with:
- Sub-GHz radio with 30+ protocol decoders
- NFC/RFID reader/writer
- Infrared remote control
- BadUSB/Bad-BT
- WiFi scanning and connection
- External app loader
- Games and utilities
- Dual boot system
