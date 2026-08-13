<!-- See COPYING.txt for license details. -->

# M1 T-1000 Firmware

**T-1000** is a custom, feature-expanded firmware for the [Monstatek M1](https://monstatek.com) handheld multi-tool — Sub-GHz, NFC, RFID, Infrared, WiFi, Bluetooth, and BadUSB in one device — with its own UI, tooling, and hardware-mod support layered on top of the M1 platform.

> **This is an independent community project and is not affiliated with or endorsed by Monstatek.**

## What's New in v0.3.0

### Added
- **M1 Link — device-to-device wireless between M1 units** over the Si4463
  sub-GHz radio (915 MHz FSK). Reliable, acknowledged messaging with automatic
  peer discovery; optional AES-256 encryption keyed by a shared passphrase; 10
  genuinely-isolated channels; ping/locate; capture sharing (`.sub`/`.nfc`/`.ir`);
  and remote trigger (replay a peer's sub-GHz, IR, BadUSB, or Bad-BT payload).
  Found under **Sub-GHz → M1 Link** — see [M1 Link](#m1-link-device-to-device)
  below.

### Fixed
- **Sub-GHz record now works repeatedly** — previously a capture-buffer leak let
  you record only once per boot before a reboot was needed.

## Features

### Flipper Zero Compatibility
- Import and use Flipper Zero `.sub`, `.rfid`, `.nfc`, and `.ir` files directly
- Drop Flipper files onto the SD card and use them on the M1

### Sub-GHz Enhancements
- **30+ protocol decoders** — Princeton, CAME, Nice Flo, Keeloq, Security+ 2.0, Linear, Holtek, Hormann, Marantec, Somfy, and many more
- **Spectrum Analyzer** — visual RF spectrum display with a persistent peak hold (max hold) trace
- **RSSI Meter** — real-time signal strength with a 128-sample rolling history timeline graph
- **Frequency Scanner** — find active frequencies
- **Weather Station** — decode Oregon v2, Acurite, LaCrosse, Infactory sensors
- **Radio Settings** — adjustable TX power, custom frequency entry
- **Extended band support** — 150, 200, 250 MHz bands added

### NFC Enhancements
- **Tag Info** — manufacturer lookup, SAK decode, technology identification
- **T2T Page Dump** — read and display Type 2 Tag memory pages
- **Clone & Emulate** — copy and replay NFC tags
- **NDEF Writer** — write a URL or text record to an NTAG/Type-2 tag
- **NFC Fuzzer** — protocol testing tool
- **MIFARE Classic Crypto1** support
- **Recovered-key report** — after a dictionary read, view the Key A/B recovered for each sector and save it to `NFC/<UID>_keys.txt`
- **MFKey Detect** — emulate a card, capture reader authentication nonces, and recover the MIFARE Classic sector key **on-device** (mfkey32v2 Crypto-1 attack, ~2–3 min, key saved to `NFC/keys_<UID>.txt` plus a Proxmark-ready `NFC/keys_<UID>.dic`); nonces are also dumped to SD for offline tools
- **Maximum Power Carrier** — 40% modulation (ST25R3916 hardware maximum)
- **Long Duration Tests** — carrier transmissions up to 60 seconds
- **False Positive Prevention** — validation checks in scan functions
- **Recovered-Key Report** — view recovered keys (A/B) on screen after dictionary scans and automatically save to `NFC/<UID>_keys.txt`

### RFID Enhancements
- **20+ protocol decoders** — HID Generic, Indala, AWID, Pyramid, Paradox, IOProx, FDX-A/B, Viking, Electra, Gallagher, Jablotron, PAC/Stanley, and more
- **Clone Card** — write to T5577 tags
- **Erase Tag** — reset T5577 to factory
- **T5577 Info** — read tag configuration
- **RFID Fuzzer** — protocol testing tool
- **Manchester decoder** with carrier auto-detection (ASK/PSK)

### Infrared
- **Universal Remote Database** — pre-built remotes for Samsung, LG, Sony, Vizio, Bose, Denon, and more (see [`ir_database/`](ir_database/))
- **Universal Power-Off (TV-B-Gone)** — blast known TV power codes to switch off any nearby television; a **Power Off A/V** action does the same for soundbars, receivers, and projectors
- **Learn & Save** — record IR signals and save to SD card
- **Import** Flipper Zero `.ir` files
- **Universal Power-Off (TV-B-Gone)** — blast TV power codes to turn off nearby televisions with progress display and abort support

### BadUSB
- **DuckyScript interpreter** — run keystroke injection scripts from SD card
- Supports `STRING`, `DELAY`, `GUI`, `CTRL`, `ALT`, `SHIFT`, key combos, and `REPEAT`
- Place `.txt` scripts in `BadUSB/` on the SD card

### Bad-BT (Bluetooth)
- **Wireless DuckyScript** — same scripting as BadUSB but over Bluetooth HID
- Pairs with target device wirelessly, no cable needed

> **Note:** Bad-BT is under active development and may not work reliably on all target devices. Bluetooth pairing and keystroke delivery depend on the target's BLE HID support.

### M1 Link (device-to-device)
Wireless link between two (or more) M1 units over the Si4463 sub-GHz radio
(915 MHz FSK) — no PC or phone in the middle. Found under **Sub-GHz → M1 Link**.
- **Messaging** — reliable, acknowledged text between units, with automatic peer
  discovery (a live nearby list of names + signal strength, so no typing IDs)
- **Encryption** — optional AES-256 keyed by a shared passphrase; units without
  the matching passphrase can't read the traffic (messages *and* file transfers)
- **Channels** — 10 genuinely-isolated channels (each on its own frequency); put
  a group on the same channel to talk, or a different channel for privacy
- **Ping / Locate** — measure round-trip time and link quality to a peer, or make
  a peer beep and flash its LED to physically find it
- **Capture sharing** — send a saved `.sub` / `.nfc` / `.ir` file to a peer; it
  lands in the right folder on their SD automatically
- **Remote trigger** — send one of your payloads to a *paired* peer and run it
  there: replay a sub-GHz `.sub`, transmit an `.ir` button, or fire a BadUSB /
  Bad-BT script (only honored when encrypted, so it can't be abused)
- Per-unit callsign and TX power, all saved to the SD card

> **Note:** M1 Link is new and under active testing. Both units must be on the
> same channel to communicate, and remote trigger requires the same passphrase
> on both.

### External Apps
- **ELF app loader** — load and run third-party apps from SD card
- Browse and launch `.m1app` files from the Apps menu
- **File Tools** — manage SD files, mount/unmount the card, and view in-app
  card info with filesystem, capacity, free space, cluster size, and label
- **Hex Viewer** — inspect files with paged hex rows, ASCII preview, clear
  page/total position, safe EOF clamping, and binary-safe printable ASCII
- **System Dashboard** — live overview for battery, SD, USB, ESP32 link,
  firmware version, heap health, and watchdog reset state
- **Clock** — local time plus relative offset pages with date rollover and
  half-hour offset support
- **Dab Timer** — adjustable countdown with pause, alert beeps, and a completed
  session counter
- **DVD Logo** — bouncing logo toy with speed, trail, pause/reset controls, and
  bounce/corner stats
- **Stock Backlight** — stock LP5814 on/off and brightness control with saved
  brightness changes
- **RGB Backlight** — SK6805 mod control with live on/brightness/reactive state,
  presets, effects, custom RGB editing, and saved settings
- Download ready-to-use apps and the App SDK at **[m1-sdk](https://github.com/bedge117/m1-sdk)**

### Games
- Snake, Tetris, T-Rex Runner, Pong, Dice — built-in games accessible from the menu

### WiFi
- **Scan** — discover nearby 2.4 GHz access points
- **2.4G Survey** — summarize nearby AP count, strongest signal, and busiest channel
- **Connect** — join 2.4 GHz networks with password entry
- **Saved Networks** — manage stored WiFi credentials
- **Sync RTC** — sync the device clock over WiFi SNTP; the RTC is now
  preserved across firmware flashes, resets, and standby/power-cycle paths
  while the backup domain remains powered
- **Status** — view connection state, IP address, signal strength
- **Attack List** — save targets for offensive tools
- **Offensive Tools** — WiFi penetration testing utilities:
  - **Deauth Flood** — send deauthentication frames to target AP
  - **Deauth All** — scan, then broadcast-deauth every nearby AP with channel hopping
  - **PMKID Capture** — capture WPA2/WPA3 PMKID hashes
  - **Handshake Capture** — capture WPA2/WPA3 handshakes
  - **Beacon Spam** — broadcast fake AP beacons
  - **Karma Attack** — respond to all probe requests
  - **Probe Sniff** — capture client probe requests
  - **Evil Twin** — open rogue AP with a DNS-hijack captive portal (editable SSID/channel)
- **ESP32-C6 coprocessor** provides 2.4 GHz WiFi (WiFi 6) and Bluetooth LE 5.0
- **WiFi stats fallback** — `WiFi 2.4G -> Stats` shows detailed ESP32-C6 link
  stats when custom firmware supports it, or basic mode/IP/MAC data through
  standard AT commands; idle zero-value fields are shown as unavailable, and
  the ESP32 Link diagnostics app uses the same fallback
- **ESP32 firmware verifier** — validate a selected ESP32-C6 `.bin` and matching
  32-byte `.bin.md5` companion file from `System -> ESP32 update` before
  flashing; standard md5sum-style files and filename/extension casing are accepted

### GPIO Enhancements
- **Pin Map** — graphical dual-column pin header layout displaying real-time logic states (HIGH/LOW) with on-the-fly pin mode configuration (Pull-Up, Pull-Down, Floating)
- **USB-UART Bridge** — transparent VCP-to-UART serial forwarding between USB CDC and USART1 (Pins 12/TX and 13/RX) at host-selected baud rates with automatic 3.3V target power on Pin 9 and real-time TX/RX traffic counters

### NFC/RFID Field Detector
- Detect external 13.56 MHz NFC reader fields and ~125 kHz RFID reader fields
- Useful for identifying hidden readers

### Bluetooth Device Manager
- Scan, save, and manage BLE devices
- View device info and connection details
- **BLE Spam** — flood Apple / Google / Microsoft "device nearby" advertisements
  (vendor selectable: All / Apple / Google / Microsoft)

### Dual Boot
- Two firmware banks with safe boot validation
- Swap between banks from the menu or via the companion app
- CRC verification before boot — falls back to working bank on corruption

### RGB Backlight
- **RGB mod support** (SK6805) — drives an add-on RGB LED backlight behind the screen
- **Color, animation, and brightness** control — Static, Breathe, Color Cycle, Strobe, and Fade effects
- **Custom color editor** — dial in an exact R/G/B color, saved to SD
- **Reactive lighting** — color follows live system state: battery level (green → amber → red), a pulse while charging, and a flash on notifications
- **Stock backlight** control for the standard LP5814 white backlight

### Other Improvements
- **RPC protocol** for [M1 T-800](https://github.com/dagnazty/M1-T-800) companion app communication
- **Settings persistence** — LCD brightness, southpaw mode, preferences saved to SD card
- **Southpaw mode** — swap left/right button functions
- **Safe NMI handler** — proper ECC fault recovery instead of hard fault
- **Watchdog improvements** — task-level suspend/resume for long operations
- **Virtual Keyboard** with MAC address formatting and colon-skipping
- **ESP32 Readiness Checks** — ensure WiFi coprocessor is ready before sending commands
- **SPI Retry Logic** — automatic retry for transient SPI communication failures
- **Attack List Integration** — auto-fill BSSID/channel in offensive tools
- **RGB Backlight Mod (SK6805)** — control menu with brightness, color presets, custom RGB editor, and effects (Breathe, Color Cycle, Strobe, Fade)
- **Reactive Backlight** — drives the RGB mod from live system state: battery level (green/amber/red), charging pulse (blue), and notification flash (white)
- **Animated main-menu logo** — the updated T-1000 logo idly bounces DVD-screensaver style in its panel, then slides off and returns on a loop; cosmetic only, any keypress restores the static menu
- **Idle power savings** — the CPU light-sleeps (`WFI`) when idle instead of busy-spinning, while keeping the millisecond timebase running so UI and timeouts stay responsive on battery; the ESP32-C6 coprocessor also powers off automatically after 60 s of no WiFi/BT use, extending battery life
- **Slimmer firmware image** — dropped unused FatFs Japanese code-page tables (~59 KB), keeping plenty of headroom in the 1023 KB firmware bank

## Companion App

**[M1 T-800](https://github.com/dagnazty/M1-T-800)** — Desktop companion app for Windows. Connect your M1 via USB to:

- View device info, battery status, firmware version
- Flash firmware updates over USB
- Flash via DFU mode (works with stock firmware)
- Mirror the M1's screen on your PC
- Browse and manage SD card files
- Manage WiFi networks
- Update the ESP32 coprocessor firmware

Download the latest release from the [M1 T-800 releases page](https://github.com/dagnazty/M1-T-800/releases).

## IR Remote Database

The [`ir_database/`](ir_database/) directory contains pre-built infrared remote files for popular devices. Copy them to `IR/` on the M1's SD card to use with the Universal Remote feature.

Includes remotes for: Samsung, LG, Sony, Philips, Panasonic, Vizio, TCL, Hisense, Toshiba, Sharp, Bose, Denon, and universal power codes.

All files use the Flipper Zero `.ir` format — you can also use IR files from the [Flipper IRDB](https://github.com/Lucaslhm/Flipper-IRDB) community database.

## Hardware

- **MCU:** STM32H573VIT6 (Cortex-M33, 250 MHz, 2 MB dual-bank flash, 640 KB RAM)
- **Display:** 128x64 monochrome (ST7586s)
- **WiFi/BT:** ESP32-C6 coprocessor (SPI AT interface) — 2.4 GHz WiFi (WiFi 6) and Bluetooth LE 5.0
- **RF:** Si4463 sub-GHz transceiver (300–928 MHz)
- **NFC:** ST25R3916 (13.56 MHz) — supports up to 40% modulation for maximum signal strength
- **RFID:** 125 kHz ASK/PSK reader with T5577 write support
- **IR:** TSOP38238 receiver + IR LED transmitter
- **USB:** USB-C (CDC + MSC composite)
- **Storage:** microSD card
- **Hardware revision:** 2.x

## Building

### Prerequisites

- **STM32CubeIDE 1.17+** (recommended), or
- **ARM GCC 14.2+** with CMake and Ninja
- **Python 3** (for post-build CRC injection)

### Build with CMake

```bash
# One-command build
./build.sh
```

`build.sh` will try to auto-discover:
- a complete `arm-none-eabi-gcc` toolchain
- `cmake`
- `ninja`

On macOS it prefers STM32CubeIDE's bundled ARM toolchain over partial Homebrew
installs that are missing the embedded C library. You can still override the
compiler path explicitly:

```bash
export ARM_GCC_BIN=/path/to/arm-gnu-toolchain/bin
./build.sh
```

If you want to run CMake manually instead, use:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

If CMake finds `arm-none-eabi-gcc` but compilation immediately fails on
`stdint.h`, `string.h`, or `math.h`, the compiler was installed without the
Arm embedded C library/sysroot. On macOS this can happen with a partial
Homebrew toolchain. Point `ARM_GCC_BIN` at a complete Arm GNU toolchain that
includes the `arm-none-eabi` headers and libraries.

### Build with STM32CubeIDE

Open the project directory in STM32CubeIDE and build.

### Build with Make (Linux)

```bash
make
```

Output: `./artifacts/`

### GitHub Actions

The workflow at `.github/workflows/firmware-release.yml` builds the firmware on
pull requests and pushes, then uploads the contents of `artifacts/` as a GitHub
Actions artifact.

Pushing a version tag such as `v0.1.1` also creates a GitHub Release and
attaches the built firmware files, checksums, and a zip bundle. The pushed tag
must match `T1000_VERSION_*` in `m1_csrc/m1_t1000_version.h`.

You can also run the workflow manually from the Actions tab with
`workflow_dispatch`. If you enable `create_release`, the workflow will publish a
release for the current commit using the matching firmware version tag.

## Flashing

### Via M1 T-800 (recommended)
Connect via USB and use the Firmware Update page in [M1 T-800](https://github.com/dagnazty/M1-T-800).

### Via DFU Mode (recovery / first install)
1. Power off the M1 (Settings > Power > Power Off > Right Button)
2. Hold **Up + OK** for 5 seconds to enter DFU mode (screen stays dark)
3. Connect via USB-C
4. Use the DFU Flash page in [M1 T-800](https://github.com/dagnazty/M1-T-800)

To exit DFU mode without flashing, hold **Right + Back** to reboot.

### Via SWD
Use an ST-Link or J-Link debugger with STM32CubeIDE or OpenOCD.

## SD Card Layout

```
0:/
├── BadUSB/          DuckyScript .txt files
├── BT/              Saved Bluetooth devices
├── Flipper/         Imported Flipper Zero files
├── IR/              Infrared remote .ir files (see ir_database/)
│   └── Learned/     IR signals recorded by the M1
├── NFC/             NFC tag .nfc files and recovered keys
├── RFID/            RFID tag .rfid files
├── SUBGHZ/          Sub-GHz signal .sub files
├── System/          Settings (settings.cfg) and saved WiFi credentials
└── apps/            External .m1app applications
```

## Contributing

Contributions are welcome. Please open an issue or pull request.

If you're building a companion app or tool that communicates with the M1, the RPC protocol is implemented in `m1_csrc/m1_rpc.c` and `Core/Src/cli_app.c`.

## Acknowledgments

This project is a fork of and builds upon the work of:
- [bedge117/M1](https://github.com/bedge117/M1) - The upstream repository from which this firmware was directly forked.
- [Monstatek/M1](https://github.com/Monstatek/M1) - The original firmware repository from which the M1 platform originated.

We would like to express our gratitude to the original developers and contributors of both projects for their foundational work and contributions to the M1 platform.

We also gratefully incorporate work from:
- [da-pingwing/M1_T-1000_RFID](https://github.com/da-pingwing/M1_T-1000_RFID) ("Monstatek M1 RFID Patch", GPL-3.0) — the T5577 write-timing fix, the LF read TIM-clock HardFault fix, and the `m1_diag` on-device reset-cause / write-phase diagnostics.
- [noproto/FlipperMfkey](https://github.com/noproto/FlipperMfkey) (GPL-3.0) — the memory-bounded Crapto-1 recovery that the on-device mfkey32 solver is ported from.
- [Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware) (GPL-3.0) — the LF RFID protocol stack (`lfrfid/`) and NFC parsers.

## License

This project is licensed under the GNU General Public License v3.0 — see [COPYING.txt](COPYING.txt) for details.
