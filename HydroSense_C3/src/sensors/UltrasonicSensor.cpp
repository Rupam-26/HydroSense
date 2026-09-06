#include "sensors/UltrasonicSensor.h"

#include <math.h>

// ============================================================
// HYDROSENSE HC-SR04 HARDWARE ACQUISITION
//
// FIRST-TESTING-PHASE OWNERSHIP MODEL:
//
// This driver performs hardware acquisition only:
//
//   trigger one real HC-SR04 ping
//          ↓
//   measure one real echo
//          ↓
//   validate the physical echo
//          ↓
//   expose raw echo time + raw distance
//
// Temperature correction, rolling sampling, outlier rejection,
// averaging, stability logic, reservoir calculations, health /
// quality integration and BLE publication remain in their
// dedicated higher-level layers.
//
// No sensor reading is simulated, substituted or hardcoded.
// ============================================================

namespace
{
    constexpr uint32_t ECHO_TIMEOUT_US =
        30000UL;

    constexpr float NOMINAL_SOUND_SPEED_CM_PER_US =
        0.0343f;

    // Practical HC-SR04 operating range.
    constexpr float MIN_DISTANCE_CM =
        2.0f;

    constexpr float MAX_DISTANCE_CM =
        400.0f;
}

UltrasonicSensor::UltrasonicSensor(
    uint8_t triggerPin,
    uint8_t echoPin)
    : trigPin(triggerPin),
      echoPin(echoPin),
      lastEchoTimeUs(0),
      lastRawDistanceCm(NAN),
      healthy(false),
      successCount(0),
      failureCount(0)
{
}

void UltrasonicSensor::begin()
{
    pinMode(
        trigPin,
        OUTPUT);

    pinMode(
        echoPin,
        INPUT);

    digitalWrite(
        trigPin,
        LOW);

    delayMicroseconds(
        5);

    lastEchoTimeUs =
        0;

    lastRawDistanceCm =
        NAN;

    healthy =
        false;

    successCount =
        0;

    failureCount =
        0;
}

bool UltrasonicSensor::validEcho(
    uint32_t durationUs) const
{
    if (
        durationUs == 0 ||
        durationUs >= ECHO_TIMEOUT_US)
    {
        return false;
    }

    const float distanceCm =
        (static_cast<float>(
             durationUs) *
         NOMINAL_SOUND_SPEED_CM_PER_US) /
        2.0f;

    return isfinite(distanceCm) &&
           distanceCm >= MIN_DISTANCE_CM &&
           distanceCm <= MAX_DISTANCE_CM;
}

bool UltrasonicSensor::read()
{
    // --------------------------------------------------------
    // One genuine HC-SR04 trigger pulse.
    // This is the clean acquisition strategy from the proven
    // first testing phase. Repeated temporal sampling belongs
    // to the scheduler / filter layer, not inside this driver.
    // --------------------------------------------------------

    digitalWrite(
        trigPin,
        LOW);

    delayMicroseconds(
        2);

    digitalWrite(
        trigPin,
        HIGH);

    delayMicroseconds(
        10);

    digitalWrite(
        trigPin,
        LOW);

    const uint32_t durationUs =
        pulseIn(
            echoPin,
            HIGH,
            ECHO_TIMEOUT_US);

    // --------------------------------------------------------
    // Validate the newly acquired physical echo.
    // --------------------------------------------------------

    if (
        !validEcho(
            durationUs))
    {
        // Preserve the last genuine successful reading in the
        // getters. The false return tells the higher layer that
        // this particular physical acquisition failed.
        healthy =
            false;

        ++failureCount;

        return false;
    }

    // --------------------------------------------------------
    // Nominal raw distance.
    //
    // Temperature-corrected distance is intentionally NOT
    // calculated here; UltrasonicPhysics owns that operation.
    // --------------------------------------------------------

    const float distanceCm =
        (static_cast<float>(
             durationUs) *
         NOMINAL_SOUND_SPEED_CM_PER_US) /
        2.0f;

    lastEchoTimeUs =
        durationUs;

    lastRawDistanceCm =
        distanceCm;

    healthy =
        true;

    ++successCount;

    return true;
}

uint32_t UltrasonicSensor::echoTimeUs() const
{
    return lastEchoTimeUs;
}

float UltrasonicSensor::rawDistanceCm() const
{
    return lastRawDistanceCm;
}

bool UltrasonicSensor::isHealthy() const
{
    return healthy;
}

uint32_t UltrasonicSensor::successfulReads() const
{
    return successCount;
}

uint32_t UltrasonicSensor::failedReads() const
{
    return failureCount;
}
