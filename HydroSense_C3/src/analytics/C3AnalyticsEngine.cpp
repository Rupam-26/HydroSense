#include "analytics/C3AnalyticsEngine.h"

// ============================================================
// CONSTRUCTOR
// ============================================================

C3AnalyticsEngine::C3AnalyticsEngine(
    uint8_t trendWindowSize)
    : airTemperatureTrend(
          trendWindowSize),
      humidityTrend(
          trendWindowSize),
      waterTemperatureTrend(
          trendWindowSize),
      waterLevelTrend(
          trendWindowSize),
      vpdTrend(
          trendWindowSize),
      latestData(),
      haveCapturedDHT22Source(false),
      haveCapturedDS18B20Source(false),
      haveCapturedReservoirSource(false),
      lastCapturedDHT22SourceMs(0),
      lastCapturedDS18B20SourceMs(0),
      lastCapturedReservoirSourceMs(0)
{
}

// ============================================================
// RESET
// ============================================================

void C3AnalyticsEngine::reset()
{
    airTemperatureTrend.reset();
    humidityTrend.reset();
    waterTemperatureTrend.reset();
    waterLevelTrend.reset();
    vpdTrend.reset();

    latestData =
        C3AnalyticsData{};

    haveCapturedDHT22Source =
        false;

    haveCapturedDS18B20Source =
        false;

    haveCapturedReservoirSource =
        false;

    lastCapturedDHT22SourceMs =
        0;

    lastCapturedDS18B20SourceMs =
        0;

    lastCapturedReservoirSourceMs =
        0;
}

// ============================================================
// SOURCE DEDUPLICATION
// ============================================================

bool C3AnalyticsEngine::shouldCaptureSource(
    bool sourceValid,
    bool alreadyCaptured,
    uint32_t sourceTimestampMs,
    uint32_t lastCapturedTimestampMs) const
{
    if (!sourceValid)
    {
        return false;
    }

    if (!alreadyCaptured)
    {
        return true;
    }

    return sourceTimestampMs !=
           lastCapturedTimestampMs;
}

// ============================================================
// CONVERT SIGNAL TREND TO PLAIN DATA
// ============================================================

TrendMetrics C3AnalyticsEngine::makeTrendMetrics(
    const SignalTrend &trend) const
{
    TrendMetrics metrics{};

    metrics.latest =
        trend.latest();

    metrics.previous =
        trend.previous();

    metrics.mean =
        trend.mean();

    metrics.minimum =
        trend.minimum();

    metrics.maximum =
        trend.maximum();

    metrics.range =
        trend.range();

    metrics.standardDeviation =
        trend.standardDeviation();

    metrics.latestChange =
        trend.latestChange();

    metrics.slopePerHour =
        trend.slopePerHour();

    metrics.sampleCount =
        trend.sampleCount();

    metrics.ready =
        trend.isReady();

    return metrics;
}

// ============================================================
// UPDATE ANALYTICS SNAPSHOT
// ============================================================

void C3AnalyticsEngine::update(
    uint32_t timestampMs,

    bool dhtValid,
    float airTemperatureC,
    float humidityPct,

    bool ds18b20Valid,
    float waterTemperatureC,

    bool ultrasonicValid,
    uint32_t echoTimeUs,
    float rawDistanceCm,
    float correctedDistanceCm,
    float stableDistanceCm,

    bool calibrationValid,

    bool reservoirValid,
    float waterDepthCm,
    float waterLevelPct,
    float usableDepthCm,

    const C3AnalyticsSourceTimestamps &sourceTimestamps,
    bool captureTrendSamples)
{
    // --------------------------------------------------------
    // Start a fresh current snapshot. Trend engines themselves are
    // persistent and are copied back near the end of this function.
    // --------------------------------------------------------

    latestData =
        C3AnalyticsData{};

    latestData.timestampMs =
        timestampMs;

    // ========================================================
    // DIRECT SENSOR DATA
    // ========================================================

    latestData.dhtValid =
        dhtValid;

    latestData.ds18b20Valid =
        ds18b20Valid;

    latestData.ultrasonicValid =
        ultrasonicValid;

    if (dhtValid)
    {
        latestData.airTemperatureC =
            airTemperatureC;

        latestData.humidityPct =
            humidityPct;
    }

    if (ds18b20Valid)
    {
        latestData.waterTemperatureC =
            waterTemperatureC;
    }

    if (ultrasonicValid)
    {
        latestData.echoTimeUs =
            echoTimeUs;

        latestData.rawDistanceCm =
            rawDistanceCm;

        latestData.correctedDistanceCm =
            correctedDistanceCm;

        latestData.stableDistanceCm =
            stableDistanceCm;
    }

    // ========================================================
    // CALIBRATION / RESERVOIR
    // ========================================================

    latestData.calibrationValid =
        calibrationValid;

    latestData.reservoirValid =
        reservoirValid;

    if (reservoirValid)
    {
        latestData.waterDepthCm =
            waterDepthCm;

        latestData.waterLevelPct =
            waterLevelPct;

        latestData.usableDepthCm =
            usableDepthCm;
    }

    // ========================================================
    // ENVIRONMENTAL PHYSICS
    //
    // This is intentionally recalculated on the fast path. It is
    // deterministic, lightweight and does not alter trend history.
    // ========================================================

    const float envAirTemp =
        dhtValid
            ? airTemperatureC
            : NAN;

    const float envHumidity =
        dhtValid
            ? humidityPct
            : NAN;

    const float envWaterTemp =
        ds18b20Valid
            ? waterTemperatureC
            : NAN;

    latestData.environment =
        EnvironmentalPhysics::calculate(
            envAirTemp,
            envHumidity,
            envWaterTemp);

    // ========================================================
    // TREND HISTORY
    //
    // A valid current measurement may be the retained last genuine
    // reading after a transient failed attempt. That value is usable
    // while fresh, but it MUST NOT be inserted again as a new temporal
    // sample. Source timestamps provide that distinction.
    // ========================================================

    if (captureTrendSamples)
    {
        const bool captureDHT22 =
            shouldCaptureSource(
                dhtValid,
                haveCapturedDHT22Source,
                sourceTimestamps.dht22Ms,
                lastCapturedDHT22SourceMs);

        if (captureDHT22)
        {
            airTemperatureTrend.addSample(
                airTemperatureC,
                sourceTimestamps.dht22Ms);

            humidityTrend.addSample(
                humidityPct,
                sourceTimestamps.dht22Ms);

            if (isfinite(
                    latestData.environment.vpdKPa))
            {
                vpdTrend.addSample(
                    latestData.environment.vpdKPa,
                    sourceTimestamps.dht22Ms);
            }

            haveCapturedDHT22Source =
                true;

            lastCapturedDHT22SourceMs =
                sourceTimestamps.dht22Ms;
        }

        const bool captureDS18B20 =
            shouldCaptureSource(
                ds18b20Valid,
                haveCapturedDS18B20Source,
                sourceTimestamps.ds18b20Ms,
                lastCapturedDS18B20SourceMs);

        if (captureDS18B20)
        {
            waterTemperatureTrend.addSample(
                waterTemperatureC,
                sourceTimestamps.ds18b20Ms);

            haveCapturedDS18B20Source =
                true;

            lastCapturedDS18B20SourceMs =
                sourceTimestamps.ds18b20Ms;
        }

        const bool captureReservoir =
            shouldCaptureSource(
                reservoirValid &&
                    isfinite(waterLevelPct),
                haveCapturedReservoirSource,
                sourceTimestamps.reservoirMs,
                lastCapturedReservoirSourceMs);

        if (captureReservoir)
        {
            waterLevelTrend.addSample(
                waterLevelPct,
                sourceTimestamps.reservoirMs);

            haveCapturedReservoirSource =
                true;

            lastCapturedReservoirSourceMs =
                sourceTimestamps.reservoirMs;
        }
    }

    // ========================================================
    // COPY TREND RESULTS INTO SNAPSHOT
    // ========================================================

    latestData.airTemperatureTrend =
        makeTrendMetrics(
            airTemperatureTrend);

    latestData.humidityTrend =
        makeTrendMetrics(
            humidityTrend);

    latestData.waterTemperatureTrend =
        makeTrendMetrics(
            waterTemperatureTrend);

    latestData.waterLevelTrend =
        makeTrendMetrics(
            waterLevelTrend);

    latestData.vpdTrend =
        makeTrendMetrics(
            vpdTrend);

    // ========================================================
    // AGGREGATE VALIDITY
    // ========================================================

    latestData.directSensorsValid =
        dhtValid &&
        ds18b20Valid &&
        ultrasonicValid;

    latestData.environmentalValid =
        latestData.environment.completeValid;

    latestData.completeSensorSetValid =
        latestData.directSensorsValid;

    latestData.valid =
        latestData.completeSensorSetValid;
}

// ============================================================
// GET CURRENT SNAPSHOT
// ============================================================

const C3AnalyticsData &
C3AnalyticsEngine::data() const
{
    return latestData;
}
