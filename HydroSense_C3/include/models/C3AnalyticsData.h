#pragma once

#include <Arduino.h>
#include <math.h>

#include "models/EnvironmentalData.h"

// ============================================================
// HYDROSENSE ESP32-C3
// UNIFIED ANALYTICS DATA MODEL
//
// DATA ONLY.
//
// Compatibility aliases are intentionally provided for:
//   dhtValid <-> dht22Valid
//   completeSensorSetValid <-> completeValid
//
// Both names use the SAME underlying memory.
// There is therefore no duplicated state and no possibility
// of the two aliases becoming inconsistent.
// ============================================================

// ============================================================
// TREND METRICS
// ============================================================

struct TrendMetrics
{
    // --------------------------------------------------------
    // WINDOW STATE
    // --------------------------------------------------------

    uint8_t sampleCount = 0;

    bool ready = false;

    // --------------------------------------------------------
    // CURRENT / PREVIOUS VALUES
    // --------------------------------------------------------

    float latest = NAN;

    float previous = NAN;

    float latestChange = NAN;

    // --------------------------------------------------------
    // ROLLING STATISTICS
    // --------------------------------------------------------

    float mean = NAN;

    float minimum = NAN;

    float maximum = NAN;

    float range = NAN;

    float standardDeviation = NAN;

    // --------------------------------------------------------
    // LINEAR REGRESSION TREND
    //
    // Units are normalized to change per hour.
    // --------------------------------------------------------

    float slopePerHour = NAN;
};

// ============================================================
// COMPLETE C3 ANALYTICS SNAPSHOT
// ============================================================

struct C3AnalyticsData
{
    // ========================================================
    // TIMESTAMP
    // ========================================================

    uint32_t timestampMs = 0;

    // ========================================================
    // SENSOR VALIDITY
    // ========================================================

    // --------------------------------------------------------
    // DHT22 compatibility alias
    //
    // Older C3AnalyticsEngine code:
    //     data.dhtValid
    //
    // Newer code:
    //     data.dht22Valid
    //
    // Both names refer to exactly the same bool.
    // --------------------------------------------------------

    union
    {
        bool dht22Valid = false;
        bool dhtValid;
    };

    bool ds18b20Valid = false;

    bool ultrasonicValid = false;

    bool calibrationValid = false;

    bool reservoirValid = false;

    // ========================================================
    // DIRECT SENSOR VALUES
    // ========================================================

    float airTemperatureC = NAN;

    float humidityPct = NAN;

    float waterTemperatureC = NAN;

    // ========================================================
    // ULTRASONIC PIPELINE
    // ========================================================

    uint32_t echoTimeUs = 0;

    // Raw HC-SR04 distance calculated by the sensor driver
    // using nominal speed of sound.
    float rawDistanceCm = NAN;

    // Temperature-compensated distance.
    float correctedDistanceCm = NAN;

    // Median-filtered corrected distance.
    float stableDistanceCm = NAN;

    // ========================================================
    // RESERVOIR MODEL
    // ========================================================

    float waterDepthCm = NAN;

    float waterLevelPct = NAN;

    float usableDepthCm = NAN;

    // ========================================================
    // ENVIRONMENTAL PHYSICS
    // ========================================================

    EnvironmentalData environment{};

    // ========================================================
    // ROLLING ANALYTICS
    // ========================================================

    TrendMetrics airTemperatureTrend{};

    TrendMetrics humidityTrend{};

    TrendMetrics waterTemperatureTrend{};

    TrendMetrics waterLevelTrend{};

    TrendMetrics vpdTrend{};

    // ========================================================
    // AGGREGATE VALIDITY
    // ========================================================

    // Direct physical sensor set
    bool directSensorsValid = false;

    // Environmental physics layer
    bool environmentalValid = false;

    // --------------------------------------------------------
    // Complete sensor-set compatibility alias
    //
    // Existing C3AnalyticsEngine:
    //     completeSensorSetValid
    //
    // Newer diagnostics/display code:
    //     completeValid
    //
    // Again, these are one physical bool.
    // --------------------------------------------------------

    union
    {
        bool completeValid = false;
        bool completeSensorSetValid;
    };

    // General analytics snapshot validity.
    bool valid = false;
};