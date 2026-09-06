#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE
// GENERIC TIME-SERIES / TREND ANALYTICS
//
// RESPONSIBILITY:
//
// - Store timestamped numerical samples
// - Rolling mean
// - Minimum
// - Maximum
// - Range
// - Standard deviation
// - Latest change
// - Linear trend / slope
//
// DOES NOT KNOW ABOUT:
//
// - DHT22
// - DS18B20
// - HC-SR04
// - Reservoir
// - TDS
// - TinyML
// - ESP-NOW
//
// Therefore one instance can be used for ANY numerical signal.
// ============================================================

class SignalTrend
{
public:
    // --------------------------------------------------------
    // Maximum internal storage
    // --------------------------------------------------------

    static constexpr uint8_t MAX_SAMPLES = 30;

    // --------------------------------------------------------
    // Constructor
    //
    // windowSize:
    // number of rolling samples to retain
    // --------------------------------------------------------

    explicit SignalTrend(uint8_t windowSize);

    // --------------------------------------------------------
    // Reset all history
    // --------------------------------------------------------

    void reset();

    // --------------------------------------------------------
    // Add timestamped sample
    //
    // timestampMs normally comes from millis()
    //
    // Returns false if value is invalid.
    // --------------------------------------------------------

    bool addSample(
        float value,
        uint32_t timestampMs);

    // --------------------------------------------------------
    // Number of samples currently available
    // --------------------------------------------------------

    uint8_t sampleCount() const;

    // --------------------------------------------------------
    // Configured rolling window
    // --------------------------------------------------------

    uint8_t windowSize() const;

    // --------------------------------------------------------
    // Has complete rolling window been filled?
    // --------------------------------------------------------

    bool isReady() const;

    // --------------------------------------------------------
    // Latest value
    // --------------------------------------------------------

    float latest() const;

    // --------------------------------------------------------
    // Previous value
    // --------------------------------------------------------

    float previous() const;

    // --------------------------------------------------------
    // Latest - previous
    // --------------------------------------------------------

    float latestChange() const;

    // --------------------------------------------------------
    // Statistics
    // --------------------------------------------------------

    float mean() const;

    float minimum() const;

    float maximum() const;

    float range() const;

    float standardDeviation() const;

    // --------------------------------------------------------
    // Linear-regression slope
    //
    // Returned as units / hour.
    //
    // Examples:
    //
    // Temperature → C/hour
    // Humidity    → %/hour
    // Water level → %/hour
    // --------------------------------------------------------

    float slopePerHour() const;

private:
    float values[MAX_SAMPLES];

    uint32_t timestamps[MAX_SAMPLES];

    uint8_t configuredWindowSize;

    uint8_t count;

    uint8_t writeIndex;

    bool validSample(float value) const;

    uint8_t chronologicalIndex(
        uint8_t position) const;
};