// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <math.h>
#include <stdint.h>

// ============================================================
// HYDROSENSE ESP32-S3
// SENSOR SNAPSHOT DATA MODEL
//
// Represents one CRC-validated telemetry packet received
// from the ESP32-C3.
//
// C3 BLE Packet
//      ↓
// S3SensorSnapshot
//      ↓
// S3FeatureBuilder
//      ↓
// S3FeatureData
//      ├── LCD
//      ├── TinyML
//      └── Cloud
// ============================================================

struct S3SensorSnapshot
{
    uint8_t sequence = 0;
    uint32_t receivedAtMs = 0;

    float airTemperatureC = NAN;
    float humidityPct = NAN;
    float waterTemperatureC = NAN;
    float stableDistanceCm = NAN;
    float waterLevelPct = NAN;

    float qualityScorePct = 0.0f;
    uint16_t qualityFlags = 0;

    bool dht22Valid = false;
    bool ds18b20Valid = false;
    bool ultrasonicValid = false;
    bool reservoirValid = false;
    bool calibrationValid = false;

    // True only after packet length, magic, version and CRC validation.
    bool packetValid = false;

    bool airEnvironmentAvailable() const
    {
        return packetValid &&
               dht22Valid &&
               isfinite(airTemperatureC) &&
               airTemperatureC >= -40.0f &&
               airTemperatureC <= 80.0f &&
               isfinite(humidityPct) &&
               humidityPct >= 0.0f &&
               humidityPct <= 100.0f;
    }

    bool waterTemperatureAvailable() const
    {
        return packetValid &&
               ds18b20Valid &&
               isfinite(waterTemperatureC) &&
               waterTemperatureC >= -55.0f &&
               waterTemperatureC <= 125.0f;
    }

    bool distanceAvailable() const
    {
        return packetValid &&
               ultrasonicValid &&
               isfinite(stableDistanceCm) &&
               stableDistanceCm >= 2.0f &&
               stableDistanceCm <= 400.0f;
    }

    bool waterLevelAvailable() const
    {
        return packetValid &&
               reservoirValid &&
               calibrationValid &&
               isfinite(waterLevelPct) &&
               waterLevelPct >= 0.0f &&
               waterLevelPct <= 100.0f;
    }

    bool coreSensorsAvailable() const
    {
        return airEnvironmentAvailable() &&
               waterTemperatureAvailable() &&
               distanceAvailable();
    }

    uint32_t ageMs(uint32_t nowMs) const
    {
        if (!packetValid)
        {
            return UINT32_MAX;
        }

        return nowMs - receivedAtMs;
    }

    bool isStale(
        uint32_t nowMs,
        uint32_t maximumAgeMs) const
    {
        if (!packetValid)
        {
            return true;
        }

        return ageMs(nowMs) > maximumAgeMs;
    }

    void clear()
    {
        sequence = 0;
        receivedAtMs = 0;

        airTemperatureC = NAN;
        humidityPct = NAN;
        waterTemperatureC = NAN;
        stableDistanceCm = NAN;
        waterLevelPct = NAN;

        qualityScorePct = 0.0f;
        qualityFlags = 0;

        dht22Valid = false;
        ds18b20Valid = false;
        ultrasonicValid = false;
        reservoirValid = false;
        calibrationValid = false;

        packetValid = false;
    }
};