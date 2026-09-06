<!--
SPDX-FileCopyrightText: 2026 Rupam Mondal
SPDX-License-Identifier: MIT
-->

# Contributing to HydroSense ESP32-C3

Thank you for considering a contribution.

## Before contributing

1. Read `README.md`.
2. Read `SECURITY.md`.
3. Read `LICENSE`.
4. Read `THIRD_PARTY_NOTICES.md`.
5. Search existing issues and pull requests.
6. Open an issue before making a major architectural change.

## Development setup

1. Install Visual Studio Code.
2. Install the PlatformIO IDE extension.
3. Clone the repository.
4. Open the folder containing `platformio.ini`.
5. Allow PlatformIO to install the pinned dependencies.
6. Run a clean build:

```bash
pio run --target clean
pio run