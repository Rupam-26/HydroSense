#include "physics/UltrasonicPhysics.h"

#include <math.h>

namespace
{
    // Same physical HC-SR04 operating bounds used by the hardware
    // driver and by the confirmed first-testing implementation.
    constexpr float MIN_DISTANCE_CM = 2.0f;
    constexpr float MAX_DISTANCE_CM = 400.0f;
}

// ============================================================
// Validate air temperature
// ============================================================

bool UltrasonicPhysics::validAirTemperature(
    float airTemperatureC)
{
    if (!isfinite(airTemperatureC))
    {
        return false;
    }

    // DHT22 specified physical measurement range.
    if (airTemperatureC < -40.0f ||
        airTemperatureC > 80.0f)
    {
        return false;
    }

    return true;
}

// ============================================================
// Calculate speed of sound
//
// c = 331.3 + (0.606 x T)
// ============================================================

float UltrasonicPhysics::speedOfSoundMps(
    float airTemperatureC)
{
    if (!validAirTemperature(
            airTemperatureC))
    {
        return NAN;
    }

    return 331.3f +
           (0.606f * airTemperatureC);
}

// ============================================================
// Temperature-corrected ultrasonic distance
//
// distance_cm = echo_us * speed_m_s / 20000
// ============================================================

float UltrasonicPhysics::correctedDistanceCm(
    uint32_t echoTimeUs,
    float airTemperatureC)
{
    if (echoTimeUs == 0)
    {
        return NAN;
    }

    const float speedMps =
        speedOfSoundMps(
            airTemperatureC);

    if (!isfinite(speedMps))
    {
        return NAN;
    }

    const float distanceCm =
        (static_cast<float>(echoTimeUs) *
         speedMps) /
        20000.0f;

    // Restore the same corrected-distance range guard used in the
    // first confirmed test implementation. A temperature correction
    // must not convert a physically acceptable nominal echo into an
    // out-of-range value that is then treated as valid.
    if (!isfinite(distanceCm) ||
        distanceCm < MIN_DISTANCE_CM ||
        distanceCm > MAX_DISTANCE_CM)
    {
        return NAN;
    }

    return distanceCm;
}
