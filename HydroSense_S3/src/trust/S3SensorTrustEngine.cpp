#include "trust/S3SensorTrustEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Universal evidence weights. Missing evidence is excluded and the
    // remaining weights are renormalized by weightedTrust().
    constexpr float WEIGHT_MEASUREMENT_INTEGRITY = 25.0f;
    constexpr float WEIGHT_DATA_QUALITY = 20.0f;
    constexpr float WEIGHT_TEMPORAL_RELIABILITY = 25.0f;
    constexpr float WEIGHT_LEARNED_BEHAVIOUR = 15.0f;
    constexpr float WEIGHT_COMMUNICATION_INTEGRITY = 15.0f;

    // Optional domain-specific evidence is intentionally secondary to the
    // universal core, but strong physically supported degradation can cap
    // the final trust score through applyRealityCaps().
    constexpr float WEIGHT_DOMAIN_DEGRADATION = 20.0f;

    // Provisional operational thresholds. They are engineering thresholds,
    // not medically/statistically calibrated probabilities. The research
    // phase should later tune them against controlled fault experiments.
    constexpr float TRUSTED_THRESHOLD = 80.0f;
    constexpr float MONITOR_THRESHOLD = 65.0f;
    constexpr float LOW_TRUST_THRESHOLD = 40.0f;

    // Communication-integrity recovery/penalty behaviour.
    constexpr float COMMUNICATION_CLEAN_RECOVERY_PER_PACKET = 1.5f;
    constexpr float COMMUNICATION_FAULT_PENALTY_PER_EVENT = 15.0f;
    constexpr float COMMUNICATION_SEQUENCE_GAP_PENALTY_PER_PACKET = 4.0f;
    constexpr float COMMUNICATION_RECONNECT_PENALTY = 8.0f;

    uint32_t safeDelta(uint32_t current, uint32_t previous)
    {
        return current >= previous
                   ? current - previous
                   : current;
    }
}

// ============================================================
// INITIALIZATION / RESET
// ============================================================

bool S3SensorTrustEngine::begin()
{
    reset();
    return true;
}

void S3SensorTrustEngine::reset()
{
    previousCounters_ = CounterSnapshot{};
    havePreviousCounters_ = false;

    havePreviousSequence_ = false;
    previousSequence_ = 0;

    communicationIntegrityPct_ = 100.0f;

    transportFaultEventsObserved_ = 0;
    sequenceGapEventsObserved_ = 0;
    reconnectEventsObserved_ = 0;
}

// ============================================================
// COMMUNICATION INTEGRITY
//
// This represents confidence in the C3 -> S3 measurement transport path,
// not Wi-Fi/cloud availability. A valid sensor value should not be blamed
// for a cloud outage, so Wi-Fi is deliberately outside this score.
// ============================================================

void S3SensorTrustEngine::updateCommunicationIntegrity(
    const S3SensorSnapshot &snapshot,
    bool bleConnected,
    uint32_t validSensorPackets,
    uint32_t crcFailures,
    uint32_t wrongLengths,
    uint32_t badHeaders,
    uint32_t badVersions,
    uint32_t queueDrops,
    uint32_t reconnectCount)
{
    CounterSnapshot current{};
    current.validSensorPackets = validSensorPackets;
    current.crcFailures = crcFailures;
    current.wrongLengths = wrongLengths;
    current.badHeaders = badHeaders;
    current.badVersions = badVersions;
    current.queueDrops = queueDrops;
    current.reconnectCount = reconnectCount;

    if (!havePreviousCounters_)
    {
        // The first trust update occurs only after the first UNIQUE, CRC-valid
        // C3 packet has been accepted.  Transport faults can therefore have
        // accumulated before this point.  Do not erase that startup evidence.
        //
        // reconnectCount includes the normal first successful connection, so
        // only connections beyond the first are treated as reconnect events.
        const uint32_t initialTransportFaults =
            current.crcFailures +
            current.wrongLengths +
            current.badHeaders +
            current.badVersions +
            current.queueDrops;

        const uint32_t initialReconnects =
            current.reconnectCount > 0U
                ? current.reconnectCount - 1U
                : 0U;

        transportFaultEventsObserved_ +=
            initialTransportFaults;
        reconnectEventsObserved_ +=
            initialReconnects;

        float initialPenalty =
            static_cast<float>(initialTransportFaults) *
            COMMUNICATION_FAULT_PENALTY_PER_EVENT;

        initialPenalty +=
            static_cast<float>(initialReconnects) *
            COMMUNICATION_RECONNECT_PENALTY;

        if (!bleConnected)
        {
            initialPenalty += 30.0f;
        }

        communicationIntegrityPct_ =
            clampScore(
                100.0f -
                std::min(60.0f, initialPenalty));

        previousCounters_ = current;
        havePreviousCounters_ = true;
        previousSequence_ = snapshot.sequence;
        havePreviousSequence_ = true;
        return;
    }

    const uint32_t newCrcFailures =
        safeDelta(current.crcFailures, previousCounters_.crcFailures);
    const uint32_t newWrongLengths =
        safeDelta(current.wrongLengths, previousCounters_.wrongLengths);
    const uint32_t newBadHeaders =
        safeDelta(current.badHeaders, previousCounters_.badHeaders);
    const uint32_t newBadVersions =
        safeDelta(current.badVersions, previousCounters_.badVersions);
    const uint32_t newQueueDrops =
        safeDelta(current.queueDrops, previousCounters_.queueDrops);
    const uint32_t newReconnects =
        safeDelta(current.reconnectCount, previousCounters_.reconnectCount);

    const uint32_t newTransportFaults =
        newCrcFailures +
        newWrongLengths +
        newBadHeaders +
        newBadVersions +
        newQueueDrops;

    uint32_t missedSequencePackets = 0;

    if (havePreviousSequence_)
    {
        const uint8_t sequenceDelta =
            static_cast<uint8_t>(
                snapshot.sequence - previousSequence_);

        // A duplicate packet can occur around reconnect/read operations and
        // is not treated as a missed measurement. A forward jump >1 means
        // one or more C3 packets were not observed by this S3 instance.
        if (sequenceDelta > 1U &&
            newReconnects == 0)
        {
            missedSequencePackets =
                static_cast<uint32_t>(sequenceDelta - 1U);
        }
    }

    previousSequence_ = snapshot.sequence;
    havePreviousSequence_ = true;

    transportFaultEventsObserved_ += newTransportFaults;

    if (missedSequencePackets > 0)
    {
        ++sequenceGapEventsObserved_;
    }

    reconnectEventsObserved_ += newReconnects;

    float penalty = 0.0f;

    penalty +=
        static_cast<float>(newTransportFaults) *
        COMMUNICATION_FAULT_PENALTY_PER_EVENT;

    penalty +=
        static_cast<float>(missedSequencePackets) *
        COMMUNICATION_SEQUENCE_GAP_PENALTY_PER_PACKET;

    penalty +=
        static_cast<float>(newReconnects) *
        COMMUNICATION_RECONNECT_PENALTY;

    if (!bleConnected)
    {
        penalty += 30.0f;
    }

    if (penalty > 0.0f)
    {
        communicationIntegrityPct_ =
            clampScore(
                communicationIntegrityPct_ -
                std::min(60.0f, penalty));
    }
    else
    {
        communicationIntegrityPct_ =
            clampScore(
                communicationIntegrityPct_ +
                COMMUNICATION_CLEAN_RECOVERY_PER_PACKET);
    }

    previousCounters_ = current;
}

// ============================================================
// UNIVERSAL COMPONENT SCORES
// ============================================================

float S3SensorTrustEngine::temporalReliabilityScore(
    const S3SensorAnomalyAnalysis &evidence)
{
    float score = 100.0f;

    if (evidence.stuckOutputEvidence)
    {
        score -= 75.0f;
    }

    if (evidence.excessiveNoiseEvidence)
    {
        score -= 45.0f;
    }

    if (evidence.rapidChangeEvidence)
    {
        // A single rapid change may be a real event, so the penalty is
        // intentionally modest unless other evidence agrees.
        score -= 12.0f;
    }

    return clampScore(score);
}

float S3SensorTrustEngine::learnedBehaviourScore(
    const S3SensorAnomalyAnalysis &evidence)
{
    float score = 100.0f;

    if (evidence.suppressedDynamicsEvidence)
    {
        score -= 45.0f;
    }

    if (evidence.amplifiedDynamicsEvidence)
    {
        score -= 40.0f;
    }

    return clampScore(score);
}

float S3SensorTrustEngine::domainDegradationScore(
    const S3AnomalyResult &anomaly)
{
    if (!anomaly.surfaceFoulingAssessmentAvailable)
    {
        return NAN;
    }

    // Evidence is discounted by assessment confidence. This prevents a high
    // evidence index based on weak physical coverage from dominating trust.
    const float supportedEvidence =
        clampScore(anomaly.surfaceFoulingRiskIndexPct) *
        clampScore(anomaly.surfaceFoulingAssessmentConfidencePct) /
        100.0f;

    return clampScore(100.0f - supportedEvidence);
}

// ============================================================
// EVIDENCE-GATED WEIGHTED FUSION
// ============================================================

float S3SensorTrustEngine::weightedTrust(
    const S3SensorTrustAnalysis &analysis)
{
    float weightedSum = 0.0f;
    float weightSum = 0.0f;

    const auto add =
        [&](const S3TrustComponent &component, float weight)
    {
        if (!component.available ||
            !std::isfinite(component.scorePct))
        {
            return;
        }

        weightedSum +=
            clampScore(component.scorePct) * weight;
        weightSum += weight;
    };

    add(
        analysis.measurementIntegrity,
        WEIGHT_MEASUREMENT_INTEGRITY);
    add(
        analysis.dataQuality,
        WEIGHT_DATA_QUALITY);
    add(
        analysis.temporalReliability,
        WEIGHT_TEMPORAL_RELIABILITY);
    add(
        analysis.learnedBehaviourConsistency,
        WEIGHT_LEARNED_BEHAVIOUR);
    add(
        analysis.communicationIntegrity,
        WEIGHT_COMMUNICATION_INTEGRITY);

    if (analysis.domainSpecificEvidenceApplied)
    {
        add(
            analysis.domainSpecificDegradation,
            WEIGHT_DOMAIN_DEGRADATION);
    }

    if (weightSum <= 0.0f)
    {
        return 0.0f;
    }

    return clampScore(weightedSum / weightSum);
}

float S3SensorTrustEngine::evidenceCoverage(
    const S3SensorTrustAnalysis &analysis)
{
    // Coverage refers to the UNIVERSAL core only. Domain-specific plugins are
    // optional and therefore do not penalize a generic sensor for being absent.
    float availableWeight = 0.0f;
    constexpr float totalCoreWeight =
        WEIGHT_MEASUREMENT_INTEGRITY +
        WEIGHT_DATA_QUALITY +
        WEIGHT_TEMPORAL_RELIABILITY +
        WEIGHT_LEARNED_BEHAVIOUR +
        WEIGHT_COMMUNICATION_INTEGRITY;

    if (analysis.measurementIntegrity.available)
    {
        availableWeight += WEIGHT_MEASUREMENT_INTEGRITY;
    }

    if (analysis.dataQuality.available)
    {
        availableWeight += WEIGHT_DATA_QUALITY;
    }

    if (analysis.temporalReliability.available)
    {
        availableWeight += WEIGHT_TEMPORAL_RELIABILITY;
    }

    if (analysis.learnedBehaviourConsistency.available)
    {
        availableWeight += WEIGHT_LEARNED_BEHAVIOUR;
    }

    if (analysis.communicationIntegrity.available)
    {
        availableWeight += WEIGHT_COMMUNICATION_INTEGRITY;
    }

    return clampScore(
        100.0f *
        availableWeight /
        totalCoreWeight);
}

// ============================================================
// REALITY CAPS
//
// Weighted fusion gives a smooth score, while these caps prevent strong,
// persistent evidence from being averaged away by unrelated healthy layers.
// ============================================================

void S3SensorTrustEngine::applyRealityCaps(
    S3SensorTrustAnalysis &analysis,
    const S3SensorAnomalyAnalysis &evidence,
    bool applyDomainDegradation,
    const S3AnomalyResult &anomaly,
    float dataQualityPct,
    float communicationIntegrityPct)
{
    if (evidence.stuckOutputEvidence)
    {
        analysis.trustScorePct =
            std::min(
                analysis.trustScorePct,
                25.0f);
    }

    if (evidence.excessiveNoiseEvidence)
    {
        analysis.trustScorePct =
            std::min(
                analysis.trustScorePct,
                55.0f);
    }

    if (evidence.changedDynamicsEvidence)
    {
        analysis.trustScorePct =
            std::min(
                analysis.trustScorePct,
                70.0f);
    }

    if (evidence.rapidChangeEvidence &&
        !evidence.excessiveNoiseEvidence &&
        !evidence.stuckOutputEvidence)
    {
        analysis.trustScorePct =
            std::min(
                analysis.trustScorePct,
                85.0f);
    }

    if (applyDomainDegradation)
    {
        if (anomaly.surfaceFoulingState ==
                S3SurfaceFoulingState::FoulingCompatibleDegradation &&
            anomaly.surfaceFoulingAssessmentConfidencePct >= 70.0f)
        {
            analysis.trustScorePct =
                std::min(
                    analysis.trustScorePct,
                    55.0f);
        }
        else if (
            anomaly.surfaceFoulingState ==
            S3SurfaceFoulingState::PossibleResponseDegradation)
        {
            analysis.trustScorePct =
                std::min(
                    analysis.trustScorePct,
                    75.0f);
        }
    }

    if (dataQualityPct < 25.0f)
    {
        analysis.trustScorePct =
            std::min(
                analysis.trustScorePct,
                45.0f);
    }
    else if (dataQualityPct < 50.0f)
    {
        analysis.trustScorePct =
            std::min(
                analysis.trustScorePct,
                65.0f);
    }

    if (communicationIntegrityPct < 35.0f)
    {
        analysis.trustScorePct =
            std::min(
                analysis.trustScorePct,
                55.0f);
    }
}

// ============================================================
// PRIMARY HUMAN-READABLE REASON
// ============================================================

S3SensorTrustReason S3SensorTrustEngine::primaryReason(
    const S3SensorAnomalyAnalysis &evidence,
    bool applyDomainDegradation,
    const S3AnomalyResult &anomaly,
    float dataQualityPct,
    float communicationIntegrityPct,
    bool baselineReady,
    bool measurementAvailable)
{
    if (!measurementAvailable)
    {
        return S3SensorTrustReason::MeasurementUnavailable;
    }

    // Before the learned behavioural baseline is ready, immediate cross-layer
    // reliability problems must still be explainable.  Otherwise a sensor can
    // remain indefinitely in "learning" while low upstream quality or a bad
    // transport path is the real reason confidence is reduced.
    if (!baselineReady)
    {
        if (dataQualityPct < 50.0f)
        {
            return S3SensorTrustReason::LowDataQuality;
        }

        if (communicationIntegrityPct < 60.0f)
        {
            return S3SensorTrustReason::CommunicationInstability;
        }

        return S3SensorTrustReason::LearningNormalBehaviour;
    }

    if (evidence.stuckOutputEvidence)
    {
        return S3SensorTrustReason::PossibleStuckOutput;
    }

    if (evidence.excessiveNoiseEvidence)
    {
        return S3SensorTrustReason::ExcessiveMeasurementNoise;
    }

    if (applyDomainDegradation &&
        anomaly.surfaceFoulingState ==
            S3SurfaceFoulingState::FoulingCompatibleDegradation &&
        anomaly.surfaceFoulingAssessmentConfidencePct >= 70.0f)
    {
        return S3SensorTrustReason::FoulingCompatibleResponseDegradation;
    }

    if (applyDomainDegradation &&
        anomaly.surfaceFoulingState ==
            S3SurfaceFoulingState::PossibleResponseDegradation)
    {
        return S3SensorTrustReason::PossibleResponseDegradation;
    }

    if (evidence.changedDynamicsEvidence)
    {
        return S3SensorTrustReason::ChangedResponseDynamics;
    }

    if (dataQualityPct < 50.0f)
    {
        return S3SensorTrustReason::LowDataQuality;
    }

    if (communicationIntegrityPct < 60.0f)
    {
        return S3SensorTrustReason::CommunicationInstability;
    }

    if (evidence.rapidChangeEvidence)
    {
        return S3SensorTrustReason::SuddenMeasurementChange;
    }

    return S3SensorTrustReason::None;
}

// ============================================================
// BUILD ONE SENSOR TRUST RESULT
// ============================================================

S3SensorTrustAnalysis S3SensorTrustEngine::buildSensorTrust(
    const S3SensorAnomalyAnalysis &sensorEvidence,
    float dataQualityPct,
    bool baselineReady,
    float communicationIntegrityPct,
    bool applyDomainDegradation,
    const S3AnomalyResult &anomaly) const
{
    S3SensorTrustAnalysis result;
    result.clear();

    result.measurementAvailable =
        sensorEvidence.available;
    result.baselineReady = baselineReady;

    if (!result.measurementAvailable)
    {
        result.state =
            S3SensorTrustState::MeasurementUnavailable;
        result.primaryReason =
            S3SensorTrustReason::MeasurementUnavailable;
        result.trustScorePct = 0.0f;
        result.evidenceCoveragePct = 0.0f;
        return result;
    }

    result.measurementIntegrity.available = true;
    result.measurementIntegrity.scorePct = 100.0f;

    // qualityScorePct is a protocol-level 0..100 engineering score.  A value
    // outside that contract must fail closed instead of being clamped upward
    // into apparently perfect quality.
    const float validatedDataQualityPct =
        std::isfinite(dataQualityPct) &&
                dataQualityPct >= 0.0f &&
                dataQualityPct <= 100.0f
            ? dataQualityPct
            : 0.0f;

    result.dataQuality.available = true;
    result.dataQuality.scorePct =
        validatedDataQualityPct;

    result.communicationIntegrity.available = true;
    result.communicationIntegrity.scorePct =
        clampScore(communicationIntegrityPct);

    if (baselineReady)
    {
        result.temporalReliability.available = true;
        result.temporalReliability.scorePct =
            temporalReliabilityScore(sensorEvidence);

        result.learnedBehaviourConsistency.available = true;
        result.learnedBehaviourConsistency.scorePct =
            learnedBehaviourScore(sensorEvidence);
    }

    if (applyDomainDegradation &&
        anomaly.surfaceFoulingAssessmentAvailable)
    {
        const float degradationScore =
            domainDegradationScore(anomaly);

        if (std::isfinite(degradationScore))
        {
            result.domainSpecificEvidenceApplied = true;
            result.domainSpecificDegradation.available = true;
            result.domainSpecificDegradation.scorePct =
                degradationScore;
        }
    }

    result.trustScorePct =
        weightedTrust(result);

    result.evidenceCoveragePct =
        evidenceCoverage(result);

    applyRealityCaps(
        result,
        sensorEvidence,
        applyDomainDegradation,
        anomaly,
        validatedDataQualityPct,
        communicationIntegrityPct);

    result.primaryReason =
        primaryReason(
            sensorEvidence,
            applyDomainDegradation,
            anomaly,
            validatedDataQualityPct,
            communicationIntegrityPct,
            baselineReady,
            result.measurementAvailable);

    if (!baselineReady)
    {
        result.state =
            S3SensorTrustState::LearningNormalBehaviour;
    }
    else
    {
        result.state =
            stateFromTrust(
                result.trustScorePct);
    }

    return result;
}

// ============================================================
// COMPLETE UPDATE
// ============================================================

S3SensorTrustResult S3SensorTrustEngine::update(
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
    uint32_t nowMs)
{
    S3SensorTrustResult result;
    result.clear();

    result.sourceSequence = snapshot.sequence;
    result.evaluatedAtMs = nowMs;
    result.baselineReady = anomaly.generalBaselineReady;

    updateCommunicationIntegrity(
        snapshot,
        bleConnected,
        validSensorPackets,
        crcFailures,
        wrongLengths,
        badHeaders,
        badVersions,
        queueDrops,
        reconnectCount);

    result.communicationIntegrityPct =
        communicationIntegrityPct_;

    result.airTemperature =
        buildSensorTrust(
            anomaly.airTemperature,
            snapshot.qualityScorePct,
            anomaly.generalBaselineReady,
            communicationIntegrityPct_,
            false,
            anomaly);

    result.humidity =
        buildSensorTrust(
            anomaly.humidity,
            snapshot.qualityScorePct,
            anomaly.generalBaselineReady,
            communicationIntegrityPct_,
            false,
            anomaly);

    result.waterTemperature =
        buildSensorTrust(
            anomaly.waterTemperature,
            snapshot.qualityScorePct,
            anomaly.generalBaselineReady,
            communicationIntegrityPct_,
            true,
            anomaly);

    result.reservoirDistance =
        buildSensorTrust(
            anomaly.reservoirDistance,
            snapshot.qualityScorePct,
            anomaly.generalBaselineReady,
            communicationIntegrityPct_,
            false,
            anomaly);

    result.allRequiredMeasurementsAvailable =
        result.airTemperature.measurementAvailable &&
        result.humidity.measurementAvailable &&
        result.waterTemperature.measurementAvailable &&
        result.reservoirDistance.measurementAvailable;

    const float dht22ModuleTrust =
        std::min(
            result.airTemperature.trustScorePct,
            result.humidity.trustScorePct);

    result.overallTrustScorePct =
        std::min(
            dht22ModuleTrust,
            std::min(
                result.waterTemperature.trustScorePct,
                result.reservoirDistance.trustScorePct));

    result.overallEvidenceCoveragePct =
        std::min(
            std::min(
                result.airTemperature.evidenceCoveragePct,
                result.humidity.evidenceCoveragePct),
            std::min(
                result.waterTemperature.evidenceCoveragePct,
                result.reservoirDistance.evidenceCoveragePct));

    // Missing required physical measurements outrank baseline-learning status.
    // "Learning" must never hide the fact that system-wide sensor coverage
    // is incomplete.
    if (!result.allRequiredMeasurementsAvailable)
    {
        result.overallState =
            S3SensorTrustState::PartialSensorCoverage;
    }
    else if (!anomaly.generalBaselineReady)
    {
        result.overallState =
            S3SensorTrustState::LearningNormalBehaviour;
    }
    else
    {
        result.overallState =
            stateFromTrust(
                result.overallTrustScorePct);
    }

    result.transportFaultEventsObserved =
        transportFaultEventsObserved_;
    result.sequenceGapEventsObserved =
        sequenceGapEventsObserved_;
    result.reconnectEventsObserved =
        reconnectEventsObserved_;

    return result;
}

// ============================================================
// SCORE / TEXT HELPERS
// ============================================================

float S3SensorTrustEngine::clampScore(float value)
{
    return std::max(
        0.0f,
        std::min(
            100.0f,
            value));
}

S3SensorTrustState S3SensorTrustEngine::stateFromTrust(
    float trustPct)
{
    if (trustPct >= TRUSTED_THRESHOLD)
    {
        return S3SensorTrustState::Trusted;
    }

    if (trustPct >= MONITOR_THRESHOLD)
    {
        return S3SensorTrustState::Monitor;
    }

    if (trustPct >= LOW_TRUST_THRESHOLD)
    {
        return S3SensorTrustState::LowTrust;
    }

    return S3SensorTrustState::Unreliable;
}

const char *S3SensorTrustEngine::trustStateText(
    S3SensorTrustState state)
{
    switch (state)
    {
    case S3SensorTrustState::MeasurementUnavailable:
        return "MEASUREMENT UNAVAILABLE";

    case S3SensorTrustState::LearningNormalBehaviour:
        return "LEARNING NORMAL SENSOR BEHAVIOUR";

    case S3SensorTrustState::Trusted:
        return "MEASUREMENT CURRENTLY TRUSTED";

    case S3SensorTrustState::Monitor:
        return "TRUST REDUCED - MONITOR SENSOR";

    case S3SensorTrustState::LowTrust:
        return "LOW SENSOR TRUST - INSPECTION RECOMMENDED";

    case S3SensorTrustState::Unreliable:
        return "MEASUREMENT NOT RELIABLE FOR OPERATIONAL DECISIONS";

    case S3SensorTrustState::PartialSensorCoverage:
        return "PARTIAL SENSOR COVERAGE - ONE OR MORE MEASUREMENTS UNAVAILABLE";

    default:
        return "UNKNOWN SENSOR TRUST STATE";
    }
}

const char *S3SensorTrustEngine::trustReasonText(
    S3SensorTrustReason reason)
{
    switch (reason)
    {
    case S3SensorTrustReason::None:
        return "No significant reliability problem is currently supported by the available evidence.";

    case S3SensorTrustReason::MeasurementUnavailable:
        return "The sensor did not provide a currently usable measurement.";

    case S3SensorTrustReason::LearningNormalBehaviour:
        return "HydroSense is still learning this sensor's normal behaviour, so the trust assessment is provisional.";

    case S3SensorTrustReason::LowDataQuality:
        return "The upstream data-quality assessment is low, so confidence in this measurement is reduced.";

    case S3SensorTrustReason::CommunicationInstability:
        return "Recent packet-integrity, sequence, queue, or reconnect evidence indicates an unstable C3-to-S3 measurement path.";

    case S3SensorTrustReason::PossibleStuckOutput:
        return "The sensor output remained unusually unchanged while a meaningful external change indicated that a response should have occurred.";

    case S3SensorTrustReason::ExcessiveMeasurementNoise:
        return "Short-term measurement variability is persistently much higher than the learned healthy behaviour.";

    case S3SensorTrustReason::ChangedResponseDynamics:
        return "The sensor is responding differently from its learned healthy dynamic behaviour.";

    case S3SensorTrustReason::SuddenMeasurementChange:
        return "A sudden measurement change was observed; one transient alone is not treated as sensor failure.";

    case S3SensorTrustReason::PossibleResponseDegradation:
        return "The submerged water-temperature probe shows possible response degradation, but the evidence is not yet strong enough for a higher-severity conclusion.";

    case S3SensorTrustReason::FoulingCompatibleResponseDegradation:
        return "Repeated comparable thermal events show response degradation compatible with probe coating, biological fouling, contamination, or ageing; direct algae identification is not claimed.";

    default:
        return "No human-readable reason is available.";
    }
}
