// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <math.h>
#include <stdint.h>

// ============================================================
// HYDROSENSE ESP32-S3
// EDGE SENSOR RELIABILITY RESULT MODEL
//
// Anomaly scores are engineering evidence indexes from 0–100.
// They are not probabilities of sensor failure.
// ============================================================

enum class S3SensorCondition : uint8_t
{
    MeasurementUnavailable = 0,
    LearningBaseline,
    Normal,
    Monitor,
    Warning
};

enum class S3SurfaceFoulingState : uint8_t
{
    LearningCleanResponse = 0,
    AssessmentNotPossible,
    InsufficientThermalEvidence,
    LowEvidence,
    PossibleResponseDegradation,
    FoulingCompatibleDegradation
};

struct S3SensorAnomalyAnalysis
{
    bool available = false;
    bool baselineReady = false;

    float anomalyScorePct = 0.0f;

    bool stuckOutputEvidence = false;
    bool excessiveNoiseEvidence = false;
    bool rapidChangeEvidence = false;
    bool changedDynamicsEvidence = false;

    bool suppressedDynamicsEvidence = false;
    bool amplifiedDynamicsEvidence = false;

    float currentValue = NAN;
    float shortTermMean = NAN;
    float shortTermStandardDeviation = NAN;
    float shortTermRange = NAN;
    float shortTermSlopePerSample = NAN;
    float shortTermDifferenceStandardDeviation = NAN;
    float shortTermMedianAbsoluteDifference = NAN;

    float baselineDifferenceStandardDeviation = NAN;

    uint16_t availableWindowSamples = 0;
    uint16_t consecutiveNearlyUnchangedSamples = 0;

    uint8_t stuckEvidencePersistence = 0;
    uint8_t noiseEvidencePersistence = 0;
    uint8_t dynamicsEvidencePersistence = 0;

    S3SensorCondition condition =
        S3SensorCondition::MeasurementUnavailable;

    void clear()
    {
        available = false;
        baselineReady = false;

        anomalyScorePct = 0.0f;

        stuckOutputEvidence = false;
        excessiveNoiseEvidence = false;
        rapidChangeEvidence = false;
        changedDynamicsEvidence = false;
        suppressedDynamicsEvidence = false;
        amplifiedDynamicsEvidence = false;

        currentValue = NAN;
        shortTermMean = NAN;
        shortTermStandardDeviation = NAN;
        shortTermRange = NAN;
        shortTermSlopePerSample = NAN;
        shortTermDifferenceStandardDeviation = NAN;
        shortTermMedianAbsoluteDifference = NAN;
        baselineDifferenceStandardDeviation = NAN;

        availableWindowSamples = 0;
        consecutiveNearlyUnchangedSamples = 0;

        stuckEvidencePersistence = 0;
        noiseEvidencePersistence = 0;
        dynamicsEvidencePersistence = 0;

        condition = S3SensorCondition::MeasurementUnavailable;
    }
};

struct S3AnomalyResult
{
    uint8_t sourceSequence = 0;
    uint32_t evaluatedAtMs = 0;

    bool generalBaselineReady = false;
    uint16_t generalBaselineSamples = 0;
    uint16_t requiredGeneralBaselineSamples = 0;

    float overallSensorAnomalyScorePct = 0.0f;

    S3SensorCondition overallCondition =
        S3SensorCondition::LearningBaseline;

    S3SensorAnomalyAnalysis airTemperature;
    S3SensorAnomalyAnalysis humidity;
    S3SensorAnomalyAnalysis waterTemperature;
    S3SensorAnomalyAnalysis reservoirDistance;

    // Submerged DS18B20 response-degradation assessment.
    // This cannot directly identify algae or biological fouling.

    bool surfaceFoulingAssessmentAvailable = false;

    // Engineering evidence index, not a probability of algae.
    float surfaceFoulingRiskIndexPct = 0.0f;
    float surfaceFoulingAssessmentConfidencePct = 0.0f;

    bool currentThermalEventEligible = false;
    bool reservoirThermallyStable = false;
    bool thermalExcitationStrongEnough = false;
    bool thermalResponseDirectionCoherent = false;
    bool thermalEventComparableToBaseline = false;
    bool competingWaterSensorFaultEvidence = false;

    bool responseAttenuationEvidence = false;
    bool thermalRelationshipShiftEvidence = false;
    bool waterResponseSuppressionEvidence = false;

    uint16_t thermalResponseBaselineEvents = 0;
    uint16_t requiredThermalResponseBaselineEvents = 0;
    uint16_t thermalResponseAssessmentEvents = 0;
    uint16_t eligibleThermalEventsObserved = 0;
    uint16_t rejectedThermalEvents = 0;

    float cleanThermalResponseRatio = NAN;
    float currentThermalResponseRatio = NAN;
    float responseRatioRelativeToCleanPct = NAN;

    S3SurfaceFoulingState surfaceFoulingState =
        S3SurfaceFoulingState::LearningCleanResponse;

    // The TDS estimate is contextual information only because it is
    // derived from the same physical sensor measurements.

    bool tinyMlTdsAvailable = false;
    bool tinyMlInputSaturated = false;

    void clear()
    {
        sourceSequence = 0;
        evaluatedAtMs = 0;

        generalBaselineReady = false;
        generalBaselineSamples = 0;
        requiredGeneralBaselineSamples = 0;

        overallSensorAnomalyScorePct = 0.0f;
        overallCondition = S3SensorCondition::LearningBaseline;

        airTemperature.clear();
        humidity.clear();
        waterTemperature.clear();
        reservoirDistance.clear();

        surfaceFoulingAssessmentAvailable = false;
        surfaceFoulingRiskIndexPct = 0.0f;
        surfaceFoulingAssessmentConfidencePct = 0.0f;

        currentThermalEventEligible = false;
        reservoirThermallyStable = false;
        thermalExcitationStrongEnough = false;
        thermalResponseDirectionCoherent = false;
        thermalEventComparableToBaseline = false;
        competingWaterSensorFaultEvidence = false;

        responseAttenuationEvidence = false;
        thermalRelationshipShiftEvidence = false;
        waterResponseSuppressionEvidence = false;

        thermalResponseBaselineEvents = 0;
        requiredThermalResponseBaselineEvents = 0;
        thermalResponseAssessmentEvents = 0;
        eligibleThermalEventsObserved = 0;
        rejectedThermalEvents = 0;

        cleanThermalResponseRatio = NAN;
        currentThermalResponseRatio = NAN;
        responseRatioRelativeToCleanPct = NAN;

        surfaceFoulingState =
            S3SurfaceFoulingState::LearningCleanResponse;

        tinyMlTdsAvailable = false;
        tinyMlInputSaturated = false;
    }
};