# HydroSense ESP32-S3 Gateway

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Project-orange?logo=platformio)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/Board-ESP32--S3-blue?logo=espressif)](https://www.waveshare.com/esp32-s3-lcd-1.9.htm)
[![Framework](https://img.shields.io/badge/Framework-Arduino-00979D?logo=arduino)](https://www.arduino.cc/)
[![Language](https://img.shields.io/badge/Language-C%2B%2B-00599C?logo=cplusplus)](https://isocpp.org/)
![TinyML](https://img.shields.io/badge/TinyML-FP32-orange)
![Status](https://img.shields.io/badge/Status-Research%20Prototype-yellow)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](../LICENSE)

HydroSense ESP32-S3 is the gateway, visualization and edge-intelligence component of the HydroSense project.

It receives validated sensor telemetry from the HydroSense ESP32-C3 through Bluetooth Low Energy (BLE), calculates derived environmental features, estimates Total Dissolved Solids using an FP32 TinyML soft sensor, evaluates sensor anomalies and trust, displays the results on the onboard LCD, and sends realtime and historical telemetry to separate backend endpoints.

> **Important:** Estimated TDS is produced by a machine-learning soft sensor. It is not a direct physical TDS measurement.

## Project Position

The complete HydroSense project is designed around three major components:

1. HydroSense ESP32-C3 sensor node
2. HydroSense ESP32-S3 gateway and edge-AI node
3. HydroSense backend and website

This folder contains only the ESP32-S3 component.

## Main Responsibilities

The ESP32-S3 firmware performs:

- BLE discovery and connection to the HydroSense ESP32-C3.
- BLE notification reception and automatic reconnection.
- Validation of the exact 20-byte HydroSense telemetry packet.
- Packet length, protocol magic, version and CRC verification.
- Duplicate and out-of-order packet rejection.
- Rejection of packets from obsolete BLE connections.
- Sensor snapshot construction.
- Physical-range and validity checking.
- Environmental and thermal feature generation.
- FP32 TinyML Estimated TDS inference.
- Sensor anomaly analysis.
- Cross-layer sensor trust evaluation.
- Three-page LCD presentation.
- Non-blocking Wi-Fi connection maintenance.
- Realtime telemetry delivery for the website.
- Validated historical telemetry delivery for MongoDB storage.
- Independent BLE and cloud recovery using FreeRTOS tasks.

## Runtime Pipeline

```text
ESP32-C3 sensors
        |
        v
20-byte BLE telemetry packet
        |
        v
Length, magic, version and CRC validation
        |
        v
S3SensorSnapshot
        |
        v
S3FeatureBuilder
        |
        v
S3FeatureData
        |
        +---------------------------+
        |                           |
        v                           v
FP32 TinyML TDS model         Anomaly engine
        |                           |
        +-------------+-------------+
                      |
                      v
             Sensor trust engine
                      |
          +-----------+-----------+
          |           |           |
          v           v           v
         LCD     Realtime API   Historical API
```

## Hardware Target

The firmware targets the Waveshare ESP32-S3-LCD-1.9 development board.

Main board characteristics:

- ESP32-S3 processor
- Maximum CPU frequency of 240 MHz
- 16 MB flash
- 8 MB OPI PSRAM
- 1.9-inch ST7789V2 LCD
- Native LCD resolution of 170 × 320 pixels
- Landscape application layout of 320 × 170 pixels
- Wi-Fi
- Bluetooth Low Energy
- Native USB CDC serial
- BOOT/GPIO0 button for LCD page switching

The custom PlatformIO board definition is stored at:

```text
boards/waveshare_esp32_s3_lcd_1_9.json
```

## BLE Communication

The ESP32-S3 connects to the HydroSense ESP32-C3 using BLE.

### BLE identifiers

```text
Service UUID:
7b2c1000-6e9f-4b5a-9f8e-4a7c33b10001

Telemetry characteristic UUID:
7b2c1001-6e9f-4b5a-9f8e-4a7c33b10001
```

The C3 and S3 firmware must use the same UUIDs.

## C3 BLE Packet

The S3 expects an exact packed 20-byte packet:

| Field | Type | Description |
|---|---:|---|
| `magic` | `uint16_t` | Protocol identifier `0x4853` |
| `version` | `uint8_t` | Protocol version `1` |
| `sequence` | `uint8_t` | C3 measurement sequence |
| `airTemperature_cC` | `int16_t` | Air temperature in centi-degrees Celsius |
| `humidity_cPct` | `uint16_t` | Relative humidity in centi-percent |
| `waterTemperature_cC` | `int16_t` | Water temperature in centi-degrees Celsius |
| `stableDistance_mm` | `uint16_t` | Stable ultrasonic distance in millimetres |
| `waterLevel_cPct` | `uint16_t` | Reservoir level in centi-percent |
| `qualityPct` | `uint8_t` | C3 data-quality score |
| `qualityFlags` | `uint16_t` | C3 data-quality flags |
| `validMask` | `uint8_t` | Sensor and calibration validity bits |
| `crc16` | `uint16_t` | CRC-16/CCITT integrity value |

Protocol settings:

```text
Magic value       : 0x4853
Protocol version  : 1
Packet size       : 20 bytes
CRC initial value : 0xFFFF
CRC polynomial    : 0x1021
```

Changing the packet structure, field order, field size, UUIDs or CRC configuration requires coordinated changes to both the C3 and S3 firmware.

## Packet Protection

The S3 rejects packets when:

- The received length is not exactly 20 bytes.
- The protocol magic is incorrect.
- The protocol version is unsupported.
- The calculated CRC does not match the packet CRC.
- The packet came from an obsolete BLE connection.
- The packet is a duplicate.
- The packet sequence is out of order.

A received C3 measurement becomes stale after approximately 8 seconds.

If no valid packet arrives for approximately 12 seconds, the BLE recovery system reconnects to the C3.

## TinyML TDS Soft Sensor

The active deployed model is a direct FP32 neural-network implementation.

| Property | Value |
|---|---|
| Architecture | `3 → 32 → 16 → 8 → 1` |
| Hidden-layer activation | ReLU |
| Output activation | Linear |
| Parameter count | 801 |
| Active model format | FP32 weights compiled into firmware |
| Recorded FP32 MAE | 90.1565 ppm |
| Output | Estimated TDS in ppm |

### Required model input order

The model input order must not be changed:

1. `DHT_temp` — air temperature in degrees Celsius
2. `DHT_humidity` — relative humidity in percent
3. `water_temp` — water temperature in degrees Celsius

The firmware:

1. Validates the required sensor values.
2. Applies the exported StandardScaler parameters.
3. Executes the FP32 neural network.
4. Converts the standardized model output back to TDS ppm.
5. Rejects non-finite or invalid results.

The historical INT8 TFLite model header is retained only as a provenance and reproducibility reference.

It is not used by the active firmware, and the current build does not require TensorFlow Lite Micro.

See [MODEL_PROVENANCE.txt](MODEL_PROVENANCE.txt) for:

- Model architecture
- Training metrics
- Parameter count
- Model hashes
- Input order
- Scaling constants
- FP32 and historical INT8 information
- Reproducibility limitations

The original training dataset, notebook and Keras model are not included in this folder.

## Derived Features

The feature builder calculates environmental and thermal values including:

- Dew-point temperature
- Saturation vapour pressure
- Actual vapour pressure
- Vapour-pressure deficit
- Absolute humidity
- Air-to-water temperature difference
- Absolute air-to-water temperature difference
- Reservoir and distance availability
- Sensor-quality information

Derived features are only marked valid when their required source measurements are valid.

## Anomaly Detection

The anomaly engine evaluates sensor behaviour using learned runtime baselines.

It monitors conditions such as:

- Unusual air-temperature behaviour
- Unusual humidity behaviour
- Unusual water-temperature behaviour
- Water-level changes
- Distance changes
- Estimated TDS behaviour
- Missing or stale source data
- Cross-sensor inconsistencies

The baseline is learned from valid incoming measurements.

An anomaly result is not treated as a direct sensor measurement.

## Sensor Trust Evaluation

The sensor trust engine combines:

- C3 quality information
- Sensor validity
- Data freshness
- BLE connection state
- Anomaly severity
- Individual sensor anomaly results

It produces:

- Overall trust score
- Air-sensor trust
- Water-temperature trust
- Ultrasonic-sensor trust
- Reservoir-level trust
- Trust flags
- Human-readable trust state

Trust scores are bounded between 0 and 100 percent.

## LCD Interface

The onboard LCD contains exactly three pages:

1. Live sensor measurements
2. Edge-AI Estimated TDS and trust information
3. Derived environmental features

Press the BOOT/GPIO0 button to advance to the next page.

### Header indicators

| Indicator | Meaning |
|---|---|
| `B` | BLE connection to the C3 |
| `W` | Wi-Fi connection |
| `C` | Realtime cloud endpoint |
| `M` | Historical MongoDB telemetry path |

### Status colours

- Green: connected, ready or valid
- Amber: waiting, initializing or retry pending
- Red: disconnected, invalid or unavailable

Stale or disconnected C3 measurements are cleared instead of being displayed as current measurements.

## Cloud Architecture

The firmware uses two separate backend paths:

| Endpoint | Purpose | MongoDB storage |
|---|---|---|
| `/api/v1/realtime` | Live website updates through Socket.IO | No |
| `/api/v1/telemetry` | Validated historical observations | Yes |

### Realtime behaviour

- The queue keeps only the latest sensor state.
- One latest frame remains pending during a Wi-Fi or backend outage.
- A failed realtime upload is retried approximately every 3 seconds.
- A newer frame replaces an older pending frame.
- Invalid numeric values are transmitted as JSON `null`.
- Realtime data is not intended for MongoDB storage.

### Historical behaviour

- The normal historical upload interval is 5 minutes.
- Only complete and physically valid measurements are eligible.
- A failed upload waits at least 30 seconds before another attempt.
- The retry requires a new eligible sensor frame.
- An old failed sample is never replayed into MongoDB.

All HTTP operations run in a background FreeRTOS task.

Network delays therefore do not block:

- BLE packet validation
- TinyML inference
- Anomaly processing
- Trust processing
- LCD updates
- Button input

## Private Configuration

The public configuration template is:

```text
include/config/S3Secrets.example.h
```

Create a private copy named:

```text
include/config/S3Secrets.h
```

Enter your Wi-Fi and backend configuration only in the private file.

The private file must contain:

- Wi-Fi SSID
- Wi-Fi password
- Historical telemetry URL
- Realtime telemetry URL
- Device identifier

`S3Secrets.h` is ignored by Git and must never be uploaded to GitHub.

Never place real passwords, tokens or private URLs in:

- `main.cpp`
- `platformio.ini`
- `README.md`
- Screenshots
- Build logs
- Git commits

## Beginner Build Instructions

### Requirements

Install:

1. Visual Studio Code
2. PlatformIO IDE extension
3. A USB data cable
4. Waveshare ESP32-S3-LCD-1.9 board

### Open the project

1. Extract the complete project folder.
2. Open Visual Studio Code.
3. Select **File → Open Folder**.
4. Select the `HydroSense_S3` folder.
5. Confirm that `platformio.ini` is visible in the project root.

Do not open only the `src` folder.

### Configure the private settings

1. Open:

```text
include/config/
```

2. Copy:

```text
S3Secrets.example.h
```

3. Rename the copy to:

```text
S3Secrets.h
```

4. Enter your Wi-Fi and backend information.
5. Save the file.

### Build

1. Wait for PlatformIO to finish loading.
2. Allow PlatformIO to download the required platform and libraries.
3. Click the PlatformIO **Build** checkmark.
4. Wait for the build result.

A successful result ends with:

```text
SUCCESS
```

### Upload

1. Connect the ESP32-S3 using a USB data cable.
2. Select the correct serial port if PlatformIO does not detect it.
3. Click the PlatformIO **Upload** arrow.
4. Wait for the upload to complete.

Uploading is only required when installing new firmware on the physical board.

### Serial Monitor

1. Open PlatformIO Serial Monitor.
2. Use a baud rate of:

```text
115200
```

3. Power the HydroSense ESP32-C3.
4. Wait for the S3 to discover and connect to the C3.

## PlatformIO Configuration

The project uses:

```text
Platform: Espressif32 6.12.0
Framework: Arduino
Board: waveshare_esp32_s3_lcd_1_9
Monitor speed: 115200
Upload speed: 921600
Arduino_GFX: 1.4.9
```

Platform and library versions are pinned in `platformio.ini` to improve build reproducibility.

## Project Structure

```text
HydroSense_S3/
├── boards/
│   └── waveshare_esp32_s3_lcd_1_9.json
├── include/
│   ├── anomaly/
│   ├── cloud/
│   ├── config/
│   ├── features/
│   ├── models/
│   ├── tinyml/
│   ├── trust/
│   ├── ui/
│   └── README.md
├── lib/
│   └── README.md
├── src/
│   ├── anomaly/
│   ├── cloud/
│   ├── features/
│   ├── tinyml/
│   ├── trust/
│   ├── ui/
│   └── main.cpp
├── test/
│   └── README.md
├── .gitignore
├── MODEL_PROVENANCE.txt
├── platformio.ini
├── README.md
├── THIRD_PARTY_NOTICES.md
└── TINYML_INTEGRATION_README.txt
```

## Files That Must Not Be Uploaded

Do not upload:

```text
.pio/
include/config/S3Secrets.h
.vscode/c_cpp_properties.json
.vscode/launch.json
.vscode/ipch/
```

These files are already covered by `.gitignore`.

The `.pio` folder may remain on your computer, but it must not be committed because it contains large, generated and machine-specific build files.

## Testing Status

The following checks have been completed:

- Project structure inspection
- Source dependency inspection
- C3-to-S3 protocol comparison
- Packet-layout comparison
- CRC algorithm comparison
- Model parameter-count verification
- Model hash verification
- Feature-builder host execution
- FP32 TinyML host execution
- Anomaly-engine host execution
- Sensor-trust-engine host execution
- Cloud JSON host execution
- Existing PlatformIO build-artifact inspection

Physical hardware tests still require:

- Waveshare ESP32-S3-LCD-1.9
- HydroSense ESP32-C3
- Wi-Fi connection
- Running backend
- MongoDB configuration

The `test/` folder is currently reserved for future automated PlatformIO tests.

## Security Notice

The current firmware uses HTTP cloud endpoints.

HTTP does not encrypt telemetry while it travels across the network. Use the current configuration only on a trusted network.

Before public or internet-facing deployment, the system should be upgraded to:

- HTTPS
- TLS certificate verification
- Backend authentication
- Device-specific credentials or tokens
- Backend request validation
- Rate limiting

Never expose private credentials through GitHub.

For responsible vulnerability reporting, follow the repository-level [security policy](../SECURITY.md).

## Safety and Interpretation

- Estimated TDS is experimental and model-derived.
- It must not be described as a direct TDS measurement.
- Do not use Estimated TDS as the sole basis for health, dosing or safety decisions.
- Invalid or stale measurements must remain unavailable.
- The firmware deliberately avoids converting impossible measurements into apparently valid values.
- The HydroSense system is a research and engineering prototype.

## Third-Party Components

Third-party libraries remain under their respective licences.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Copyright

Copyright (c) 2026 Rupam Mondal.

Rupam Mondal remains the sole copyright owner of the original HydroSense source code.

## License

This component is distributed under the repository-level [MIT License](../LICENSE).

The MIT License permits use, copying, modification and distribution under its stated conditions.

Third-party components remain governed by their respective licences.