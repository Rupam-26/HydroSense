// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <math.h>
#include <stdint.h>

// ============================================================
// HYDROSENSE ESP32-S3
// CROSS-LAYER SENSOR TRUST RESULT MODEL
//
// A trust score is an engineering index from 0–100.
// It is not a calibrated probability that a sensor is correct.
// ============================================================

enum class S3SensorTrustState : uint8_t
{
    MeasurementUnavailable = 0,
    LearningNormalBehaviour,
    Trusted,
    Monitor,
    LowTrust,
    Unreliable,
    PartialSensorCoverage
};

enum class S3SensorTrustReason : uint8_t
{
    None = 0,
    MeasurementUnavailable,
    LearningNormalBehaviour,
    LowDataQuality,
    CommunicationInstability,
    PossibleStuckOutput,
    ExcessiveMeasurementNoise,
    ChangedResponseDynamics,
    SuddenMeasurementChange,
    PossibleResponseDegradation,
    FoulingCompatibleResponseDegradation
};

struct S3TrustComponent
{
    bool available = false;
    float scorePct = NAN;

    void clear()
    {
        available = false;
        scorePct = NAN;
    }
};

struct S3SensorTrustAnalysis
{
    bool measurementAvailable = false;
    bool baselineReady = false;

    // Engineering trust index, not probability.
    float trustScorePct = 0.0f;

    // Percentage of the universal evidence stack that was available.
    float evidenceCoveragePct = 0.0f;

    S3SensorTrustState state =
        S3SensorTrustState::MeasurementUnavailable;

    S3SensorTrustReason primaryReason =
        S3SensorTrustReason::MeasurementUnavailable;

    S3TrustComponent measurementIntegrity;
    S3TrustComponent dataQuality;
    S3TrustComponent temporalReliability;
    S3TrustComponent learnedBehaviourConsistency;
    S3TrustComponent communicationIntegrity;

    // Optional degradation evidence for the submerged
    // water-temperature probe.
    S3TrustComponent domainSpecificDegradation;
    bool domainSpecificEvidenceApplied = false;

    void clear()
    {
        measurementAvailable = false;
        baselineReady = false;

        trustScorePct = 0.0f;
        evidenceCoveragePct = 0.0f;

        state = S3SensorTrustState::MeasurementUnavailable;
        primaryReason = S3SensorTrustReason::MeasurementUnavailable;

        measurementIntegrity.clear();
        dataQuality.clear();
        temporalReliability.clear();
        learnedBehaviourConsistency.clear();
        communicationIntegrity.clear();
        domainSpecificDegradation.clear();

        domainSpecificEvidenceApplied = false;
    }
};

struct S3SensorTrustResult
{
    uint8_t sourceSequence = 0;
    uint32_t evaluatedAtMs = 0;

    bool baselineReady = false;
    bool allRequiredMeasurementsAvailable = false;

    // Communication quality of the C3-to-S3 measurement path.
    float communicationIntegrityPct = 100.0f;

    // Weakest-link trust across required physical measurements.
    float overallTrustScorePct = 0.0f;
    float overallEvidenceCoveragePct = 0.0f;

    S3SensorTrustState overallState =
        S3SensorTrustState::LearningNormalBehaviour;

    S3SensorTrustAnalysis airTemperature;
    S3SensorTrustAnalysis humidity;
    S3SensorTrustAnalysis waterTemperature;
    S3SensorTrustAnalysis reservoirDistance;

    uint32_t transportFaultEventsObserved = 0;
    uint32_t sequenceGapEventsObserved = 0;
    uint32_t reconnectEventsObserved = 0;

    void clear()
    {
        sourceSequence = 0;
        evaluatedAtMs = 0;

        baselineReady = false;
        allRequiredMeasurementsAvailable = false;

        communicationIntegrityPct = 100.0f;
        overallTrustScorePct = 0.0f;
        overallEvidenceCoveragePct = 0.0f;

        overallState =
            S3SensorTrustState::LearningNormalBehaviour;

        airTemperature.clear();
        humidity.clear();
        waterTemperature.clear();
        reservoirDistance.clear();

        transportFaultEventsObserved = 0;
        sequenceGapEventsObserved = 0;
        reconnectEventsObserved = 0;
    }
};