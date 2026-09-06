# HydroSense ESP32-S3 Private Libraries

Copyright (c) 2026 Rupam Mondal  
SPDX-License-Identifier: MIT

This directory is reserved for project-specific PlatformIO libraries.

The current HydroSense ESP32-S3 firmware does not require any private library
inside this directory. Firmware modules are stored in `src/` and their public
headers are stored in `include/`.

External libraries must be declared and version-pinned in the root
`platformio.ini` file. PlatformIO will download them automatically during the
first build.

Do not copy generated libraries from `.pio/libdeps/` into this directory.