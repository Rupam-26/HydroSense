<!--
SPDX-FileCopyrightText: 2026 Rupam Mondal
SPDX-License-Identifier: MIT
-->

## Summary

Describe the change clearly.

## Related issue

Closes #

## Change type

- [ ] Bug fix
- [ ] New feature
- [ ] Documentation
- [ ] Test
- [ ] Refactoring
- [ ] Build or CI
- [ ] Other

## Components affected

- [ ] DHT22
- [ ] DS18B20
- [ ] HC-SR04
- [ ] Filtering
- [ ] Environmental physics
- [ ] Reservoir calibration
- [ ] Analytics
- [ ] Sensor health
- [ ] Data quality
- [ ] OLED
- [ ] Button
- [ ] BLE
- [ ] Serial terminal
- [ ] PlatformIO
- [ ] Documentation

## Testing performed

Describe the hardware and software testing performed.

## Verification checklist

- [ ] A clean PlatformIO build succeeds.
- [ ] The firmware uploads successfully.
- [ ] Serial output works at 115200 baud.
- [ ] Relevant physical sensors were tested.
- [ ] All three OLED pages were tested.
- [ ] GPIO3 button switching was tested.
- [ ] Reservoir configuration remains functional.
- [ ] NVS persistence remains functional.
- [ ] BLE connection and reconnection were tested.
- [ ] The BLE packet remains exactly 20 bytes.
- [ ] CRC behavior remains correct.
- [ ] Documentation was updated.
- [ ] `CHANGELOG.md` was updated.
- [ ] No generated `.pio` files are included.
- [ ] No credentials or private information are included.
- [ ] New files contain copyright and SPDX information.
- [ ] The commits include a Developer Certificate of Origin sign-off.

## BLE compatibility

Describe any impact on the ESP32-S3 receiver.

```text
BLE compatibility impact: