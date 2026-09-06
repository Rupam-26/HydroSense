#pragma once

#include <Arduino.h>

#include "analytics/SignalTrend.h"
#include "models/C3AnalyticsData.h"
#include "physics/EnvironmentalPhysics.h"

// ============================================================
// HYDROSENSE
// C3 ANALYTICS ENGINE
//
// RESPONSIBILITY:
// - Combine already-validated measurements
// - Calculate environmental physics
// - Maintain rolling trends
// - Generate one unified analytical snapshot
//
// IMPORTANT REAL-TIME RULE:
// Current values may be refreshed more frequently than the trend
// sampling cadence. Trend history must therefore ingest only a NEW
// genuine source measurement, never a repeatedly held last-good value.
//
// DOES NOT:
// - Read sensors
// - Trigger HC-SR04
// - Access NVS
// - Perform reservoir calibration
// - Send BLE / ESP-NOW
// - Control OLED
// ============================================================

struct C3AnalyticsSourceTimestamps
{
    // Timestamp of the latest genuine successful DHT22 acquisition.
    uint32_t dht22Ms = 0;

    // Timestamp of the latest genuine successful DS18B20 acquisition.
    uint32_t ds18b20Ms = 0;

    // Timestamp of the latest genuine trusted sonar-derived reservoir
    // measurement used for the current water level.
    uint32_t reservoirMs = 0;
};

class C3AnalyticsEngine
{
public:
    explicit C3AnalyticsEngine(
        uint8_t trendWindowSize);

    // Reset trend history, source-deduplication state and current data.
    void reset();

    // Refresh one complete analytics snapshot.
    //
    // captureTrendSamples = false:
    //   Recompute current direct/environmental state immediately, but
    //   do not alter trend history. Used by the fast real-time path.
    //
    // captureTrendSamples = true:
    //   Also ingest trend samples, but only when the corresponding
    //   genuine source timestamp differs from the last captured one.
    //   This prevents held last-good values from being inserted again
    //   as fake new temporal samples.
    void update(
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
        bool captureTrendSamples);

    const C3AnalyticsData &data() const;

private:
    SignalTrend airTemperatureTrend;
    SignalTrend humidityTrend;
    SignalTrend waterTemperatureTrend;
    SignalTrend waterLevelTrend;
    SignalTrend vpdTrend;

    C3AnalyticsData latestData;

    // Source timestamp deduplication belongs here because this class
    // owns temporal trend history.
    bool haveCapturedDHT22Source;
    bool haveCapturedDS18B20Source;
    bool haveCapturedReservoirSource;

    uint32_t lastCapturedDHT22SourceMs;
    uint32_t lastCapturedDS18B20SourceMs;
    uint32_t lastCapturedReservoirSourceMs;

    TrendMetrics makeTrendMetrics(
        const SignalTrend &trend) const;

    bool shouldCaptureSource(
        bool sourceValid,
        bool alreadyCaptured,
        uint32_t sourceTimestampMs,
        uint32_t lastCapturedTimestampMs) const;
};
