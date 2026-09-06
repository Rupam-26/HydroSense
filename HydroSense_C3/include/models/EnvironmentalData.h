#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE
// ENVIRONMENTAL DERIVED DATA MODEL
//
// This file contains DATA ONLY.
//
// No sensor reading.
// No equations.
// No filtering.
// No Serial output.
// ============================================================

struct EnvironmentalData
{
    // --------------------------------------------------------
    // Direct input values used for the calculation
    // --------------------------------------------------------

    float airTemperatureC;
    float humidityPct;
    float waterTemperatureC;

    // --------------------------------------------------------
    // Atmospheric derived quantities
    // --------------------------------------------------------

    float dewPointC;

    float dewPointDepressionC;

    float saturationVaporPressureKPa;

    float actualVaporPressureKPa;

    float vpdKPa;

    float absoluteHumidityGM3;

    float heatIndexC;

    // --------------------------------------------------------
    // Air / water thermal relationship
    // --------------------------------------------------------

    float airWaterDeltaC;

    // --------------------------------------------------------
    // Calculation validity
    // --------------------------------------------------------

    bool atmosphericValid;

    bool waterTemperatureValid;

    bool completeValid;
};