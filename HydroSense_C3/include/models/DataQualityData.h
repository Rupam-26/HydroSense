#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE
// DATA QUALITY MODEL
//
// DATA ONLY.
//
// No calculations.
// No hardware access.
// No communications.
// ============================================================

// ============================================================
// OVERALL DATA QUALITY STATE
// ============================================================

enum class DataQualityState : uint8_t
{
    INVALID = 0,
    DEGRADED,
    GOOD
};

// ============================================================
// QUALITY / FAULT FLAGS
//
// Multiple flags can be active simultaneously.
// This will later be useful for ESP-NOW and the S3.
// ============================================================

enum DataQualityFlags : uint16_t
{
    DQ_NONE = 0,

    DQ_DHT22_INVALID = 1 << 0,
    DQ_DS18B20_INVALID = 1 << 1,
    DQ_ULTRASONIC_INVALID = 1 << 2,

    DQ_CALIBRATION_INVALID = 1 << 3,
    DQ_RESERVOIR_INVALID = 1 << 4,
    DQ_ENVIRONMENT_INVALID = 1 << 5,

    DQ_AIR_TEMP_TREND_NOT_READY = 1 << 6,
    DQ_HUMIDITY_TREND_NOT_READY = 1 << 7,
    DQ_WATER_TEMP_TREND_NOT_READY = 1 << 8,
    DQ_WATER_LEVEL_TREND_NOT_READY = 1 << 9,
    DQ_VPD_TREND_NOT_READY = 1 << 10
};

// ============================================================
// COMPLETE DATA-QUALITY SNAPSHOT
// ============================================================

struct DataQualityData
{
    // --------------------------------------------------------
    // Overall score
    //
    // 0   = unusable
    // 100 = maximum current confidence
    // --------------------------------------------------------

    float scorePct = 0.0f;

    // --------------------------------------------------------
    // Overall categorical state
    // --------------------------------------------------------

    DataQualityState state =
        DataQualityState::INVALID;

    // --------------------------------------------------------
    // Component validity
    // --------------------------------------------------------

    bool dht22Valid = false;

    bool ds18b20Valid = false;

    bool ultrasonicValid = false;

    bool calibrationValid = false;

    bool reservoirValid = false;

    bool environmentValid = false;

    // --------------------------------------------------------
    // Trend readiness
    // --------------------------------------------------------

    bool airTemperatureTrendReady = false;

    bool humidityTrendReady = false;

    bool waterTemperatureTrendReady = false;

    bool waterLevelTrendReady = false;

    bool vpdTrendReady = false;

    bool allRequiredTrendsReady = false;

    // --------------------------------------------------------
    // Permission / usability decisions
    // --------------------------------------------------------

    // Basic real sensor telemetry can be used.
    bool telemetryUsable = false;

    // Reservoir level/depth can be trusted.
    bool reservoirDataUsable = false;

    // Environmental derived parameters can be trusted.
    bool environmentalDataUsable = false;

    // Full input set exists for future TinyML processing.
    bool aiInputUsable = false;

    // --------------------------------------------------------
    // Diagnostic bit mask
    // --------------------------------------------------------

    uint16_t flags = DQ_NONE;
};