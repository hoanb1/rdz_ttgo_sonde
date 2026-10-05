# Changelog

All notable changes to the rdz_ttgo_sonde firmware are documented in this file.

---

## [1.3.4-hoanuk] - 2026-10-05

### Bug Fixes & Reliability
- **GPS Sanity & Plausibility Guard (`RS41.cpp`)**:
  - Refactored `posrs41()` to return explicit status codes (0 on success, -1 on failure).
  - Enforced strict physical sanity guards on velocity: reject packets with horizontal speed > 100 m/s (~360 km/h) or vertical speed > 100 m/s.
  - Linked `posok` directly to `posrs41()` success: frames with corrupt coordinates, all-zero ECEF, out-of-bounds, or kinematic jumps will no longer falsely set `posok = 1`.
  - Sanitized satellite count to physical limits (<= 36 sats) to eliminate corrupted 200+ sats spikes.
- **RSSI Normalization (`conn-hoanuk.cpp`)**:
  - Corrected raw RSSI conversion from uint8 attenuation register (`2 * -dBm`) to true negative dBm (`-(rssi / 2.0f)`), preventing false positive +214 dBm spikes.
  - Added strict regex validation for RS41 serial numbers in raw forward frames.

---

## [1.3.3-hoanuk] - 2026-10-04

### Architecture: Spec-Driven & Schema-Driven Development (SDD)
- Converted project to IEEE Std 1016-2009 Software Design Description (SDD) architecture with full documentation in `docs/SDD.md`.
- Defined formal JSON Schemas in `schemas/`:
  - `CompactTelemetryV1.json`: Ultra-compact binary telemetry wire format (11 to 24 bytes).
  - `Rs41RawForward.json`: Raw hex packet forwarding protocol for server-side Reed-Solomon recovery.
  - `QrgChannelConfig.json`: Channel directory and single-character type identifier (`typech`).

### Features & Enhancements
- **Compact Binary Telemetry**: Implemented high-efficiency 11–24 byte binary telemetry dispatcher in `conn-hoanuk.cpp` saving 90% mobile bandwidth.
- **RS41 Raw Packet Hex Forwarding**: Automatically forward descrambled raw 320-byte frames to `hoan.uk` cloud ingestion when local CRC fails, enabling server-side subblock recovery.
- **SX1262 GFSK Optimization**: Optimized frequency deviation to standard 2400 Hz for Vaisala radiosondes, and fixed long-packet receive timeout handling.
- **QRG Type Character Preservation**: Added `typech` preservation across web UI, `qrg.txt`, and `/qrg.json` API.
- **Webflasher Deployment Tooling**: Added `prefer_env` targeting to `scripts/deploy_to_webflasher.py` for automated OTA generation.

---


### Board: Heltec WiFi LoRa 32 V3 (SX1262 / ESP32-S3)

#### Enhanced
- Confirmed RxBoosted gain mode already enabled (register 0x08AC = 0x96) for optimal sensitivity.
- Rebuilt with latest codebase including all bug fixes from v1.1.0.

#### Notes
- SX1262 GFSK mode does not expose a frequency error register, so hardware-style AFC is not available on this chip.

---

## [1.1.0] - 2026-07-13

### Board: TTGO LoRa32 v2.1 (SX1278 / ESP32) & Heltec V3

#### Fixed
- **LNA Boost HF was disabled**: `setLNAGain()` wrote only bits[7:5] to REG_LNA (0x0C), zeroing bits[1:0] (LnaBoostHf). This silently disabled the LNA boost circuit for the 400-525 MHz radiosonde band, losing ~3 dB of sensitivity.

#### Enhanced
- **Enabled LNA Boost for HF band** (REG_LNA bits[1:0] = 0x03): Improves receiver sensitivity by ~3 dB at 400-525 MHz. Trade-off: ~2 mA extra current consumption.
- **RSSI Smoothing**: Set REG_RSSI_CONFIG to 8-sample averaging (bits[2:0] = 0x03) for more stable RSSI measurement with weak signals, improving preamble detection reliability at long range.
- Added `notes-on-bugs/Notes-SX1278-Sensitivity.txt` technical documentation.
- Added "Radio Performance & Sensitivity" section to README.

---

## [1.0.0] - 2026-07-08

### Board: All (Heltec V3, TTGO LoRa32, T-Beam)

#### Fixed
- **SX1262 Frequency Unit Mismatch**: The SX1278 FSK driver uses Hz directly, but the SX1262 implementation expected MHz internally, causing integer overflow and garbage frequencies on Heltec V3 boards.
- **SX1262 RX Bandwidth Selection**: Changed `getBandwidth()` from rounding-to-nearest to ceiling logic. 12600 Hz DSB now maps to 14600 Hz instead of 11700 Hz, providing safe margin for crystal frequency drift.
- **SX1262 Spectrum Scan Saturation**: Scanner sweep now correctly transitions through standby mode, sets frequency, re-enters RX continuous mode, and waits 5 ms for PLL lock and AGC settling.
- **Heltec V3 Welcome Screen Freeze**: Defaulted SD card pins (CS/MISO/MOSI/CLK) to -1 to avoid GPIO 0 boot pin collision.
- **Button ISR Deadlock**: Replaced direct `digitalRead()` calls in button ISRs with safe volatile flag pattern for Arduino ESP32 Core v3 compatibility.

#### Enhanced
- Reed-Solomon RS(255,231) error correction decoder active for RS41 and RS92 (from dxlAPRS).
- Full support for RS41, RS92, DFM06/09/17, M10/M20, and MP3H radiosondes.
