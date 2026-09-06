# HydroSense ESP32-S3 Tests

Copyright (c) 2026 Rupam Mondal  
SPDX-License-Identifier: MIT

This directory is reserved for PlatformIO automated tests.

Automated unit tests have not yet been added. The current firmware has been
verified using a successful PlatformIO build.

Future tests should cover:

- BLE packet length, header, version and CRC validation.
- Sensor-value range validation.
- Derived-feature calculations.
- TinyML input preprocessing and TDS estimation.
- Anomaly detection.
- Sensor-trust scoring.
- Cloud JSON generation.
- Real-time and historical upload retry behaviour.

Hardware-dependent testing must be performed using the Waveshare
ESP32-S3-LCD-1.9 board and a compatible HydroSense ESP32-C3 transmitter.