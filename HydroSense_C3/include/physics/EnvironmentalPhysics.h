#pragma once

#include <Arduino.h>

#include "models/EnvironmentalData.h"

// ============================================================
// HYDROSENSE
// ENVIRONMENTAL PHYSICS ENGINE
//
// RESPONSIBILITY:
//
// Inputs:
// - Air temperature
// - Relative humidity
// - Water temperature
//
// Outputs:
// - Dew point
// - Dew-point depression
// - Saturation vapor pressure
// - Actual vapor pressure
// - VPD
// - Absolute humidity
// - Heat index
// - Air-water temperature difference
//
// DOES NOT:
// - Read DHT22
// - Read DS18B20
// - Read HC-SR04
// - Perform reservoir calculations
// - Perform filtering
// - Handle communications
// ============================================================

class EnvironmentalPhysics
{
public:
    // --------------------------------------------------------
    // Calculate complete environmental state
    // --------------------------------------------------------

    static EnvironmentalData calculate(
        float airTemperatureC,
        float humidityPct,
        float waterTemperatureC);

    // --------------------------------------------------------
    // Individual calculations
    // --------------------------------------------------------

    static float dewPointC(
        float airTemperatureC,
        float humidityPct);

    static float saturationVaporPressureKPa(
        float airTemperatureC);

    static float actualVaporPressureKPa(
        float airTemperatureC,
        float humidityPct);

    static float vpdKPa(
        float airTemperatureC,
        float humidityPct);

    static float absoluteHumidityGM3(
        float airTemperatureC,
        float humidityPct);

    static float heatIndexC(
        float airTemperatureC,
        float humidityPct);

    static float airWaterDeltaC(
        float airTemperatureC,
        float waterTemperatureC);

    // --------------------------------------------------------
    // Validation
    // --------------------------------------------------------

    static bool validAirTemperature(
        float value);

    static bool validHumidity(
        float value);

    static bool validWaterTemperature(
        float value);

private:
    static float fahrenheitToCelsius(
        float valueF);

    static float celsiusToFahrenheit(
        float valueC);
};