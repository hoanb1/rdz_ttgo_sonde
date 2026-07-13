# Changelog

All notable changes to the rdz_ttgo_sonde firmware are documented in this file.

---

## [1.2.0] - 2026-07-13

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
