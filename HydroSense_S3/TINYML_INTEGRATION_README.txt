HYDROSENSE ESP32-S3 — FP32 TDS SOFT-SENSOR INTEGRATION
=======================================================

Copyright (c) 2026 Rupam Mondal
SPDX-License-Identifier: MIT


OVERVIEW
--------

The ESP32-S3 receives genuine sensor data from the HydroSense ESP32-C3
through Bluetooth Low Energy (BLE).

The S3:

1. Receives the fixed 20-byte C3 telemetry packet.
2. Checks packet length, protocol magic and protocol version.
3. Verifies the CRC-16 checksum.
4. Rejects duplicate, obsolete and out-of-order packets.
5. Creates an S3SensorSnapshot.
6. Builds derived environmental and thermal features.
7. Runs the FP32 TDS soft-sensor model.
8. Updates the three-page LCD interface.
9. Performs anomaly and sensor-trust analysis.
10. Sends realtime and historical telemetry to the backend.

No fake, synthetic or replacement sensor values are generated.


ACTIVE TINYML MODEL
-------------------

Model:
Tiny_32_16_8

Architecture:
3 -> 32 -> 16 -> 8 -> 1

Hidden activation:
ReLU

Output activation:
Linear

Parameters:
801

Runtime:
Direct FP32 C++ inference

Input order:
1. DHT_temp
2. DHT_humidity
3. water_temp

Recorded FP32 MAE:
90.1565 ppm

The firmware does not require TensorFlow, TensorFlow Lite, TensorFlow Lite
Micro or another external machine-learning runtime.


MODEL FILES
-----------

Active FP32 weights:
include/tinyml/HydroSense_TDS_FP32_weights.h

Preprocessing constants:
include/tinyml/HydroSense_TDS_constants.h

Inference interface:
include/tinyml/S3TinyMLTDS.h

Inference implementation:
src/tinyml/S3TinyMLTDS.cpp

Model result structure:
include/models/S3TDSResult.h

Detailed model information:
MODEL_PROVENANCE.txt


HISTORICAL INT8 REFERENCE
-------------------------

The following file is retained only for research provenance:

include/tinyml/HydroSense_TDS_INT8_model.h

It is not included, compiled or executed by the active firmware.


IMPORTANT INTERPRETATION
------------------------

The displayed value is called:

Estimated TDS

It is a machine-learning soft-sensor estimate. It is not a direct electrical
conductivity or physical TDS measurement.

The model may extrapolate when sensor conditions differ from its training
data. Do not use the estimate as the only basis for safety-critical,
agricultural or chemical decisions.


REQUIRED PLATFORMIO DEPENDENCY
------------------------------

GFX Library for Arduino 1.4.9

PlatformIO downloads this dependency automatically.

No TFLite dependency is required.


PRIVATE CONFIGURATION
---------------------

Copy:

include/config/S3Secrets.example.h

to:

include/config/S3Secrets.h

Enter the Wi-Fi name, Wi-Fi password, backend IP address and device ID in
S3Secrets.h.

S3Secrets.h is ignored by Git and must never be uploaded.


BEGINNER BUILD STEPS
--------------------

1. Install Visual Studio Code.
2. Install the PlatformIO IDE extension.
3. Open the HydroSense_S3 folder in Visual Studio Code.
4. Confirm that platformio.ini is visible at the folder root.
5. Open include/config/S3Secrets.h.
6. Enter the correct Wi-Fi and backend information.
7. Connect the Waveshare ESP32-S3-LCD-1.9 using a USB data cable.
8. Click the PlatformIO Build check-mark button.
9. Wait for SUCCESS.
10. Click the PlatformIO Upload arrow button.
11. Open the PlatformIO Serial Monitor at 115200 baud.
12. Power the HydroSense ESP32-C3 separately.


EXPECTED STARTUP OUTPUT
-----------------------

LCD initialization   : SUCCESS
TinyML initialization: SUCCESS
TinyML model         : Tiny_32_16_8 FP32
BLE queue            : READY
Cloud TX queue       : READY
BLE stack            : READY
BLE manager task     : READY
Cloud async task     : READY


LCD PAGES
---------

Page 1:
Real sensor data

Page 2:
FP32 Edge AI TDS estimate

Page 3:
Derived environmental and thermal features

Press the BOOT button once after startup to move to the next page.


VALIDITY BEHAVIOUR
------------------

Inference requires:

- A CRC-valid C3 packet
- Valid DHT22 air temperature
- Valid DHT22 humidity
- Valid DS18B20 water temperature
- Finite values inside their physical sensor ranges

If required inputs are unavailable, Estimated TDS is reported as unavailable.

Values are not clamped, fabricated or replaced.


CLOUD PATHS
-----------

Realtime telemetry:
S3 -> /api/v1/realtime -> website live update

Historical telemetry:
S3 -> /api/v1/telemetry -> MongoDB storage

Realtime and historical uploads are handled by a background FreeRTOS task so
HTTP delays do not block BLE packet processing, the LCD or the BOOT button.


C3 COMPATIBILITY
----------------

The S3 firmware matches the C3 contract:

Packet size:
20 bytes

Protocol version:
1

CRC:
CRC-16/CCITT with initial value 0xFFFF

Normal C3 telemetry interval:
Approximately 3 seconds

S3 stale-data threshold:
8 seconds