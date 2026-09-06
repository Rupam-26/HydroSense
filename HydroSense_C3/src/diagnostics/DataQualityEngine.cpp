#include "diagnostics/DataQualityEngine.h"

#include "config/DataQualityConfig.h"

#include <math.h>

using namespace HydroSense;

// ============================================================
// Constructor
// ============================================================

DataQualityEngine::DataQualityEngine()
{
}

// ============================================================
// Sensor contribution
//
// Invalid/non-live sensor:
// contribution = 0
//
// Valid sensor:
// weight × availability fraction
//
// Example:
//
// Weight       = 20
// Availability = 98%
//
// Score:
// 20 × 0.98 = 19.6
// ============================================================

float DataQualityEngine::sensorContribution(
    const SensorHealth &health,
    float maximumWeight) const
{
    if (!health.valid)
    {
        return 0.0f;
    }

    float availability =
        health.availabilityPct;

    if (
        availability <
        DataQualityConfig::MIN_AVAILABILITY_PCT)
    {
        availability =
            DataQualityConfig::MIN_AVAILABILITY_PCT;
    }

    if (
        availability >
        DataQualityConfig::MAX_AVAILABILITY_PCT)
    {
        availability =
            DataQualityConfig::MAX_AVAILABILITY_PCT;
    }

    float fraction =
        availability / 100.0f;

    return maximumWeight *
           fraction;
}

// ============================================================
// Trend readiness
//
// Currently tracks:
//
// 1. Air temperature
// 2. Humidity
// 3. Water temperature
// 4. Water level
// 5. VPD
//
// Result:
// 0.0 -> none ready
// 1.0 -> all ready
// ============================================================

float DataQualityEngine::trendReadinessFraction(
    const C3AnalyticsData &analytics) const
{
    uint8_t readyCount =
        0;

    if (
        analytics.airTemperatureTrend.ready)
    {
        readyCount++;
    }

    if (
        analytics.humidityTrend.ready)
    {
        readyCount++;
    }

    if (
        analytics.waterTemperatureTrend.ready)
    {
        readyCount++;
    }

    if (
        analytics.waterLevelTrend.ready)
    {
        readyCount++;
    }

    if (
        analytics.vpdTrend.ready)
    {
        readyCount++;
    }

    constexpr float REQUIRED_TRENDS =
        5.0f;

    return static_cast<float>(
               readyCount) /
           REQUIRED_TRENDS;
}

// ============================================================
// Score classification
// ============================================================

DataQualityState DataQualityEngine::classifyScore(
    float score) const
{
    if (
        score >=
        DataQualityConfig::GOOD_THRESHOLD)
    {
        return DataQualityState::GOOD;
    }

    if (
        score >=
        DataQualityConfig::DEGRADED_THRESHOLD)
    {
        return DataQualityState::DEGRADED;
    }

    return DataQualityState::INVALID;
}

// ============================================================
// Human-readable state
// ============================================================

const char *DataQualityEngine::stateText(
    DataQualityState state)
{
    switch (state)
    {
    case DataQualityState::GOOD:
        return "GOOD";

    case DataQualityState::DEGRADED:
        return "DEGRADED";

    case DataQualityState::INVALID:
        return "INVALID";

    default:
        return "UNKNOWN";
    }
}

// ============================================================
// Complete evaluation
// ============================================================

DataQualityData DataQualityEngine::evaluate(
    const C3AnalyticsData &analytics,
    const NodeHealthData &health) const
{
    DataQualityData result{};

    // ========================================================
    // SENSOR VALIDITY
    // ========================================================

    result.dht22Valid =
        health.dht22.valid;

    result.ds18b20Valid =
        health.ds18b20.valid;

    result.ultrasonicValid =
        health.ultrasonic.valid;

    // ========================================================
    // CALIBRATION / RESERVOIR
    // ========================================================

    result.calibrationValid =
        health.calibrationValid;

    result.reservoirValid =
        health.reservoirValid;

    // ========================================================
    // ENVIRONMENTAL DATA
    // ========================================================

    result.environmentValid =
        analytics.environment.completeValid;

    // ========================================================
    // TREND STATES
    // ========================================================

    result.airTemperatureTrendReady =
        analytics.airTemperatureTrend.ready;

    result.humidityTrendReady =
        analytics.humidityTrend.ready;

    result.waterTemperatureTrendReady =
        analytics.waterTemperatureTrend.ready;

    result.waterLevelTrendReady =
        analytics.waterLevelTrend.ready;

    result.vpdTrendReady =
        analytics.vpdTrend.ready;

    result.allRequiredTrendsReady =
        result.airTemperatureTrendReady &&
        result.humidityTrendReady &&
        result.waterTemperatureTrendReady &&
        result.waterLevelTrendReady &&
        result.vpdTrendReady;

    // ========================================================
    // BUILD QUALITY FLAGS
    // ========================================================

    result.flags =
        DQ_NONE;

    if (!result.dht22Valid)
    {
        result.flags |=
            DQ_DHT22_INVALID;
    }

    if (!result.ds18b20Valid)
    {
        result.flags |=
            DQ_DS18B20_INVALID;
    }

    if (!result.ultrasonicValid)
    {
        result.flags |=
            DQ_ULTRASONIC_INVALID;
    }

    if (!result.calibrationValid)
    {
        result.flags |=
            DQ_CALIBRATION_INVALID;
    }

    if (!result.reservoirValid)
    {
        result.flags |=
            DQ_RESERVOIR_INVALID;
    }

    if (!result.environmentValid)
    {
        result.flags |=
            DQ_ENVIRONMENT_INVALID;
    }

    if (
        !result.airTemperatureTrendReady)
    {
        result.flags |=
            DQ_AIR_TEMP_TREND_NOT_READY;
    }

    if (
        !result.humidityTrendReady)
    {
        result.flags |=
            DQ_HUMIDITY_TREND_NOT_READY;
    }

    if (
        !result.waterTemperatureTrendReady)
    {
        result.flags |=
            DQ_WATER_TEMP_TREND_NOT_READY;
    }

    if (
        !result.waterLevelTrendReady)
    {
        result.flags |=
            DQ_WATER_LEVEL_TREND_NOT_READY;
    }

    if (
        !result.vpdTrendReady)
    {
        result.flags |=
            DQ_VPD_TREND_NOT_READY;
    }

    // ========================================================
    // SCORE
    // ========================================================

    float score =
        0.0f;

    score +=
        sensorContribution(
            health.dht22,
            DataQualityConfig::
                DHT22_WEIGHT);

    score +=
        sensorContribution(
            health.ds18b20,
            DataQualityConfig::
                DS18B20_WEIGHT);

    score +=
        sensorContribution(
            health.ultrasonic,
            DataQualityConfig::
                ULTRASONIC_WEIGHT);

    if (result.calibrationValid)
    {
        score +=
            DataQualityConfig::
                CALIBRATION_WEIGHT;
    }

    if (result.reservoirValid)
    {
        score +=
            DataQualityConfig::
                RESERVOIR_WEIGHT;
    }

    if (result.environmentValid)
    {
        score +=
            DataQualityConfig::
                ENVIRONMENT_WEIGHT;
    }

    float trendFraction =
        trendReadinessFraction(
            analytics);

    score +=
        DataQualityConfig::
            TREND_WEIGHT *
        trendFraction;

    // Defensive clamp
    if (score < 0.0f)
    {
        score = 0.0f;
    }

    if (
        score >
        DataQualityConfig::MAX_SCORE)
    {
        score =
            DataQualityConfig::MAX_SCORE;
    }

    result.scorePct =
        score;

    result.state =
        classifyScore(
            score);

    // ========================================================
    // USABILITY DECISIONS
    // ========================================================

    // Basic telemetry requires all three direct sensors.
    result.telemetryUsable =
        result.dht22Valid &&
        result.ds18b20Valid &&
        result.ultrasonicValid;

    // Reservoir values require ultrasonic + calibration/model.
    result.reservoirDataUsable =
        result.ultrasonicValid &&
        result.calibrationValid &&
        result.reservoirValid;

    // Environmental physics requires DHT + water temperature.
    result.environmentalDataUsable =
        result.dht22Valid &&
        result.ds18b20Valid &&
        result.environmentValid;

    // Future TinyML input permission.
    //
    // We deliberately require:
    //
    // - all three physical sensors
    // - valid reservoir data
    // - valid environmental data
    // - complete trend history
    //
    // If future ML feature requirements change, this decision
    // can be modified here without touching sensor drivers.
    result.aiInputUsable =
        result.telemetryUsable &&
        result.reservoirDataUsable &&
        result.environmentalDataUsable &&
        result.allRequiredTrendsReady;

    return result;
}