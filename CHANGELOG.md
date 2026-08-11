# Changelog

All notable changes to the M1 T-1000 firmware will be documented in this file.

## [Unreleased]

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
