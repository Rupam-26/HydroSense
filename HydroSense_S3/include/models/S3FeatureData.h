#pragma once

/*
 * SPDX-FileCopyrightText: 2026 Rupam Mondal
 * SPDX-License-Identifier: MIT
 */

#include <Arduino.h>
#include <math.h>

// ============================================================
// HYDROSENSE ESP32-S3
// DERIVED FEATURE DATA MODEL
//
// The active FP32 TinyML model directly uses:
//   1. Air temperature
//   2. Relative humidity
//   3. Water temperature
//
// This structure also contains additional scientifically derived
// values used by the LCD, diagnostics, anomaly analysis, Sensor
// Trust analysis and cloud telemetry.
// ============================================================

struct S3FeatureData
{
    uint8_t sourceSequence =
        0;

    uint32_t generatedAtMs =
        0;

    // Direct values copied from the C3 snapshot.
    float airTemperatureC =
        NAN;

    float humidityPct =
        NAN;

    float waterTemperatureC =
        NAN;

    float stableDistanceCm =
        NAN;

    float waterLevelPct =
        NAN;

    // Environmental derived features.
    float dewPointC =
        NAN;

    float saturationVaporPressureKPa =
        NAN;

    float actualVaporPressureKPa =
        NAN;

    float vpdKPa =
        NAN;

    float absoluteHumidityGm3 =
        NAN;

    // Thermal derived features.
    float airWaterDeltaC =
        NAN;

    float absoluteAirWaterDeltaC =
        NAN;

    // Distance and reservoir availability.
    bool distanceAvailable =
        false;

    bool waterLevelAvailable =
        false;

    // C3 data-quality information.
    float qualityScorePct =
        0.0f;

    uint16_t qualityFlags =
        0;

    // Individual source validity.
    bool airEnvironmentValid =
        false;

    bool waterTemperatureValid =
        false;

    bool ultrasonicValid =
        false;

    bool reservoirValid =
        false;

    bool calibrationValid =
        false;

    // Derived feature-group validity.
    bool environmentalFeaturesValid =
        false;

    bool thermalFeaturesValid =
        false;

    // True when this structure was created from a valid packet.
    // Individual fields can still be unavailable.
    bool valid =
        false;

    uint32_t ageMs(
        uint32_t nowMs) const
    {
        if (!valid)
        {
            return UINT32_MAX;
        }

        return nowMs -
               generatedAtMs;
    }

    void clear()
    {
        sourceSequence =
            0;

        generatedAtMs =
            0;

        airTemperatureC =
            NAN;

        humidityPct =
            NAN;

        waterTemperatureC =
            NAN;

        stableDistanceCm =
            NAN;

        waterLevelPct =
            NAN;

        dewPointC =
            NAN;

        saturationVaporPressureKPa =
            NAN;

        actualVaporPressureKPa =
            NAN;

        vpdKPa =
            NAN;

        absoluteHumidityGm3 =
            NAN;

        airWaterDeltaC =
            NAN;

        absoluteAirWaterDeltaC =
            NAN;

        distanceAvailable =
            false;

        waterLevelAvailable =
            false;

        qualityScorePct =
            0.0f;

        qualityFlags =
            0;

        airEnvironmentValid =
            false;

        waterTemperatureValid =
            false;

        ultrasonicValid =
            false;

        reservoirValid =
            false;

        calibrationValid =
            false;

        environmentalFeaturesValid =
            false;

        thermalFeaturesValid =
            false;

        valid =
            false;
    }
};