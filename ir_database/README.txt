M1 IR Remote Database
=====================

Copy the contents of this directory to the SD card at 0:/IR/

The M1 will browse these directories and .ir files from:
  Infrared > Universal Remote

Directory Structure
-------------------
  IR/
  ├── TV/
  │   ├── Samsung.ir         Full Samsung TV remote
  │   ├── LG.ir              Full LG TV remote
  │   ├── Sony.ir            Full Sony TV remote
  │   ├── Philips.ir         Full Philips TV remote
  │   ├── Panasonic.ir       Full Panasonic TV remote
  │   ├── Vizio.ir           Vizio TV remote
  │   ├── TCL.ir             TCL TV remote
  │   ├── Hisense.ir         Hisense TV remote
  │   ├── Toshiba.ir         Toshiba TV remote
  │   ├── Sharp.ir           Sharp TV remote
  │   └── Universal_Power.ir Power codes for 40+ brands (brute force)
  ├── Audio/
  │   ├── Samsung_Soundbar.ir
  │   ├── Bose.ir
  │   ├── Denon_Receiver.ir
  │   └── Universal_Power.ir Power codes for 19 audio brands
  ├── Projector/
  │   └── Universal_Projector.ir  Power codes for 16 projector brands
  ├── Fan/
  │   └── Universal_Fan.ir   Power/speed for common fan brands
  ├── Bluray/
  │   ├── Sony_Bluray.ir     Sony Blu-ray transport (SIRC)
  │   └── Samsung_Bluray.ir  Samsung Blu-ray transport (Samsung32)
  ├── Monitor/
  │   └── Universal_Monitor.ir  Power codes for common monitor brands
  ├── LEDs/
  │   └── RGB_24key.ir       Common 24-key RGB LED strip remote (NEC)
  └── Streaming/
      ├── Apple_TV.ir        Apple TV remote (Apple protocol)
      └── Roku.ir            Roku streaming remote (NEC)

File Format
-----------
All files use the Flipper Zero .ir format (compatible with Flipper IRDB).
You can add your own .ir files or download more from the Flipper IRDB
community database.

Every shipped .ir file is checked with tools/host_test/validate_ir (valid
header + at least one parseable signal). The codes in the newer categories
(Bluray, Monitor, LEDs, Streaming) are hand-authored from published brand
tables and are format-valid; on-device transmit accuracy is bench-verified
per device.

The M1 can also learn new remotes using the IR receiver.
Learned remotes are saved to 0:/IR/Learned/
