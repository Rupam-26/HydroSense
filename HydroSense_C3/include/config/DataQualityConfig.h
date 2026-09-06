#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE
// DATA QUALITY CONFIGURATION
//
// All tunable scoring weights and thresholds live here.
//
// Therefore changing quality policy later does NOT require
// editing DataQualityEngine.cpp.
// ============================================================

namespace HydroSense
{
    namespace DataQualityConfig
    {

        // ============================================================
        // SENSOR WEIGHTS
        //
        // Total sensor contribution = 60 points
        // ============================================================

        constexpr float DHT22_WEIGHT =
            20.0f;

        constexpr float DS18B20_WEIGHT =
            20.0f;

        constexpr float ULTRASONIC_WEIGHT =
            20.0f;

        // ============================================================
        // PROCESSING / DERIVED-DATA WEIGHTS
        //
        // Total = 40 points
        // ============================================================

        constexpr float CALIBRATION_WEIGHT =
            10.0f;

        constexpr float RESERVOIR_WEIGHT =
            10.0f;

        constexpr float ENVIRONMENT_WEIGHT =
            10.0f;

        constexpr float TREND_WEIGHT =
            10.0f;

        // ============================================================
        // TOTAL
        // ============================================================

        constexpr float MAX_SCORE =
            100.0f;

        // ============================================================
        // QUALITY STATE THRESHOLDS
        //
        // GOOD:
        // >= 85
        //
        // DEGRADED:
        // >= 50 and < 85
        //
        // INVALID:
        // < 50
        // ============================================================

        constexpr float GOOD_THRESHOLD =
            85.0f;

        constexpr float DEGRADED_THRESHOLD =
            50.0f;

        // ============================================================
        // MINIMUM SENSOR AVAILABILITY CONTRIBUTION
        //
        // Availability is already calculated by SensorHealthManager.
        // A LIVE sensor's scoring contribution is scaled using its
        // historical availability percentage.
        // ============================================================

        constexpr float MIN_AVAILABILITY_PCT =
            0.0f;

        constexpr float MAX_AVAILABILITY_PCT =
            100.0f;

    }
}
