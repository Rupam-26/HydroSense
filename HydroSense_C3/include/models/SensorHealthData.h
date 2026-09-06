#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE
// SENSOR / NODE HEALTH DATA MODEL
//
// DATA ONLY.
//
// No calculations.
// No hardware access.
// No communication.
// ============================================================

// ============================================================
// Individual sensor state
// ============================================================

enum class SensorState : uint8_t
{
    NEVER = 0,
    LIVE,
    STALE,
    FAULT
};

// ============================================================
// Individual sensor health snapshot
// ============================================================

struct SensorHealth
{
    SensorState state = SensorState::NEVER;

    bool valid = false;

    uint32_t lastValidTimestampMs = 0;

    uint32_t ageMs = 0;

    uint32_t successfulReads = 0;

    uint32_t failedReads = 0;

    uint32_t consecutiveFailures = 0;

    float availabilityPct = 0.0f;
};

// ============================================================
// Complete C3 node health
// ============================================================

struct NodeHealthData
{
    SensorHealth dht22{};
    SensorHealth ds18b20{};
    SensorHealth ultrasonic{};

    bool calibrationValid = false;

    bool reservoirValid = false;

    bool allCriticalSensorsValid = false;

    bool nodeHealthy = false;

    uint32_t uptimeMs = 0;
};