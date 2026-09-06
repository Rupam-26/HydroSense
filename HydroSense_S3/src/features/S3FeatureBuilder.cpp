#include "features/S3FeatureBuilder.h"

#include <math.h>

// ============================================================
// CONSTANTS
// ============================================================

// Magnus equation constants for water vapour.
//
// These are physical-model coefficients,
// NOT hardcoded sensor measurements.

namespace
{
    // Keep the S3 derived-physics model numerically aligned with
    // the proven C3 EnvironmentalPhysics implementation.
    constexpr float MAGNUS_A =
        17.625f;

    constexpr float MAGNUS_B_C =
        243.04f;

    constexpr float SVP_A_KPA =
        0.6108f;

    constexpr float SVP_B =
        17.27f;

    constexpr float SVP_C_C =
        237.3f;

    // Absolute humidity relation:
    //
    // AH = 216.7 * e_hPa / T_K
    //
    // Since:
    //
    // e_hPa = e_kPa * 10
    //
    // AH = 2167 * e_kPa / T_K

    constexpr float ABS_HUMIDITY_CONSTANT =
        2167.0f;

    constexpr float KELVIN_OFFSET =
        273.15f;
}

// ============================================================
// BUILD FEATURE SET
// ============================================================

S3FeatureData S3FeatureBuilder::build(
    const S3SensorSnapshot &snapshot,
    uint32_t nowMs) const
{
    S3FeatureData features;

    features.clear();

    // ========================================================
    // PACKET MUST ALREADY BE VALIDATED
    // ========================================================

    if (
        !snapshot.packetValid)
    {
        return features;
    }

    // ========================================================
    // SOURCE INFORMATION
    // ========================================================

    features.sourceSequence =
        snapshot.sequence;

    features.generatedAtMs =
        nowMs;

    // ========================================================
    // COPY QUALITY INFORMATION
    // ========================================================

    features.qualityScorePct =
        snapshot.qualityScorePct;

    features.qualityFlags =
        snapshot.qualityFlags;

    // ========================================================
    // COPY SENSOR VALIDITY
    // ========================================================

    features.airEnvironmentValid =
        snapshot.airEnvironmentAvailable();

    features.waterTemperatureValid =
        snapshot.waterTemperatureAvailable();

    features.ultrasonicValid =
        snapshot.distanceAvailable();

    features.reservoirValid =
        snapshot.reservoirValid;

    features.calibrationValid =
        snapshot.calibrationValid;

    // ========================================================
    // COPY AIR DATA
    // ========================================================

    if (
        features.airEnvironmentValid)
    {
        features.airTemperatureC =
            snapshot.airTemperatureC;

        features.humidityPct =
            snapshot.humidityPct;
    }

    // ========================================================
    // COPY WATER TEMPERATURE
    // ========================================================

    if (
        features.waterTemperatureValid)
    {
        features.waterTemperatureC =
            snapshot.waterTemperatureC;
    }

    // ========================================================
    // COPY ULTRASONIC DATA
    // ========================================================

    if (
        features.ultrasonicValid)
    {
        features.stableDistanceCm =
            snapshot.stableDistanceCm;

        features.distanceAvailable =
            true;
    }

    // ========================================================
    // COPY RESERVOIR DATA
    // ========================================================

    if (
        snapshot.waterLevelAvailable())
    {
        features.waterLevelPct =
            snapshot.waterLevelPct;

        features.waterLevelAvailable =
            true;
    }

    // ========================================================
    // ENVIRONMENTAL FEATURE ENGINEERING
    // ========================================================

    if (
        features.airEnvironmentValid &&
        isfinite(
            features.airTemperatureC) &&
        isfinite(
            features.humidityPct) &&
        features.humidityPct > 0.0f &&
        features.humidityPct <= 100.0f)
    {
        // ----------------------------------------------------
        // Dew point
        // ----------------------------------------------------

        features.dewPointC =
            calculateDewPointC(
                features.airTemperatureC,
                features.humidityPct);

        // ----------------------------------------------------
        // Saturation vapour pressure
        // ----------------------------------------------------

        features.saturationVaporPressureKPa =
            calculateSaturationVaporPressureKPa(
                features.airTemperatureC);

        // ----------------------------------------------------
        // Actual vapour pressure
        // ----------------------------------------------------

        features.actualVaporPressureKPa =
            calculateActualVaporPressureKPa(
                features.saturationVaporPressureKPa,
                features.humidityPct);

        // ----------------------------------------------------
        // Vapour pressure deficit
        // ----------------------------------------------------

        features.vpdKPa =
            calculateVpdKPa(
                features.saturationVaporPressureKPa,
                features.actualVaporPressureKPa);

        // ----------------------------------------------------
        // Absolute humidity
        // ----------------------------------------------------

        features.absoluteHumidityGm3 =
            calculateAbsoluteHumidityGm3(
                features.airTemperatureC,
                features.actualVaporPressureKPa);

        features.environmentalFeaturesValid =
            isfinite(
                features.dewPointC) &&
            isfinite(
                features.saturationVaporPressureKPa) &&
            isfinite(
                features.actualVaporPressureKPa) &&
            isfinite(
                features.vpdKPa) &&
            isfinite(
                features.absoluteHumidityGm3);
    }

    // ========================================================
    // AIR ↔ WATER THERMAL FEATURES
    // ========================================================

    if (
        features.airEnvironmentValid &&
        features.waterTemperatureValid &&
        isfinite(
            features.airTemperatureC) &&
        isfinite(
            features.waterTemperatureC))
    {
        features.airWaterDeltaC =
            features.airTemperatureC -
            features.waterTemperatureC;

        features.absoluteAirWaterDeltaC =
            fabsf(
                features.airWaterDeltaC);

        features.thermalFeaturesValid =
            true;
    }

    // ========================================================
    // STRUCTURE VALIDITY
    //
    // The BLE packet itself passed protocol + CRC validation.
    //
    // Individual feature groups may still be unavailable.
    // ========================================================

    features.valid =
        true;

    return features;
}

// ============================================================
// DEW POINT
// ============================================================

float S3FeatureBuilder::calculateDewPointC(
    float temperatureC,
    float humidityPct)
{
    if (
        !isfinite(
            temperatureC) ||
        temperatureC < -40.0f ||
        temperatureC > 80.0f ||
        !isfinite(
            humidityPct) ||
        humidityPct <= 0.0f ||
        humidityPct > 100.0f)
    {
        return NAN;
    }

    const float relativeHumidity =
        humidityPct /
        100.0f;

    const float gamma =
        logf(
            relativeHumidity) +
        (MAGNUS_A *
         temperatureC) /
            (MAGNUS_B_C +
             temperatureC);

    const float denominator =
        MAGNUS_A -
        gamma;

    if (
        fabsf(
            denominator) <
        0.000001f)
    {
        return NAN;
    }

    return (
               MAGNUS_B_C *
               gamma) /
           denominator;
}

// ============================================================
// SATURATION VAPOUR PRESSURE
// ============================================================

float S3FeatureBuilder::calculateSaturationVaporPressureKPa(
    float temperatureC)
{
    if (
        !isfinite(
            temperatureC) ||
        temperatureC < -40.0f ||
        temperatureC > 80.0f)
    {
        return NAN;
    }

    const float denominator =
        temperatureC +
        SVP_C_C;

    if (
        fabsf(
            denominator) <
        0.000001f)
    {
        return NAN;
    }

    const float exponent =
        (SVP_B *
         temperatureC) /
        denominator;

    const float result =
        SVP_A_KPA *
        expf(
            exponent);

    if (
        !isfinite(
            result) ||
        result < 0.0f)
    {
        return NAN;
    }

    return result;
}

// ============================================================
// ACTUAL VAPOUR PRESSURE
// ============================================================

float S3FeatureBuilder::calculateActualVaporPressureKPa(
    float saturationVaporPressureKPa,
    float humidityPct)
{
    if (
        !isfinite(
            saturationVaporPressureKPa) ||
        !isfinite(
            humidityPct) ||
        saturationVaporPressureKPa < 0.0f ||
        humidityPct < 0.0f ||
        humidityPct > 100.0f)
    {
        return NAN;
    }

    return saturationVaporPressureKPa *
           (humidityPct /
            100.0f);
}

// ============================================================
// VAPOUR PRESSURE DEFICIT
// ============================================================

float S3FeatureBuilder::calculateVpdKPa(
    float saturationVaporPressureKPa,
    float actualVaporPressureKPa)
{
    if (
        !isfinite(
            saturationVaporPressureKPa) ||
        !isfinite(
            actualVaporPressureKPa))
    {
        return NAN;
    }

    float vpd =
        saturationVaporPressureKPa -
        actualVaporPressureKPa;

    // Small floating-point errors should not generate
    // physically impossible negative VPD values.

    if (
        vpd < 0.0f &&
        vpd > -0.001f)
    {
        vpd =
            0.0f;
    }

    if (
        vpd < 0.0f)
    {
        return NAN;
    }

    return vpd;
}

// ============================================================
// ABSOLUTE HUMIDITY
// ============================================================

float S3FeatureBuilder::calculateAbsoluteHumidityGm3(
    float temperatureC,
    float actualVaporPressureKPa)
{
    if (
        !isfinite(
            temperatureC) ||
        temperatureC < -40.0f ||
        temperatureC > 80.0f ||
        !isfinite(
            actualVaporPressureKPa) ||
        actualVaporPressureKPa < 0.0f)
    {
        return NAN;
    }

    const float temperatureK =
        temperatureC +
        KELVIN_OFFSET;

    if (
        temperatureK <= 0.0f)
    {
        return NAN;
    }

    const float absoluteHumidity =
        (ABS_HUMIDITY_CONSTANT *
         actualVaporPressureKPa) /
        temperatureK;

    if (
        !isfinite(
            absoluteHumidity) ||
        absoluteHumidity < 0.0f)
    {
        return NAN;
    }

    return absoluteHumidity;
}