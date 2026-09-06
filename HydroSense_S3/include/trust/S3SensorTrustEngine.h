// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "models/S3AnomalyResult.h"
#include "models/S3SensorSnapshot.h"
#include "models/S3SensorTrustResult.h"

// ============================================================
// HYDROSENSE ESP32-S3
// CROSS-LAYER SENSOR TRUST ENGINE
//
// Universal evidence:
// 1. Measurement integrity
// 2. Data-quality evidence
// 3. Temporal reliability
// 4. Learned-behaviour consistency
// 5. Communication integrity
//
// Optional physically eligible submerged-probe degradation
// evidence can also be applied.
//
// Missing or scientifically inapplicable evidence is excluded.
// It is never replaced with fabricated values.
// ============================================================

class S3SensorTrustEngine
{
public:
    bool begin();

    S3SensorTrustResult update(
        const S3SensorSnapshot &snapshot,
        const S3AnomalyResult &anomaly,
        bool bleConnected,
        uint32_t validSensorPackets,
        uint32_t crcFailures,
        uint32_t wrongLengths,
        uint32_t badHeaders,
        uint32_t badVersions,
        uint32_t queueDrops,
        uint32_t reconnectCount,
        uint32_t nowMs);

    void reset();

    static const char *trustStateText(
        S3SensorTrustState state);

    static const char *trustReasonText(
        S3SensorTrustReason reason);

private:
    struct CounterSnapshot
    {
        uint32_t validSensorPackets = 0;
        uint32_t crcFailures = 0;
        uint32_t wrongLengths = 0;
        uint32_t badHeaders = 0;
        uint32_t badVersions = 0;
        uint32_t queueDrops = 0;
        uint32_t reconnectCount = 0;
    };

    CounterSnapshot previousCounters_{};
    bool havePreviousCounters_ = false;

    bool havePreviousSequence_ = false;
    uint8_t previousSequence_ = 0;

    float communicationIntegrityPct_ = 100.0f;

    uint32_t transportFaultEventsObserved_ = 0;
    uint32_t sequenceGapEventsObserved_ = 0;
    uint32_t reconnectEventsObserved_ = 0;

    void updateCommunicationIntegrity(
        const S3SensorSnapshot &snapshot,
        bool bleConnected,
        uint32_t validSensorPackets,
        uint32_t crcFailures,
        uint32_t wrongLengths,
        uint32_t badHeaders,
        uint32_t badVersions,
        uint32_t queueDrops,
        uint32_t reconnectCount);

    S3SensorTrustAnalysis buildSensorTrust(
        const S3SensorAnomalyAnalysis &sensorEvidence,
        float dataQualityPct,
        bool baselineReady,
        float communicationIntegrityPct,
        bool applyDomainDegradation,
        const S3AnomalyResult &anomaly) const;

    static float temporalReliabilityScore(
        const S3SensorAnomalyAnalysis &evidence);

    static float learnedBehaviourScore(
        const S3SensorAnomalyAnalysis &evidence);

    static float domainDegradationScore(
        const S3AnomalyResult &anomaly);

    static float weightedTrust(
        const S3SensorTrustAnalysis &analysis);

    static float evidenceCoverage(
        const S3SensorTrustAnalysis &analysis);

    static void applyRealityCaps(
        S3SensorTrustAnalysis &analysis,
        const S3SensorAnomalyAnalysis &evidence,
        bool applyDomainDegradation,
        const S3AnomalyResult &anomaly,
        float dataQualityPct,
        float communicationIntegrityPct);

    static S3SensorTrustState stateFromTrust(
        float trustPct);

    static S3SensorTrustReason primaryReason(
        const S3SensorAnomalyAnalysis &evidence,
        bool applyDomainDegradation,
        const S3AnomalyResult &anomaly,
        float dataQualityPct,
        float communicationIntegrityPct,
        bool baselineReady,
        bool measurementAvailable);

    static float clampScore(float value);
};