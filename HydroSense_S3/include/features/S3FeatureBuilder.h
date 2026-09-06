// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>

#include "models/S3FeatureData.h"
#include "models/S3SensorSnapshot.h"

// ============================================================
// HYDROSENSE ESP32-S3
// FEATURE BUILDER
//
// Converts a validated S3SensorSnapshot into S3FeatureData.
//
// This module calculates derived environmental and thermal
// quantities. It does not handle BLE, LCD, TinyML, Wi-Fi,
// cloud communication or MongoDB.
// ============================================================

class S3FeatureBuilder
{
public:
    S3FeatureData build(
        const S3SensorSnapshot &snapshot,
        uint32_t nowMs) const;

private:
    // Magnus approximation.
    // Returns the dew-point temperature in degrees Celsius.
    static float calculateDewPointC(
        float temperatureC,
        float humidityPct);

    // Returns saturation vapour pressure in kPa.
    static float calculateSaturationVaporPressureKPa(
        float temperatureC);

    // Returns actual vapour pressure in kPa.
    static float calculateActualVaporPressureKPa(
        float saturationVaporPressureKPa,
        float humidityPct);

    // Returns vapour-pressure deficit in kPa.
    static float calculateVpdKPa(
        float saturationVaporPressureKPa,
        float actualVaporPressureKPa);

    // Returns absolute humidity in grams per cubic metre.
    static float calculateAbsoluteHumidityGm3(
        float temperatureC,
        float actualVaporPressureKPa);
};