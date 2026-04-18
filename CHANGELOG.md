# Changelog

All notable changes to the M1 T-1000 firmware will be documented in this file.

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