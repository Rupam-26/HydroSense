<!--
SPDX-FileCopyrightText: 2026 Rupam Mondal
SPDX-License-Identifier: MIT
-->

# Third-Party Software Notices

HydroSense ESP32-C3 uses third-party frameworks and libraries.

These components are not owned by Rupam Mondal. They remain copyrighted by their respective authors and are distributed under their original licenses.

The HydroSense MIT License does not replace or modify any third-party license.

## Dependency summary

| Component | Version | License | Source |
|---|---:|---|---|
| DHT Sensor Library | 1.4.7 | MIT | [Adafruit DHT Sensor Library](https://github.com/adafruit/DHT-sensor-library) |
| Adafruit Unified Sensor | 1.1.15 | Apache-2.0 | [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor) |
| OneWire | 2.3.8 | MIT | [Paul Stoffregen OneWire](https://github.com/PaulStoffregen/OneWire) |
| DallasTemperature | 4.0.6 | MIT | [Arduino Temperature Control Library](https://github.com/milesburton/Arduino-Temperature-Control-Library) |
| Adafruit GFX Library | 1.12.6 | BSD-3-Clause | [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) |
| Adafruit SH110X | 2.1.15 | BSD-3-Clause | [Adafruit SH110X](https://github.com/adafruit/Adafruit_SH110x) |
| Adafruit BusIO | PlatformIO-resolved dependency | MIT | [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) |
| Arduino Core for ESP32 | Platform-provided | LGPL-2.1-or-later | [Arduino Core for ESP32](https://github.com/espressif/arduino-esp32) |

## DHT Sensor Library

- Maintained by Adafruit Industries and contributors
- Used for DHT22 air-temperature and relative-humidity acquisition
- Licensed under the MIT License
- Official license: [DHT Sensor Library license](https://github.com/adafruit/DHT-sensor-library/blob/master/license.txt)

## Adafruit Unified Sensor

- Maintained by Adafruit Industries and contributors
- Provides the common Adafruit sensor abstraction
- Licensed under the Apache License 2.0
- Includes identified portions originating from the Android Open Source Project
- Official license: [Adafruit Unified Sensor license](https://github.com/adafruit/Adafruit_Sensor/blob/master/LICENSE.txt)

## OneWire

- Maintained by Paul Stoffregen and contributors
- Used for OneWire communication with the DS18B20
- Licensed under the MIT License
- Some source portions contain additional historical copyright and attribution notices
- Official source and notices: [OneWire repository](https://github.com/PaulStoffregen/OneWire)

## DallasTemperature

- Originally developed by Miles Burton with contributions from Tim Newsome, Guil Barros, Rob Tillaart and other contributors
- Used to communicate with the DS18B20 temperature sensor
- Version 4.0.6 is licensed under the MIT License
- Official license: [DallasTemperature license](https://github.com/milesburton/Arduino-Temperature-Control-Library/blob/4.0.6/LICENSE)

## Adafruit GFX Library

- Written and maintained by Adafruit Industries and contributors
- Used for OLED graphics and text rendering
- Licensed under the BSD 3-Clause License
- Individual bundled fonts or supporting files may contain additional notices
- Official license: [Adafruit GFX license](https://github.com/adafruit/Adafruit-GFX-Library/blob/master/license.txt)

## Adafruit SH110X

- Written and maintained by Adafruit Industries and contributors
- Used to control the SH1106 OLED
- Licensed under the BSD 3-Clause License
- Official license: [Adafruit SH110X license](https://github.com/adafruit/Adafruit_SH110x/blob/master/license.txt)

## Adafruit BusIO

- Maintained by Adafruit Industries and contributors
- Used transitively by Adafruit display libraries
- Licensed under the MIT License
- Official license: [Adafruit BusIO license](https://github.com/adafruit/Adafruit_BusIO/blob/master/LICENSE)

## Arduino Core for ESP32

- Developed and maintained by Espressif Systems and contributors
- Provides the Arduino framework, BLE, GPIO, I²C, NVS, timing and ESP32 runtime support
- Distributed under the GNU Lesser General Public License 2.1 or later
- The framework may contain separately licensed components and binary libraries
- Official license: [Arduino-ESP32 license](https://github.com/espressif/arduino-esp32/blob/master/LICENSE.md)

## PlatformIO dependency handling

The third-party libraries are declared in `platformio.ini`.

PlatformIO downloads them into the generated `.pio` directory during dependency installation and compilation.

The `.pio` directory is build output and must not be committed to this repository.

## Redistribution

Anyone redistributing HydroSense source code or compiled firmware is responsible for complying with all applicable third-party license conditions.

Required copyright, attribution and license notices must be preserved.

When distributing third-party source or binaries, include the corresponding official license files supplied with those components.

If this notice conflicts with an upstream license, the upstream component’s official license controls.

## No endorsement

Mentioning a third-party project does not imply that its authors or maintainers endorse HydroSense.

All product names, project names and trademarks belong to their respective owners.