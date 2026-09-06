#include "physics/EnvironmentalPhysics.h"

#include <math.h>

// ============================================================
// INPUT VALIDATION
// ============================================================

bool EnvironmentalPhysics::validAirTemperature(
    float value)
{
    if (!isfinite(value))
    {
        return false;
    }

    // DHT22 measurement range
    return value >= -40.0f &&
           value <= 80.0f;
}

bool EnvironmentalPhysics::validHumidity(
    float value)
{
    if (!isfinite(value))
    {
        return false;
    }

    // RH must be greater than zero for logarithmic
    // dew-point calculation.
    return value > 0.0f &&
           value <= 100.0f;
}

bool EnvironmentalPhysics::validWaterTemperature(
    float value)
{
    if (!isfinite(value))
    {
        return false;
    }

    // DS18B20 physical range
    return value >= -55.0f &&
           value <= 125.0f;
}

// ============================================================
// TEMPERATURE CONVERSION
// ============================================================

float EnvironmentalPhysics::celsiusToFahrenheit(
    float valueC)
{
    return (valueC * 9.0f / 5.0f) + 32.0f;
}

float EnvironmentalPhysics::fahrenheitToCelsius(
    float valueF)
{
    return (valueF - 32.0f) * 5.0f / 9.0f;
}

// ============================================================
// DEW POINT
//
// Magnus approximation
//
// gamma =
// ln(RH/100) + aT/(b+T)
//
// Td =
// b*gamma/(a-gamma)
// ============================================================

float EnvironmentalPhysics::dewPointC(
    float airTemperatureC,
    float humidityPct)
{
    if (!validAirTemperature(airTemperatureC) ||
        !validHumidity(humidityPct))
    {
        return NAN;
    }

    constexpr float A = 17.625f;
    constexpr float B = 243.04f;

    float gamma =
        logf(humidityPct / 100.0f) +
        (A * airTemperatureC) /
            (B + airTemperatureC);

    float dewPoint =
        (B * gamma) /
        (A - gamma);

    if (!isfinite(dewPoint))
    {
        return NAN;
    }

    return dewPoint;
}

// ============================================================
// SATURATION VAPOR PRESSURE
//
// Tetens-type approximation
//
// Result: kPa
// ============================================================

float EnvironmentalPhysics::
    saturationVaporPressureKPa(
        float airTemperatureC)
{
    if (!validAirTemperature(
            airTemperatureC))
    {
        return NAN;
    }

    float svp =
        0.6108f *
        expf(
            (
                17.27f * airTemperatureC) /
            (airTemperatureC + 237.3f));

    if (!isfinite(svp))
    {
        return NAN;
    }

    return svp;
}

// ============================================================
// ACTUAL VAPOR PRESSURE
//
// AVP = SVP * RH/100
//
// Result: kPa
// ============================================================

float EnvironmentalPhysics::
    actualVaporPressureKPa(
        float airTemperatureC,
        float humidityPct)
{
    if (!validHumidity(humidityPct))
    {
        return NAN;
    }

    float svp =
        saturationVaporPressureKPa(
            airTemperatureC);

    if (!isfinite(svp))
    {
        return NAN;
    }

    return svp *
           (humidityPct / 100.0f);
}

// ============================================================
// VAPOR PRESSURE DEFICIT
//
// VPD = SVP - AVP
//
// Result: kPa
// ============================================================

float EnvironmentalPhysics::vpdKPa(
    float airTemperatureC,
    float humidityPct)
{
    float svp =
        saturationVaporPressureKPa(
            airTemperatureC);

    float avp =
        actualVaporPressureKPa(
            airTemperatureC,
            humidityPct);

    if (!isfinite(svp) ||
        !isfinite(avp))
    {
        return NAN;
    }

    float vpd =
        svp - avp;

    // Numerical defensive clamp.
    if (vpd < 0.0f)
    {
        vpd = 0.0f;
    }

    return vpd;
}

// ============================================================
// ABSOLUTE HUMIDITY
//
// AH =
// 216.7 * e / (T + 273.15)
//
// e must be in hPa.
//
// Output:
// grams water / cubic meter
// ============================================================

float EnvironmentalPhysics::
    absoluteHumidityGM3(
        float airTemperatureC,
        float humidityPct)
{
    float avpKPa =
        actualVaporPressureKPa(
            airTemperatureC,
            humidityPct);

    if (!isfinite(avpKPa))
    {
        return NAN;
    }

    float vaporPressureHPa =
        avpKPa * 10.0f;

    float absoluteHumidity =
        216.7f *
        vaporPressureHPa /
        (airTemperatureC + 273.15f);

    if (!isfinite(absoluteHumidity))
    {
        return NAN;
    }

    return absoluteHumidity;
}

// ============================================================
// HEAT INDEX
//
// Uses NOAA/Rothfusz method in Fahrenheit.
//
// For temperatures where full heat-index regression is not
// applicable, the simpler preliminary approximation is used.
// ============================================================

float EnvironmentalPhysics::heatIndexC(
    float airTemperatureC,
    float humidityPct)
{
    if (!validAirTemperature(airTemperatureC) ||
        !validHumidity(humidityPct))
    {
        return NAN;
    }

    float temperatureF =
        celsiusToFahrenheit(
            airTemperatureC);

    // Preliminary/simple heat-index approximation
    float simpleHI =
        0.5f *
        (temperatureF + 61.0f +
         (temperatureF - 68.0f) * 1.2f +
         humidityPct * 0.094f);

    simpleHI =
        (simpleHI + temperatureF) / 2.0f;

    float heatIndexF =
        simpleHI;

    // Full Rothfusz regression when appropriate
    if (simpleHI >= 80.0f)
    {
        float T =
            temperatureF;

        float R =
            humidityPct;

        heatIndexF =
            -42.379f +
            2.04901523f * T +
            10.14333127f * R -
            0.22475541f * T * R -
            0.00683783f * T * T -
            0.05481717f * R * R +
            0.00122874f * T * T * R +
            0.00085282f * T * R * R -
            0.00000199f * T * T * R * R;

        // ----------------------------------------------------
        // Low-humidity adjustment
        // ----------------------------------------------------

        if (
            R < 13.0f &&
            T >= 80.0f &&
            T <= 112.0f)
        {
            float adjustment =
                ((13.0f - R) / 4.0f) *
                sqrtf(
                    (
                        17.0f -
                        fabsf(
                            T - 95.0f)) /
                    17.0f);

            heatIndexF -=
                adjustment;
        }

        // ----------------------------------------------------
        // High-humidity adjustment
        // ----------------------------------------------------

        if (
            R > 85.0f &&
            T >= 80.0f &&
            T <= 87.0f)
        {
            float adjustment =
                ((R - 85.0f) / 10.0f) *
                ((87.0f - T) / 5.0f);

            heatIndexF +=
                adjustment;
        }
    }

    return fahrenheitToCelsius(
        heatIndexF);
}

// ============================================================
// AIR - WATER TEMPERATURE DIFFERENCE
//
// Signed:
//
// positive:
// air hotter than water
//
// negative:
// water hotter than air
// ============================================================

float EnvironmentalPhysics::airWaterDeltaC(
    float airTemperatureC,
    float waterTemperatureC)
{
    if (!validAirTemperature(
            airTemperatureC) ||
        !validWaterTemperature(
            waterTemperatureC))
    {
        return NAN;
    }

    return airTemperatureC -
           waterTemperatureC;
}

// ============================================================
// COMPLETE ENVIRONMENTAL CALCULATION
// ============================================================

EnvironmentalData EnvironmentalPhysics::calculate(
    float airTemperatureC,
    float humidityPct,
    float waterTemperatureC)
{
    EnvironmentalData data{};

    // --------------------------------------------------------
    // Store supplied direct measurements
    // --------------------------------------------------------

    data.airTemperatureC =
        airTemperatureC;

    data.humidityPct =
        humidityPct;

    data.waterTemperatureC =
        waterTemperatureC;

    // --------------------------------------------------------
    // Default outputs
    // --------------------------------------------------------

    data.dewPointC = NAN;

    data.dewPointDepressionC = NAN;

    data.saturationVaporPressureKPa =
        NAN;

    data.actualVaporPressureKPa =
        NAN;

    data.vpdKPa = NAN;

    data.absoluteHumidityGM3 = NAN;

    data.heatIndexC = NAN;

    data.airWaterDeltaC = NAN;

    // --------------------------------------------------------
    // Validate atmospheric input
    // --------------------------------------------------------

    data.atmosphericValid =
        validAirTemperature(
            airTemperatureC) &&
        validHumidity(
            humidityPct);

    // --------------------------------------------------------
    // Validate water sensor input
    // --------------------------------------------------------

    data.waterTemperatureValid =
        validWaterTemperature(
            waterTemperatureC);

    // --------------------------------------------------------
    // Atmospheric calculations
    // --------------------------------------------------------

    if (data.atmosphericValid)
    {
        data.dewPointC =
            dewPointC(
                airTemperatureC,
                humidityPct);

        if (isfinite(data.dewPointC))
        {
            data.dewPointDepressionC =
                airTemperatureC -
                data.dewPointC;
        }

        data.saturationVaporPressureKPa =
            saturationVaporPressureKPa(
                airTemperatureC);

        data.actualVaporPressureKPa =
            actualVaporPressureKPa(
                airTemperatureC,
                humidityPct);

        data.vpdKPa =
            vpdKPa(
                airTemperatureC,
                humidityPct);

        data.absoluteHumidityGM3 =
            absoluteHumidityGM3(
                airTemperatureC,
                humidityPct);

        data.heatIndexC =
            heatIndexC(
                airTemperatureC,
                humidityPct);
    }

    // --------------------------------------------------------
    // Air-water thermal relationship
    // --------------------------------------------------------

    if (
        data.atmosphericValid &&
        data.waterTemperatureValid)
    {
        data.airWaterDeltaC =
            airWaterDeltaC(
                airTemperatureC,
                waterTemperatureC);
    }

    // --------------------------------------------------------
    // Complete dataset valid only when both source sensor
    // families are valid.
    // --------------------------------------------------------

    data.completeValid =
        data.atmosphericValid &&
        data.waterTemperatureValid;

    return data;
}