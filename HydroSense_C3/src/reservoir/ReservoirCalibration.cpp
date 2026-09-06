#include "reservoir/ReservoirCalibration.h"

// ============================================================
// NVS CONFIGURATION
// ============================================================

static constexpr const char *NVS_NAMESPACE =
    "hydro-cal";

static constexpr const char *KEY_FULL_DISTANCE =
    "full_cm";

static constexpr const char *KEY_EMPTY_DISTANCE =
    "empty_cm";

// ============================================================
// Constructor
// ============================================================

ReservoirCalibration::ReservoirCalibration()
    : fullDistance(NAN),
      emptyDistance(NAN),
      fullAvailable(false),
      emptyAvailable(false)
{
}

// ============================================================
// Validate one calibration distance
// ============================================================

bool ReservoirCalibration::validDistance(
    float distanceCm) const
{
    if (!isfinite(distanceCm))
    {
        return false;
    }

    if (distanceCm <= 0.0f)
    {
        return false;
    }

    return true;
}

// ============================================================
// Validate FULL / EMPTY relationship
//
// Sensor is mounted above reservoir:
//
// FULL water:
// sensor is closer to water
//
// EMPTY reservoir:
// sensor is farther from bottom
//
// Therefore:
//
// FULL_DISTANCE < EMPTY_DISTANCE
// ============================================================

bool ReservoirCalibration::validateCalibration() const
{
    if (!fullAvailable ||
        !emptyAvailable)
    {
        return false;
    }

    if (!validDistance(fullDistance) ||
        !validDistance(emptyDistance))
    {
        return false;
    }

    if (fullDistance >= emptyDistance)
    {
        return false;
    }

    return true;
}

// ============================================================
// Begin / load from NVS
// ============================================================

bool ReservoirCalibration::begin()
{
    if (!preferences.begin(
            NVS_NAMESPACE,
            false))
    {
        fullDistance = NAN;
        emptyDistance = NAN;

        fullAvailable = false;
        emptyAvailable = false;

        return false;
    }

    // --------------------------------------------------------
    // FULL
    // --------------------------------------------------------

    if (preferences.isKey(
            KEY_FULL_DISTANCE))
    {
        float storedFull =
            preferences.getFloat(
                KEY_FULL_DISTANCE,
                NAN);

        if (validDistance(storedFull))
        {
            fullDistance =
                storedFull;

            fullAvailable =
                true;
        }
    }

    // --------------------------------------------------------
    // EMPTY
    // --------------------------------------------------------

    if (preferences.isKey(
            KEY_EMPTY_DISTANCE))
    {
        float storedEmpty =
            preferences.getFloat(
                KEY_EMPTY_DISTANCE,
                NAN);

        if (validDistance(storedEmpty))
        {
            emptyDistance =
                storedEmpty;

            emptyAvailable =
                true;
        }
    }

    return true;
}

// ============================================================
// Store FULL calibration
// ============================================================

bool ReservoirCalibration::setFullDistance(
    float distanceCm)
{
    if (!validDistance(distanceCm))
    {
        return false;
    }

    size_t bytesWritten =
        preferences.putFloat(
            KEY_FULL_DISTANCE,
            distanceCm);

    if (bytesWritten == 0)
    {
        return false;
    }

    fullDistance =
        distanceCm;

    fullAvailable =
        true;

    return true;
}

// ============================================================
// Store EMPTY calibration
// ============================================================

bool ReservoirCalibration::setEmptyDistance(
    float distanceCm)
{
    if (!validDistance(distanceCm))
    {
        return false;
    }

    size_t bytesWritten =
        preferences.putFloat(
            KEY_EMPTY_DISTANCE,
            distanceCm);

    if (bytesWritten == 0)
    {
        return false;
    }

    emptyDistance =
        distanceCm;

    emptyAvailable =
        true;

    return true;
}

// ============================================================
// Clear calibration
// ============================================================

bool ReservoirCalibration::clear()
{
    bool success =
        preferences.clear();

    if (!success)
    {
        return false;
    }

    fullDistance =
        NAN;

    emptyDistance =
        NAN;

    fullAvailable =
        false;

    emptyAvailable =
        false;

    return true;
}

// ============================================================
// Calibration validity
// ============================================================

bool ReservoirCalibration::isValid() const
{
    return validateCalibration();
}

// ============================================================
// FULL availability
// ============================================================

bool ReservoirCalibration::hasFullDistance() const
{
    return fullAvailable;
}

// ============================================================
// EMPTY availability
// ============================================================

bool ReservoirCalibration::hasEmptyDistance() const
{
    return emptyAvailable;
}

// ============================================================
// Get FULL distance
// ============================================================

float ReservoirCalibration::fullDistanceCm() const
{
    if (!fullAvailable)
    {
        return NAN;
    }

    return fullDistance;
}

// ============================================================
// Get EMPTY distance
// ============================================================

float ReservoirCalibration::emptyDistanceCm() const
{
    if (!emptyAvailable)
    {
        return NAN;
    }

    return emptyDistance;
}

// ============================================================
// Calculate usable reservoir depth
// ============================================================

float ReservoirCalibration::usableDepthCm() const
{
    if (!validateCalibration())
    {
        return NAN;
    }

    return emptyDistance -
           fullDistance;
}