// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "models/S3AnomalyResult.h"
#include "models/S3FeatureData.h"
#include "models/S3SensorSnapshot.h"
#include "models/S3TDSResult.h"

// ============================================================
// HYDROSENSE ESP32-S3
// EXPLAINABLE SENSOR RELIABILITY ENGINE
//
// Design principles:
// - A stable value is not automatically treated as a fault.
// - A real environmental change is not automatically a fault.
// - Isolated transients are separated from persistent behaviour.
// - DHT22 temperature and humidity originate from one module.
// - Derived TDS is not independent evidence of sensor health.
// - DS18B20 response degradation requires repeated,
//   comparable and physically eligible thermal events.
// ============================================================

class S3AnomalyEngine
{
public:
    bool begin();

    S3AnomalyResult update(
        const S3SensorSnapshot &snapshot,
        const S3FeatureData &features,
        const S3TDSResult &tdsResult,
        uint32_t nowMs);

    void resetBaseline();

    bool baselineReady() const;

    static const char *sensorConditionText(
        S3SensorCondition condition);

    static const char *surfaceFoulingStateText(
        S3SurfaceFoulingState state);

private:
    // Approximately six minutes at the normal three-second C3 rate.
    static constexpr uint16_t WINDOW_SIZE = 120;

    // Approximately ten minutes of clean short-term operation.
    static constexpr uint16_t GENERAL_BASELINE_SAMPLES = 200;

    // Approximately six minutes between the beginning and end
    // of one thermal-response observation.
    static constexpr uint16_t THERMAL_EVENT_WINDOW = 119;

    static constexpr uint16_t REQUIRED_THERMAL_BASELINE_EVENTS = 10;
    static constexpr uint16_t REQUIRED_THERMAL_ASSESSMENT_EVENTS = 5;
    static constexpr uint16_t RECENT_THERMAL_EVENT_MEMORY = 6;

    struct RunningStats
    {
        uint32_t count = 0;
        double mean = 0.0;
        double m2 = 0.0;

        void clear();
        void add(float value);
        float standardDeviation() const;
    };

    struct SensorTracker
    {
        float values[WINDOW_SIZE]{};
        bool valid[WINDOW_SIZE]{};

        uint16_t writeIndex = 0;
        uint16_t sampleCount = 0;

        bool previousValid = false;
        float previousValue = NAN;
        uint16_t consecutiveNearlyUnchanged = 0;

        // Baseline continuity is independent from live tracking.
        // Rejected or low-quality packets break this chain.
        bool baselinePreviousValid = false;
        float baselinePreviousValue = NAN;

        RunningStats baselineValue;
        RunningStats baselineDifference;

        uint8_t stuckPersistence = 0;
        uint8_t noisePersistence = 0;
        uint8_t dynamicsPersistence = 0;

        void clear();
    };

    struct WindowStats
    {
        bool valid = false;
        uint16_t count = 0;

        float mean = NAN;
        float standardDeviation = NAN;
        float minimum = NAN;
        float maximum = NAN;
        float range = NAN;
        float slopePerSample = NAN;
        float differenceStandardDeviation = NAN;
        float medianAbsoluteDifference = NAN;
        float latestDifference = NAN;
    };

    SensorTracker airTemperature_;
    SensorTracker humidity_;
    SensorTracker waterTemperature_;
    SensorTracker reservoirDistance_;

    RunningStats baselineAirWaterDelta_;

    RunningStats baselineThermalResponseRatio_;
    RunningStats baselineThermalAirChangeMagnitude_;
    RunningStats baselineThermalReservoirDistance_;

    uint16_t validGeneralSamples_ = 0;
    bool generalBaselineReady_ = false;

    uint16_t thermalResponseAssessmentEvents_ = 0;
    uint16_t eligibleThermalEventsObserved_ = 0;
    uint16_t rejectedThermalEvents_ = 0;

    bool recentResponseAttenuation_
        [RECENT_THERMAL_EVENT_MEMORY]{};

    uint16_t recentThermalEventWriteIndex_ = 0;
    uint16_t recentThermalEventCount_ = 0;

    bool persistentResponseAttenuationEvidence_ = false;

    uint32_t lastThermalEventEvaluationMs_ = 0;

    void pushSensorValue(
        SensorTracker &tracker,
        float value,
        bool valueValid,
        float unchangedEpsilon,
        bool baselineEligible);

    WindowStats calculateWindowStats(
        const SensorTracker &tracker) const;

    S3SensorAnomalyAnalysis analyseSensor(
        SensorTracker &tracker,
        bool available,
        float currentValue,
        float minimumNoiseDifference,
        float rapidChangeFloor,
        bool suppressionReferenceMoved,
        uint16_t minimumStuckSamples,
        bool stuckAssessmentAllowed);

    bool generalBaselineSampleEligible(
        const S3SensorSnapshot &snapshot,
        const S3FeatureData &features) const;

    void updateBaseline(
        const S3FeatureData &features,
        bool baselineEligible);

    void updateThermalResponseModel(
        const S3SensorSnapshot &snapshot,
        const S3FeatureData &features,
        const WindowStats &airWindow,
        const WindowStats &waterWindow,
        const WindowStats &distanceWindow,
        uint32_t nowMs,
        S3AnomalyResult &result);

    bool getValueFromSamplesAgo(
        const SensorTracker &tracker,
        uint16_t samplesAgo,
        float &value) const;

    float calculateSurfaceFoulingEvidenceIndex(
        const S3AnomalyResult &result) const;

    float calculateFoulingAssessmentConfidence(
        const S3SensorSnapshot &snapshot,
        const S3AnomalyResult &result) const;

    static void updatePersistence(
        uint8_t &counter,
        bool evidence,
        uint8_t maximum = 12);

    static float clampScore(float value);

    static S3SensorCondition conditionFromScore(
        float scorePct);
};