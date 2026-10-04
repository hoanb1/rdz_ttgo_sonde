rdzTTGOsonde (hoan.uk Edition)
==============================

> **Multi-Protocol Radiosonde Ground Station & IoT Telemetry Gateway**  
> **Architecture:** Conforming to IEEE Std 1016-2009 (Software Design Descriptions - SDD)  
> **Latest Release:** `v1.3.3-hoanuk` | **Dual Target:** ESP32-S3 (Heltec V3) & ESP32 (TTGO v2.1)

This is an enterprise-grade multi-protocol ground station receiver for radiosondes (Vaisala RS41, RS92, Graw DFM, Meteomodem M10/M20, Meteo-Labor MP3H) and IoT telemetry trackers, optimized for the `hoan.uk` platform.

## Architecture: Spec-Driven & Schema-Driven Development (SDD)

This project strictly adheres to **Clean 4-Layer SDD Architecture** conforming to **IEEE Std 1016-2009**:
- **Full Architecture Specification**: See [docs/SDD.md](docs/SDD.md) for complete C4 models, subblock decoders, and radio HAL.
- **Single Source of Truth (SSOT) Schemas**:
  - [`schemas/CompactTelemetryV1.json`](schemas/CompactTelemetryV1.json): Ultra-compact 11 to 24-byte binary telemetry protocol.
  - [`schemas/Rs41RawForward.json`](schemas/Rs41RawForward.json): Raw descrambled RS41 frame hex forwarding on decode error for cloud Reed-Solomon recovery.
  - [`schemas/QrgChannelConfig.json`](schemas/QrgChannelConfig.json): Channel configuration with type character (`typech`) preservation.
- **Release History**: See [CHANGELOG.md](CHANGELOG.md) for detailed version history.

## Supported Outputs & Telemetry Egress

- **hoan.uk Platform**:
  - Ultra-compact binary telemetry (11–24 bytes via `connHoanUK`, saving 90% bandwidth).
  - Cloud Reed-Solomon raw packet forwarding fallback (`rs41_raw_forward`).
- **SondeHub v2**: Real-time atmospheric sounding crowdsource tracking.
- **APRS-IS**: AX.25 packet forwarding for amateur radio mapping.
- **MQTT**: Direct JSON publishing to enterprise message brokers.
- **AXUDP**: For `aprsmap` and local decoders.
- **KISS TNC**: For APRSdroid and mobile trackers.
- **Embedded Web UI & In-App OTA**: Real-time status at `/live.json`, QRG channel editor at `/qrg.json`, and direct OTA updating via `hoan.uk/firmware/rdz/`.

## Supported Hardware Platforms

| Board | Microcontroller | RF Transceiver | Frequency Range | PlatformIO Target |
| :--- | :--- | :--- | :--- | :--- |
| **Heltec WiFi LoRa 32 V3** | ESP32-S3 (Dual-Core 240MHz, 8MB Flash) | Semtech SX1262 (SPI) | 400–438 MHz | `heltec-lora32-v3` |
| **LilyGO TTGO LoRa32 v2.1** | ESP32 (Dual-Core 240MHz, 4MB Flash) | Semtech SX1278 (SPI) | 400–438 MHz | `ttgo-lora32` |
| **LilyGO T-Beam** | ESP32 + GNSS | Semtech SX1278 (SPI) | 400–438 MHz | `ttgo-t-beam` |

## Quick Start (Building & Flashing)

```bash
# Build firmware for Heltec WiFi LoRa 32 V3 (ESP32-S3)
pio run -e heltec-lora32-v3

# Flash Heltec WiFi LoRa 32 V3 via USB
pio run -e heltec-lora32-v3 -t upload --upload-port /dev/ttyUSB0

# Build firmware for TTGO LoRa32 v2.1 (ESP32)
pio run -e ttgo-lora32

# Flash TTGO LoRa32 v2.1 via USB
pio run -e ttgo-lora32 -t upload --upload-port /dev/ttyUSB0
```



### Radiosonde Support Matrix

Manufacturer | Model | Position | Temperature | Humidity | Pressure
-------------|-------|----------|-------------|----------|----------
Vaisala | RS92-SGP | :heavy_check_mark: | :heavy_check_mark: | :x: | :x:
Vaisala | RS41-SG/SGP/SGM | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: (for -SGP)
Graw | DFM06/09/17 | :heavy_check_mark: | :heavy_check_mark: | :x: | :x:
Meteomodem | M10 | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | Not Sent
Meteomodem | M20 | :heavy_check_mark: | :x: | :x: | Not Sent
Meteo-Radiy | MP3-H1 (MRZ-H1) | :heavy_check_mark: | :x: | :x: | :x: 

SondeHub integration has mainly been tested with RS41 and DFM. 


### Radio Performance & Sensitivity

The firmware includes several optimizations for receiving weak radiosonde signals at long range:

- **LNA Boost (SX1278)**: Enables the Low-Noise Amplifier boost circuit for the 400-525 MHz band, improving sensitivity by ~3 dB on TTGO LoRa32 and T-Beam boards.
- **RxBoosted Gain (SX1262)**: Enabled by default on Heltec WiFi LoRa 32 V3 boards for improved sensitivity.
- **RSSI Smoothing (SX1278)**: 8-sample RSSI averaging for more stable preamble detection with weak signals.
- **Reed-Solomon ECC**: Full RS(255,231) error correction decoder (from dxlAPRS) can fix up to 12 symbol errors per codeword, recovering frames that would otherwise be lost.
- **Automatic Frequency Correction (SX1278)**: Hardware AFC automatically tracks frequency drift of the sonde transmitter.
- **Configurable RX Bandwidth**: Tune via `config.txt` (e.g., `rs41.rxbw=6300`) to balance sensitivity vs. frequency tolerance.

Support for other radiosondes that use AFSK modulation is not feasible with the TTGO hardware.
In particular, decoding iMet-1/iMet-4 radiosondes is not practical (iMet-5x seems to use FSK,
so should be feasible to implement).

Adding support for LMS6 (see issue #48) and ims100 (see branch ims100) could be feasible,
but currently I don't have plans to do add this myself. Well-tested pull requests will of
course be considered for inclusion :-).

## Installation

You can download the latest binary automated build for the development and testing branches [here](http://rdzsonde.org/download.html), the binary includes everything including configuration files so any existing settings will be reset. 

To update an existing installation to the latest development or master version you can use the [OTA](https://github.com/dl9rdz/rdz_ttgo_sonde/wiki/Other-features#over-the-air-updates) update feature.

The downloaded .bin file can be flashed to your ESP32 board using [esptool](https://github.com/espressif/esptool) or [ESP32 Download Tool](https://www.espressif.com/en/support/download/other-tools)

### esptool

You can run the following command replacing `<filename.bin>` with the path to the downloaded .bin file. 

If you encounter errors with the device COM not automatically being detected replace `/dev/cu.SLAB_USBtoUART` with `COM<X>`.

```
esptool --chip esp32 --port /dev/cu.SLAB_USBtoUART --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x1000 <filename.bin>
```

### ESP32 Download Tool

The binary file can also be installed using the GUI application with the [following](http://rdzsonde.mooo.com/) settings.

## Button commands

You can use the button on the board (not the reset button, the second one) to
issue some commands. The software distinguishes between several inputs:

- SHORT	Short button press (<1.5 seconds)
- DOUBLE  Short button press, followed by another button press within 0.5 seconds
- MID	Medium-length button press (2-4 seconds)
- LONG	Long button press (>5 seconds)

You can optionally use a second button, which you have to add manually to your board.
See https://github.com/dl9rdz/rdz_ttgo_sonde/wiki/Hardware-configuration for details.


## Wireless configuration

On startup, as well as after a LONG button press, the WiFI configuration will
be started.  The board will scan available WiFi networks, if the scan results
contains a WiFi network configured with ID and Password in networks.txt, it
will connect to that network in station mode. If no known network is found, or
the connection does not suceed after 5 seconds, it instead starts in access point
mode. In both cases, the ESP32's IP address will be shown in tiny letters in the
bottom line. Then the board will switch to scanning mode.

## Scanning mode

In the scanning mode, the board will iterate over all channels configured in
channels.txt, trying to decode a radio sonde on each channel for about 1 second.
If a valid signal is found, the board switches to receiving mode on that channel.
A SHORT buttong press will also switch to receiving mode.

## Receiving mode

In receiving mode, a single frequency will be decoded, and sonde info (ID, GPS
coordinates, RSSI) will be displayed. The bar above the IP address indicates,
for the last 18 frames, if reception was successfull (|) or failed (.), or had
some errors (E), e.g., CRC check failed.
 
A DOUBLE press will switch to scanning mode.

A SHORT press will switch to the next channel in channels.txt

A SHORT press on the second button will switch to a different display screen.

## Spectrum mode

A medium press will active scan the whole band (400..406 MHz) and display a
spectrum diagram (each line == 50 kHz)
For TTGO boards without configurable button there are some new parameter in config.txt:
- spectrum=10       // 0=off / 1-99 number of seconds to show spectrum after restart
- timer=1           // 0=off / 1= show spectrum countdown timer in spectrum display
- marker=1          // 0=off / 1= show channel edge freq in spectrum display

## Setup

see [Wiki](https://github.com/dl9rdz/rdz_ttgo_sonde/wiki/Installation)

