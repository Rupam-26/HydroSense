#include "filters/MedianFilter.h"

#include <math.h>

// ============================================================
// Constructor
// ============================================================

MedianFilter::MedianFilter(uint8_t windowSize)
    : configuredWindowSize(windowSize),
      count(0),
      writeIndex(0)
{
    if (configuredWindowSize == 0)
    {
        configuredWindowSize = 1;
    }

    if (configuredWindowSize > MAX_WINDOW_SIZE)
    {
        configuredWindowSize = MAX_WINDOW_SIZE;
    }

    reset();
}

// ============================================================
// Reset
// ============================================================

void MedianFilter::reset()
{
    for (uint8_t i = 0;
         i < MAX_WINDOW_SIZE;
         ++i)
    {
        samples[i] = NAN;
    }

    count = 0;
    writeIndex = 0;
}

// ============================================================
// Input validation
// ============================================================

bool MedianFilter::validSample(float value) const
{
    return isfinite(value);
}

// ============================================================
// Add one genuine sample
// ============================================================

bool MedianFilter::addSample(float value)
{
    if (!validSample(value))
    {
        return false;
    }

    samples[writeIndex] = value;

    ++writeIndex;

    if (writeIndex >= configuredWindowSize)
    {
        writeIndex = 0;
    }

    if (count < configuredWindowSize)
    {
        ++count;
    }

    return true;
}

// ============================================================
// Arithmetic mean
// ============================================================

float MedianFilter::mean() const
{
    if (count == 0)
    {
        return NAN;
    }

    double sum = 0.0;

    for (uint8_t i = 0;
         i < count;
         ++i)
    {
        sum += static_cast<double>(
            samples[i]);
    }

    return static_cast<float>(
        sum /
        static_cast<double>(count));
}

// ============================================================
// Copy + sort active samples
// ============================================================

void MedianFilter::copySorted(
    float *destination) const
{
    if (destination == nullptr)
    {
        return;
    }

    for (uint8_t i = 0;
         i < count;
         ++i)
    {
        destination[i] =
            samples[i];
    }

    // Insertion sort is deterministic and efficient for the small
    // fixed windows used by HydroSense.
    for (uint8_t i = 1;
         i < count;
         ++i)
    {
        const float key =
            destination[i];

        int8_t j =
            static_cast<int8_t>(i) - 1;

        while (j >= 0 &&
               destination[j] > key)
        {
            destination[j + 1] =
                destination[j];

            --j;
        }

        destination[j + 1] =
            key;
    }
}

// ============================================================
// Median
// ============================================================

float MedianFilter::median() const
{
    if (count == 0)
    {
        return NAN;
    }

    float sorted[MAX_WINDOW_SIZE];
    copySorted(sorted);

    if ((count % 2U) == 1U)
    {
        return sorted[count / 2U];
    }

    const uint8_t upperIndex =
        count / 2U;

    const uint8_t lowerIndex =
        upperIndex - 1U;

    return (sorted[lowerIndex] +
            sorted[upperIndex]) /
           2.0f;
}

// ============================================================
// Robust trimmed mean
// ============================================================

float MedianFilter::trimmedMean() const
{
    if (count == 0)
    {
        return NAN;
    }

    if (count < 3)
    {
        return mean();
    }

    float sorted[MAX_WINDOW_SIZE];
    copySorted(sorted);

    // Remove exactly one low extreme and one high extreme.
    // For the final 7-sample HC-SR04 window this averages the five
    // central genuine corrected measurements.
    const uint8_t firstIndex = 1;
    const uint8_t lastExclusive =
        count - 1U;

    double sum = 0.0;

    for (uint8_t i = firstIndex;
         i < lastExclusive;
         ++i)
    {
        sum += static_cast<double>(
            sorted[i]);
    }

    const uint8_t usedCount =
        lastExclusive - firstIndex;

    if (usedCount == 0)
    {
        return NAN;
    }

    return static_cast<float>(
        sum /
        static_cast<double>(usedCount));
}

// ============================================================
// State accessors
// ============================================================

uint8_t MedianFilter::sampleCount() const
{
    return count;
}

bool MedianFilter::isReady() const
{
    return count >= configuredWindowSize;
}

uint8_t MedianFilter::windowSize() const
{
    return configuredWindowSize;
}
