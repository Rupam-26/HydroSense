#pragma once

#include <Arduino.h>

#include "models/SensorHealthData.h"

// ============================================================
// HYDROSENSE
// SENSOR HEALTH MANAGER
//
// RESPONSIBILITY:
// - Track successful / failed physical sensor attempts
// - Preserve the timestamp of the last genuine valid sample
// - Track consecutive failures and long-run availability
// - Classify each sensor as NEVER / LIVE / STALE / FAULT
// - Build one node-health snapshot
//
// IMPORTANT DESIGN RULE:
// Health is freshness-based, not "last call returned true" based.
// A single transient bus/CRC miss does not instantly destroy a
// still-fresh genuine measurement. This restores the proven first
// testing-phase health behaviour while retaining the newer counters.
// ============================================================

struct SensorHealthPolicy
{
    uint32_t liveTimeoutMs;
    uint32_t faultTimeoutMs;

    // Explicit constexpr constructor keeps this type compatible with
    // the ESP32 Arduino / PlatformIO GNU++11 toolchain while still
    // allowing compile-time policy declarations such as:
    //
    // constexpr SensorHealthPolicy POLICY{5000UL, 15000UL};
    constexpr SensorHealthPolicy(
        uint32_t liveTimeout = 0,
        uint32_t faultTimeout = 0)
        : liveTimeoutMs(liveTimeout),
          faultTimeoutMs(faultTimeout)
    {
    }
};

class SensorHealthManager
{
public:
    SensorHealthManager(
        SensorHealthPolicy dht22Policy,
        SensorHealthPolicy ds18b20Policy,
        SensorHealthPolicy ultrasonicPolicy);

    void reset();

    void updateDHT22(
        bool valid,
        uint32_t timestampMs);

    void updateDS18B20(
        bool valid,
        uint32_t timestampMs);

    void updateUltrasonic(
        bool valid,
        uint32_t timestampMs);

    NodeHealthData snapshot(
        uint32_t nowMs,
        bool calibrationValid,
        bool reservoirValid) const;

private:
    struct InternalHealth
    {
        bool everValid = false;
        bool lastReadValid = false;

        uint32_t lastValidTimestampMs = 0;

        uint32_t successfulReads = 0;
        uint32_t failedReads = 0;
        uint32_t consecutiveFailures = 0;
    };

    SensorHealthPolicy dht22Timing;
    SensorHealthPolicy ds18b20Timing;
    SensorHealthPolicy ultrasonicTiming;

    InternalHealth dht22Health;
    InternalHealth ds18b20Health;
    InternalHealth ultrasonicHealth;

    static SensorHealthPolicy sanitizePolicy(
        SensorHealthPolicy policy);

    void updateSensor(
        InternalHealth &health,
        bool valid,
        uint32_t timestampMs);

    SensorHealth buildSensorHealth(
        const InternalHealth &health,
        const SensorHealthPolicy &policy,
        uint32_t nowMs) const;
};
