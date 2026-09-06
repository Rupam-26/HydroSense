<!--
SPDX-FileCopyrightText: 2026 Rupam Mondal
SPDX-License-Identifier: MIT
-->

# Changelog

All notable changes to HydroSense ESP32-C3 will be documented in this file.

The project follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- MIT project licensing.
- Copyright attribution for Rupam Mondal.
- Pinned PlatformIO platform and library versions.
- GitHub-ready project documentation.
- Third-party dependency notices.
- Machine-readable citation metadata.
- Three-page SH1106 OLED interface documentation.
- Reproducible build and upload instructions.

### Changed

- Corrected the OLED controller header to describe three runtime pages.
- Expanded `.gitignore` to exclude generated files, secrets and local machine data.

### Security

- Added exclusions for credential files, environment files and private keys.
- Documented safe HC-SR04 ECHO voltage reduction.

## [0.1.0] — Planned

First public development release of the HydroSense ESP32-C3 firmware.

Planned release requirements:

- Clean PlatformIO build.
- Successful ESP32-C3 upload.
- DHT22 hardware verification.
- DS18B20 hardware verification.
- HC-SR04 hardware verification.
- Three-page OLED verification.
- Button verification.
- NVS calibration verification.
- BLE packet and CRC verification.
- GitHub Actions build verification.