#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE
// HC-SR04 ULTRASONIC SENSOR DRIVER
//
// RESPONSIBILITY:
// - Generate trigger pulse
// - Measure echo duration
// - Detect timeout
// - Calculate RAW distance
// - Report hardware health
//
// DOES NOT HANDLE:
// - Temperature correction
// - Filtering / averaging
// - Reservoir depth
// - Water level
// - Volume
// - Calibration
// ============================================================

class UltrasonicSensor
{
public:
    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------

    UltrasonicSensor(
        uint8_t triggerPin,
        uint8_t echoPin);

    // --------------------------------------------------------
    // Initialize hardware
    // --------------------------------------------------------

    void begin();

    // --------------------------------------------------------
    // Perform ONE genuine ultrasonic measurement
    //
    // Returns:
    // true  -> valid echo received
    // false -> timeout / invalid measurement
    // --------------------------------------------------------

    bool read();

    // --------------------------------------------------------
    // Latest genuine echo duration
    // --------------------------------------------------------

    uint32_t echoTimeUs() const;

    // --------------------------------------------------------
    // Latest raw nominal distance in centimeters
    // --------------------------------------------------------

    float rawDistanceCm() const;

    // --------------------------------------------------------
    // Current sensor health
    // --------------------------------------------------------

    bool isHealthy() const;

    // --------------------------------------------------------
    // Number of successful measurements
    // --------------------------------------------------------

    uint32_t successfulReads() const;

    // --------------------------------------------------------
    // Number of failed measurements
    // --------------------------------------------------------

    uint32_t failedReads() const;

private:
    // --------------------------------------------------------
    // GPIO configuration
    // --------------------------------------------------------

    uint8_t trigPin;
    uint8_t echoPin;

    // --------------------------------------------------------
    // Latest genuine measurement
    // --------------------------------------------------------

    uint32_t lastEchoTimeUs;
    float lastRawDistanceCm;
    bool healthy;

    // --------------------------------------------------------
    // Diagnostic counters
    // --------------------------------------------------------

    uint32_t successCount;
    uint32_t failureCount;

    // --------------------------------------------------------
    // Internal physical validation
    // --------------------------------------------------------

    bool validEcho(uint32_t durationUs) const;
};
