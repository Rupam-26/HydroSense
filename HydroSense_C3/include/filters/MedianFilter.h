#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE
// GENERIC ROLLING SAMPLE FILTER
//
// RESPONSIBILITY:
// - Accept only finite floating-point samples
// - Maintain a rolling genuine-sample window
// - Calculate arithmetic mean
// - Calculate median
// - Calculate a robust trimmed mean
//
// The class name is retained for source compatibility with the
// current HydroSense project. The original median operation is
// still available; Block 4 now uses trimmedMean() for the HC-SR04.
//
// DOES NOT KNOW ABOUT:
// - HC-SR04 hardware
// - DHT22 / DS18B20
// - Reservoir calibration
// - BLE / ESP-NOW
// ============================================================

class MedianFilter
{
public:
    explicit MedianFilter(uint8_t windowSize);

    // Remove every stored sample and restart warm-up.
    void reset();

    // Add one genuine finite sample to the rolling window.
    bool addSample(float value);

    // Arithmetic mean of all currently stored samples.
    // Returns NAN when no sample exists.
    float mean() const;

    // Median retained for compatibility and diagnostic use.
    // Returns NAN when no sample exists.
    float median() const;

    // Robust average used by the final HC-SR04 pipeline.
    //
    // count >= 3:
    //   sort -> remove one minimum + one maximum -> average rest
    //
    // count < 3:
    //   falls back to the ordinary mean.
    //
    // No synthetic samples are inserted.
    float trimmedMean() const;

    uint8_t sampleCount() const;

    bool isReady() const;

    uint8_t windowSize() const;

private:
    static constexpr uint8_t MAX_WINDOW_SIZE = 15;

    float samples[MAX_WINDOW_SIZE];

    uint8_t configuredWindowSize;
    uint8_t count;
    uint8_t writeIndex;

    bool validSample(float value) const;

    // Copy the active window and sort it in ascending order.
    void copySorted(float *destination) const;
};
