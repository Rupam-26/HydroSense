#pragma once

#include <Arduino.h>
#include <math.h>

#include "models/C3AnalyticsData.h"
#include "models/SensorHealthData.h"
#include "models/DataQualityData.h"

// ============================================================
// HYDROSENSE ESP32-C3
// LOCAL DISPLAY DATA SNAPSHOT
// ============================================================

enum class C3ReservoirType : uint8_t
{
    UNCONFIGURED = 0,
    RECTANGULAR,
    VERTICAL_CYLINDER
};

struct C3DisplayData
{
    // Runtime
    uint32_t measurementNumber = 0;
    uint32_t uptimeMs = 0;

    // Monotonic local-display snapshot revision.
    // Incremented only when the C3 rebuilds a real runtime snapshot.
    // This lets the OLED avoid redundant full-frame I2C transfers.
    uint32_t revision = 0;

    // Current system snapshots
    C3AnalyticsData analytics{};
    NodeHealthData health{};
    DataQualityData quality{};

    // Ultrasonic presentation state
    bool currentSonarPingValid = false;
    bool distanceHeld = false;
    uint32_t distanceAgeMs = 0;
    float currentStableDistanceCm = NAN;
    float speedOfSoundMps = NAN;

    // Reservoir calibration
    bool fullDistanceAvailable = false;
    bool halfDistanceAvailable = false;
    bool emptyDistanceAvailable = false;

    float fullDistanceCm = NAN;
    float halfDistanceCm = NAN;
    float emptyDistanceCm = NAN;
    float calibrationRangeCm = NAN;

    // Reservoir geometry configured by the operator
    C3ReservoirType reservoirType = C3ReservoirType::UNCONFIGURED;
    bool geometryConfigured = false;

    float reservoirLengthCm = NAN;
    float reservoirWidthCm = NAN;
    float reservoirHeightCm = NAN;
    float reservoirDiameterCm = NAN;

    // Geometric capacity and current estimated volume.
    // These are only exposed when the required geometry exists.
    float reservoirCapacityL = NAN;
    float currentVolumeL = NAN;
};