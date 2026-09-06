# HydroSense ESP32-S3 Header Files

Copyright (c) 2026 Rupam Mondal  
SPDX-License-Identifier: MIT

This directory contains the public headers and data models used by the
HydroSense ESP32-S3 firmware.

## Directory Structure

- `anomaly/` — Sensor anomaly-detection declarations.
- `cloud/` — Historical and real-time cloud uploader declarations.
- `config/` — Wi-Fi and backend configuration templates.
- `features/` — Derived sensor-feature generation declarations.
- `models/` — Shared sensor, TinyML, anomaly, trust and cloud-status models.
- `tinyml/` — TDS soft-sensor model, constants and inference declarations.
- `trust/` — Sensor-trust evaluation declarations.
- `ui/` — Waveshare LCD manager declarations.

## Private Configuration

`config/S3Secrets.h` contains private credentials and must never be committed.

Create it by copying:

`config/S3Secrets.example.h`

Then enter your actual Wi-Fi and backend details inside the private copy.