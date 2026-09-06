#include "analytics/SignalTrend.h"

#include <math.h>

// ============================================================
// Constructor
// ============================================================

SignalTrend::SignalTrend(uint8_t windowSize)
    : configuredWindowSize(windowSize),
      count(0),
      writeIndex(0)
{
    if (configuredWindowSize == 0)
    {
        configuredWindowSize = 1;
    }

    if (configuredWindowSize > MAX_SAMPLES)
    {
        configuredWindowSize = MAX_SAMPLES;
    }

    reset();
}

// ============================================================
// Reset
// ============================================================

void SignalTrend::reset()
{
    for (uint8_t i = 0;
         i < MAX_SAMPLES;
         i++)
    {
        values[i] = NAN;
        timestamps[i] = 0;
    }

    count = 0;
    writeIndex = 0;
}

// ============================================================
// Input validation
// ============================================================

bool SignalTrend::validSample(
    float value) const
{
    return isfinite(value);
}

// ============================================================
// Add sample
// ============================================================

bool SignalTrend::addSample(
    float value,
    uint32_t timestampMs)
{
    if (!validSample(value))
    {
        return false;
    }

    values[writeIndex] =
        value;

    timestamps[writeIndex] =
        timestampMs;

    writeIndex++;

    if (
        writeIndex >=
        configuredWindowSize)
    {
        writeIndex = 0;
    }

    if (
        count <
        configuredWindowSize)
    {
        count++;
    }

    return true;
}

// ============================================================
// Convert chronological position to circular-buffer index
//
// position:
// 0 = oldest
//
// count-1 = newest
// ============================================================

uint8_t SignalTrend::chronologicalIndex(
    uint8_t position) const
{
    if (count == 0)
    {
        return 0;
    }

    // Buffer not yet full.
    if (
        count <
        configuredWindowSize)
    {
        return position;
    }

    // writeIndex points to location where NEXT sample
    // will overwrite the oldest sample.
    return (
               writeIndex +
               position) %
           configuredWindowSize;
}

// ============================================================
// Sample count
// ============================================================

uint8_t SignalTrend::sampleCount() const
{
    return count;
}

// ============================================================
// Window size
// ============================================================

uint8_t SignalTrend::windowSize() const
{
    return configuredWindowSize;
}

// ============================================================
// Ready?
// ============================================================

bool SignalTrend::isReady() const
{
    return count >=
           configuredWindowSize;
}

// ============================================================
// Latest
// ============================================================

float SignalTrend::latest() const
{
    if (count == 0)
    {
        return NAN;
    }

    uint8_t index =
        chronologicalIndex(
            count - 1);

    return values[index];
}

// ============================================================
// Previous
// ============================================================

float SignalTrend::previous() const
{
    if (count < 2)
    {
        return NAN;
    }

    uint8_t index =
        chronologicalIndex(
            count - 2);

    return values[index];
}

// ============================================================
// Latest change
// ============================================================

float SignalTrend::latestChange() const
{
    float current =
        latest();

    float old =
        previous();

    if (
        !isfinite(current) ||
        !isfinite(old))
    {
        return NAN;
    }

    return current - old;
}

// ============================================================
// Mean
// ============================================================

float SignalTrend::mean() const
{
    if (count == 0)
    {
        return NAN;
    }

    float sum = 0.0f;

    for (uint8_t i = 0;
         i < count;
         i++)
    {
        uint8_t index =
            chronologicalIndex(i);

        sum +=
            values[index];
    }

    return sum /
           static_cast<float>(count);
}

// ============================================================
// Minimum
// ============================================================

float SignalTrend::minimum() const
{
    if (count == 0)
    {
        return NAN;
    }

    float result =
        values[chronologicalIndex(0)];

    for (uint8_t i = 1;
         i < count;
         i++)
    {
        float value =
            values[chronologicalIndex(i)];

        if (value < result)
        {
            result = value;
        }
    }

    return result;
}

// ============================================================
// Maximum
// ============================================================

float SignalTrend::maximum() const
{
    if (count == 0)
    {
        return NAN;
    }

    float result =
        values[chronologicalIndex(0)];

    for (uint8_t i = 1;
         i < count;
         i++)
    {
        float value =
            values[chronologicalIndex(i)];

        if (value > result)
        {
            result = value;
        }
    }

    return result;
}

// ============================================================
// Range
// ============================================================

float SignalTrend::range() const
{
    float minValue =
        minimum();

    float maxValue =
        maximum();

    if (
        !isfinite(minValue) ||
        !isfinite(maxValue))
    {
        return NAN;
    }

    return maxValue -
           minValue;
}

// ============================================================
// Population standard deviation
// ============================================================

float SignalTrend::standardDeviation() const
{
    if (count == 0)
    {
        return NAN;
    }

    float average =
        mean();

    if (!isfinite(average))
    {
        return NAN;
    }

    float squaredDifferenceSum =
        0.0f;

    for (uint8_t i = 0;
         i < count;
         i++)
    {
        float value =
            values[chronologicalIndex(i)];

        float difference =
            value -
            average;

        squaredDifferenceSum +=
            difference *
            difference;
    }

    float variance =
        squaredDifferenceSum /
        static_cast<float>(count);

    return sqrtf(variance);
}

// ============================================================
// Linear regression slope
//
// X = elapsed time in hours
// Y = measured signal
//
// Result = signal units/hour
// ============================================================

float SignalTrend::slopePerHour() const
{
    if (count < 2)
    {
        return NAN;
    }

    uint8_t firstIndex =
        chronologicalIndex(0);

    uint32_t baseTime =
        timestamps[firstIndex];

    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumXY = 0.0f;
    float sumXX = 0.0f;

    for (uint8_t i = 0;
         i < count;
         i++)
    {
        uint8_t index =
            chronologicalIndex(i);

        uint32_t elapsedMs =
            timestamps[index] -
            baseTime;

        // milliseconds → hours
        float x =
            static_cast<float>(
                elapsedMs) /
            3600000.0f;

        float y =
            values[index];

        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
    }

    float n =
        static_cast<float>(count);

    float denominator =
        (n * sumXX) -
        (sumX * sumX);

    if (
        fabsf(denominator) <
        0.0000001f)
    {
        return NAN;
    }

    float numerator =
        (n * sumXY) -
        (sumX * sumY);

    return numerator /
           denominator;
}