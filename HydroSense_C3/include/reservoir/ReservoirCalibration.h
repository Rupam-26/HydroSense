#pragma once

#include <Arduino.h>
#include <Preferences.h>

// ============================================================
// HYDROSENSE
// RESERVOIR CALIBRATION MANAGER
//
// RESPONSIBILITY:
// - Store FULL calibration distance
// - Store EMPTY calibration distance
// - Persist calibration in ESP32 NVS
// - Validate calibration
// - Provide usable reservoir depth
//
// DOES NOT:
// - Read ultrasonic sensor
// - Filter distance
// - Calculate current water level
// - Calculate volume
// - Handle OLED
// - Handle ESP-NOW
// ============================================================

class ReservoirCalibration
{
public:
    ReservoirCalibration();

    // --------------------------------------------------------
    // Load saved calibration from ESP32 NVS
    // --------------------------------------------------------

    bool begin();

    // --------------------------------------------------------
    // Save current measured distance as FULL position
    // --------------------------------------------------------

    bool setFullDistance(float distanceCm);

    // --------------------------------------------------------
    // Save current measured distance as EMPTY position
    // --------------------------------------------------------

    bool setEmptyDistance(float distanceCm);

    // --------------------------------------------------------
    // Remove all calibration data
    // --------------------------------------------------------

    bool clear();

    // --------------------------------------------------------
    // Calibration state
    //
    // Valid only when:
    //
    // FULL distance < EMPTY distance
    // --------------------------------------------------------

    bool isValid() const;

    bool hasFullDistance() const;

    bool hasEmptyDistance() const;

    // --------------------------------------------------------
    // Stored distances
    // --------------------------------------------------------

    float fullDistanceCm() const;

    float emptyDistanceCm() const;

    // --------------------------------------------------------
    // Physical usable reservoir depth
    //
    // EMPTY - FULL
    //
    // Returns NAN if calibration is invalid.
    // --------------------------------------------------------

    float usableDepthCm() const;

private:
    Preferences preferences;

    float fullDistance;
    float emptyDistance;

    bool fullAvailable;
    bool emptyAvailable;

    bool validDistance(float distanceCm) const;

    bool validateCalibration() const;
};