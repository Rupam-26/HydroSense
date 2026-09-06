#pragma once

#include <Arduino.h>

#include "models/DataQualityData.h"
#include "models/C3AnalyticsData.h"
#include "models/SensorHealthData.h"

// ============================================================
// HYDROSENSE
// DATA QUALITY ENGINE
//
// RESPONSIBILITY:
//
// - Examine sensor health
// - Examine calibration validity
// - Examine reservoir validity
// - Examine environmental-data validity
// - Examine trend readiness
// - Calculate transparent quality score
// - Determine whether data can be sent to future TinyML
//
// DOES NOT:
//
// - Read hardware
// - Modify sensor data
// - Fabricate missing data
// - Run TinyML
// - Send ESP-NOW
// - Display UI
// ============================================================

class DataQualityEngine
{
public:
    DataQualityEngine();

    // --------------------------------------------------------
    // Evaluate current C3 state
    // --------------------------------------------------------

    DataQualityData evaluate(
        const C3AnalyticsData &analytics,
        const NodeHealthData &health) const;

    // --------------------------------------------------------
    // Human-readable state
    // --------------------------------------------------------

    static const char *stateText(
        DataQualityState state);

private:
    // --------------------------------------------------------
    // Calculate sensor score contribution using:
    //
    // current validity
    // +
    // historical availability
    // --------------------------------------------------------

    float sensorContribution(
        const SensorHealth &health,
        float maximumWeight) const;

    // --------------------------------------------------------
    // Calculate percentage of required trend systems ready
    // --------------------------------------------------------

    float trendReadinessFraction(
        const C3AnalyticsData &analytics) const;

    // --------------------------------------------------------
    // Convert score into categorical state
    // --------------------------------------------------------

    DataQualityState classifyScore(
        float score) const;
};