#include "anomaly/S3AnomalyEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Resolution-aware unchanged thresholds.
    constexpr float AIR_UNCHANGED_EPSILON_C = 0.05f;
    constexpr float HUMIDITY_UNCHANGED_EPSILON_PCT = 0.05f;
    constexpr float WATER_UNCHANGED_EPSILON_C = 0.04f;
    constexpr float DISTANCE_UNCHANGED_EPSILON_CM = 0.05f;

    // Minimum practical noise floors. They prevent a tiny baseline
    // variance from making ordinary quantisation look anomalous.
    constexpr float AIR_MINIMUM_NOISE_DIFFERENCE_C = 0.12f;
    constexpr float HUMIDITY_MINIMUM_NOISE_DIFFERENCE_PCT = 0.45f;
    constexpr float WATER_MINIMUM_NOISE_DIFFERENCE_C = 0.06f;
    constexpr float DISTANCE_MINIMUM_NOISE_DIFFERENCE_CM = 0.35f;

    // One-sample changes large enough to be worth noting. A single
    // event is not automatically treated as sensor failure.
    constexpr float AIR_RAPID_CHANGE_FLOOR_C = 1.50f;
    constexpr float HUMIDITY_RAPID_CHANGE_FLOOR_PCT = 10.0f;
    constexpr float WATER_RAPID_CHANGE_FLOOR_C = 0.90f;
    constexpr float DISTANCE_RAPID_CHANGE_FLOOR_CM = 2.50f;

    // Strong external movement is required before a flat water probe
    // is considered suspicious. Water can legitimately remain stable.
    constexpr float WATER_REFERENCE_AIR_RANGE_C = 1.20f;

    // The DHT22's temperature and humidity are from the same physical
    // device, so they are not used as independent proof against each
    // other for hard stuck-sensor decisions.

    // Persistence requirements. With ~3 s packets, 120 unchanged
    // samples corresponds to roughly six minutes.
    constexpr uint16_t WATER_STUCK_MINIMUM_SAMPLES = 120;

    // Clean evidence admission. This is intentionally the SAME
    // threshold already proven by the Block-5 thermal-event logic.
    // It is not a new sensor-validity threshold and does not clamp data.
    constexpr float MINIMUM_CLEAN_EVIDENCE_QUALITY_PCT = 85.0f;

    // Thermal-event eligibility for fouling analysis.
    constexpr float THERMAL_EVENT_MINIMUM_AIR_CHANGE_C = 0.50f;
    constexpr float THERMAL_EVENT_MINIMUM_WATER_CHANGE_C = 0.05f;

    // Reservoir movement changes thermal mass and can also indicate
    // refill/drain activity, so those periods are excluded from the
    // ambient-response fouling inference.
    constexpr float MAXIMUM_THERMAL_EVENT_DISTANCE_RANGE_CM = 0.80f;

    // Do not evaluate near-duplicate thermal events every few seconds.
    constexpr uint32_t THERMAL_EVENT_MINIMUM_INTERVAL_MS = 240000UL;

    // Current event should be broadly comparable with the clean events.
    constexpr float MINIMUM_COMPARABLE_EXCITATION_RATIO = 0.50f;
    constexpr float MAXIMUM_COMPARABLE_EXCITATION_RATIO = 2.00f;
    constexpr float MINIMUM_COMPARABLE_RESERVOIR_TOLERANCE_CM = 1.50f;

    // A real attenuation event must be substantially lower than the
    // clean response and outside ordinary clean-event variability.
    constexpr float RESPONSE_ATTENUATION_FRACTION_OF_CLEAN = 0.60f;
    constexpr float RESPONSE_ATTENUATION_STANDARD_DEVIATIONS = 2.0f;

    constexpr float THERMAL_RELATIONSHIP_MINIMUM_SHIFT_C = 1.20f;
    constexpr float WATER_DYNAMIC_SUPPRESSION_RATIO = 0.35f;

    // A serious fouling-compatible conclusion requires repeated events.
    constexpr uint16_t MINIMUM_ATTENUATION_EVENTS_FOR_PERSISTENCE = 4;

    // General score thresholds are intentionally conservative.
    constexpr float SENSOR_MONITOR_SCORE = 30.0f;
    constexpr float SENSOR_WARNING_SCORE = 65.0f;

    float medianOfArray(float *values, uint16_t count)
    {
        if (count == 0)
        {
            return NAN;
        }

        std::sort(values, values + count);

        if ((count & 1U) != 0U)
        {
            return values[count / 2];
        }

        return 0.5f *
               (values[count / 2 - 1] +
                values[count / 2]);
    }
}

// ============================================================
// RUNNING STATISTICS
// ============================================================

void S3AnomalyEngine::RunningStats::clear()
{
    count = 0;
    mean = 0.0;
    m2 = 0.0;
}

void S3AnomalyEngine::RunningStats::add(float value)
{
    if (!std::isfinite(value))
    {
        return;
    }

    ++count;

    const double delta =
        static_cast<double>(value) - mean;

    mean +=
        delta / static_cast<double>(count);

    const double delta2 =
        static_cast<double>(value) - mean;

    m2 +=
        delta * delta2;
}

float S3AnomalyEngine::RunningStats::standardDeviation() const
{
    if (count < 2)
    {
        return NAN;
    }

    return static_cast<float>(
        std::sqrt(
            m2 /
            static_cast<double>(count - 1)));
}

// ============================================================
// SENSOR TRACKER
// ============================================================

void S3AnomalyEngine::SensorTracker::clear()
{
    for (uint16_t index = 0; index < WINDOW_SIZE; ++index)
    {
        values[index] = NAN;
        valid[index] = false;
    }

    writeIndex = 0;
    sampleCount = 0;
    previousValid = false;
    previousValue = NAN;
    consecutiveNearlyUnchanged = 0;

    baselinePreviousValid = false;
    baselinePreviousValue = NAN;

    baselineValue.clear();
    baselineDifference.clear();

    stuckPersistence = 0;
    noisePersistence = 0;
    dynamicsPersistence = 0;
}

// ============================================================
// INITIALIZATION / RESET
// ============================================================

bool S3AnomalyEngine::begin()
{
    resetBaseline();
    return true;
}

void S3AnomalyEngine::resetBaseline()
{
    airTemperature_.clear();
    humidity_.clear();
    waterTemperature_.clear();
    reservoirDistance_.clear();

    baselineAirWaterDelta_.clear();
    baselineThermalResponseRatio_.clear();
    baselineThermalAirChangeMagnitude_.clear();
    baselineThermalReservoirDistance_.clear();

    validGeneralSamples_ = 0;
    generalBaselineReady_ = false;

    thermalResponseAssessmentEvents_ = 0;
    eligibleThermalEventsObserved_ = 0;
    rejectedThermalEvents_ = 0;

    recentThermalEventWriteIndex_ = 0;
    recentThermalEventCount_ = 0;
    persistentResponseAttenuationEvidence_ = false;
    lastThermalEventEvaluationMs_ = 0;

    for (uint16_t index = 0;
         index < RECENT_THERMAL_EVENT_MEMORY;
         ++index)
    {
        recentResponseAttenuation_[index] = false;
    }
}

bool S3AnomalyEngine::baselineReady() const
{
    return generalBaselineReady_;
}

// ============================================================
// EVIDENCE PERSISTENCE
// ============================================================

void S3AnomalyEngine::updatePersistence(
    uint8_t &counter,
    bool evidence,
    uint8_t maximum)
{
    if (evidence)
    {
        if (counter < maximum)
        {
            ++counter;
        }
    }
    else if (counter > 0)
    {
        --counter;
    }
}

// ============================================================
// PUSH SENSOR VALUE
// ============================================================

void S3AnomalyEngine::pushSensorValue(
    SensorTracker &tracker,
    float value,
    bool valueValid,
    float unchangedEpsilon,
    bool baselineEligible)
{
    const bool finiteValue =
        valueValid && std::isfinite(value);

    tracker.values[tracker.writeIndex] =
        finiteValue ? value : NAN;

    tracker.valid[tracker.writeIndex] =
        finiteValue;

    tracker.writeIndex =
        (tracker.writeIndex + 1) % WINDOW_SIZE;

    if (tracker.sampleCount < WINDOW_SIZE)
    {
        ++tracker.sampleCount;
    }

    // --------------------------------------------------------
    // CLEAN-BASELINE LEARNING
    //
    // Live tracking and baseline learning are intentionally
    // separate. Genuine current values remain visible to the
    // anomaly windows even when upstream quality is poor, but
    // poor/incomplete evidence is never allowed to teach the
    // startup reference what "healthy" behaviour looks like.
    // --------------------------------------------------------

    if (!generalBaselineReady_)
    {
        if (finiteValue && baselineEligible)
        {
            tracker.baselineValue.add(value);

            if (tracker.baselinePreviousValid)
            {
                tracker.baselineDifference.add(
                    value - tracker.baselinePreviousValue);
            }

            tracker.baselinePreviousValid = true;
            tracker.baselinePreviousValue = value;
        }
        else
        {
            // Never bridge a clean baseline derivative across an
            // ineligible packet or unavailable measurement.
            tracker.baselinePreviousValid = false;
            tracker.baselinePreviousValue = NAN;
        }
    }

    // --------------------------------------------------------
    // LIVE TEMPORAL TRACKING
    // --------------------------------------------------------

    if (!finiteValue)
    {
        tracker.previousValid = false;
        tracker.consecutiveNearlyUnchanged = 0;
        return;
    }

    if (tracker.previousValid)
    {
        const float difference =
            value - tracker.previousValue;

        if (std::fabs(difference) <= unchangedEpsilon)
        {
            if (tracker.consecutiveNearlyUnchanged < UINT16_MAX)
            {
                ++tracker.consecutiveNearlyUnchanged;
            }
        }
        else
        {
            tracker.consecutiveNearlyUnchanged = 0;
        }
    }
    else
    {
        tracker.consecutiveNearlyUnchanged = 0;
    }

    tracker.previousValid = true;
    tracker.previousValue = value;
}

// ============================================================
// WINDOW STATISTICS
// ============================================================

S3AnomalyEngine::WindowStats
S3AnomalyEngine::calculateWindowStats(
    const SensorTracker &tracker) const
{
    WindowStats result{};

    if (tracker.sampleCount == 0)
    {
        return result;
    }

    float ordered[WINDOW_SIZE];
    uint16_t count = 0;

    const uint16_t oldestIndex =
        tracker.sampleCount < WINDOW_SIZE
            ? 0
            : tracker.writeIndex;

    for (uint16_t offset = 0;
         offset < tracker.sampleCount;
         ++offset)
    {
        const uint16_t index =
            (oldestIndex + offset) % WINDOW_SIZE;

        if (tracker.valid[index] &&
            std::isfinite(tracker.values[index]))
        {
            ordered[count++] = tracker.values[index];
        }
    }

    if (count == 0)
    {
        return result;
    }

    double sum = 0.0;
    float minimum = ordered[0];
    float maximum = ordered[0];

    for (uint16_t index = 0; index < count; ++index)
    {
        sum += ordered[index];
        minimum = std::min(minimum, ordered[index]);
        maximum = std::max(maximum, ordered[index]);
    }

    const double mean =
        sum / static_cast<double>(count);

    double varianceAccumulator = 0.0;

    for (uint16_t index = 0; index < count; ++index)
    {
        const double delta =
            static_cast<double>(ordered[index]) - mean;

        varianceAccumulator += delta * delta;
    }

    float standardDeviation = 0.0f;

    if (count > 1)
    {
        standardDeviation =
            static_cast<float>(
                std::sqrt(
                    varianceAccumulator /
                    static_cast<double>(count - 1)));
    }

    // Linear-regression slope against sample order.
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumXX = 0.0;

    for (uint16_t index = 0; index < count; ++index)
    {
        const double x = static_cast<double>(index);
        const double y = static_cast<double>(ordered[index]);

        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
    }

    float slope = 0.0f;

    if (count > 1)
    {
        const double denominator =
            static_cast<double>(count) * sumXX -
            sumX * sumX;

        if (std::fabs(denominator) > 1.0e-12)
        {
            slope =
                static_cast<float>(
                    (static_cast<double>(count) * sumXY -
                     sumX * sumY) /
                    denominator);
        }
    }

    RunningStats differenceStats;
    differenceStats.clear();

    float absoluteDifferences[WINDOW_SIZE];
    uint16_t differenceCount = 0;

    for (uint16_t index = 1; index < count; ++index)
    {
        const float difference =
            ordered[index] - ordered[index - 1];

        differenceStats.add(difference);
        absoluteDifferences[differenceCount++] =
            std::fabs(difference);
    }

    result.valid = true;
    result.count = count;
    result.mean = static_cast<float>(mean);
    result.standardDeviation = standardDeviation;
    result.minimum = minimum;
    result.maximum = maximum;
    result.range = maximum - minimum;
    result.slopePerSample = slope;

    result.differenceStandardDeviation =
        differenceStats.count >= 2
            ? differenceStats.standardDeviation()
            : 0.0f;

    result.medianAbsoluteDifference =
        differenceCount > 0
            ? medianOfArray(
                  absoluteDifferences,
                  differenceCount)
            : 0.0f;

    result.latestDifference =
        count >= 2
            ? ordered[count - 1] - ordered[count - 2]
            : 0.0f;

    return result;
}

// ============================================================
// REALITY-ORIENTED SENSOR ANALYSIS
// ============================================================

S3SensorAnomalyAnalysis S3AnomalyEngine::analyseSensor(
    SensorTracker &tracker,
    bool available,
    float currentValue,
    float minimumNoiseDifference,
    float rapidChangeFloor,
    bool suppressionReferenceMoved,
    uint16_t minimumStuckSamples,
    bool stuckAssessmentAllowed)
{
    S3SensorAnomalyAnalysis result;
    result.clear();

    result.available =
        available && std::isfinite(currentValue);

    result.currentValue =
        result.available ? currentValue : NAN;

    result.baselineReady =
        generalBaselineReady_;

    result.consecutiveNearlyUnchangedSamples =
        tracker.consecutiveNearlyUnchanged;

    if (!result.available)
    {
        result.condition =
            S3SensorCondition::MeasurementUnavailable;

        tracker.stuckPersistence = 0;
        tracker.noisePersistence = 0;
        tracker.dynamicsPersistence = 0;
        return result;
    }

    const WindowStats window =
        calculateWindowStats(tracker);

    if (window.valid)
    {
        result.availableWindowSamples = window.count;
        result.shortTermMean = window.mean;
        result.shortTermStandardDeviation = window.standardDeviation;
        result.shortTermRange = window.range;
        result.shortTermSlopePerSample = window.slopePerSample;
        result.shortTermDifferenceStandardDeviation =
            window.differenceStandardDeviation;
        result.shortTermMedianAbsoluteDifference =
            window.medianAbsoluteDifference;
    }

    result.baselineDifferenceStandardDeviation =
        tracker.baselineDifference.standardDeviation();

    if (!generalBaselineReady_)
    {
        result.condition =
            S3SensorCondition::LearningBaseline;

        result.anomalyScorePct = 0.0f;
        return result;
    }

    const float baselineDiffStd =
        std::isfinite(result.baselineDifferenceStandardDeviation)
            ? result.baselineDifferenceStandardDeviation
            : 0.0f;

    // --------------------------------------------------------
    // Candidate stuck evidence.
    // A flat signal is only suspicious when there is a meaningful
    // external reason to expect response. Stable hydroponic water is
    // allowed to remain stable for long periods.
    // --------------------------------------------------------

    const bool stuckCandidate =
        stuckAssessmentAllowed &&
        suppressionReferenceMoved &&
        minimumStuckSamples > 0 &&
        tracker.consecutiveNearlyUnchanged >= minimumStuckSamples;

    updatePersistence(
        tracker.stuckPersistence,
        stuckCandidate);

    result.stuckOutputEvidence =
        tracker.stuckPersistence >= 3;

    // --------------------------------------------------------
    // Noise must be elevated relative to baseline AND exceed a
    // practical floor. Median absolute difference prevents a single
    // isolated spike from being mistaken for sustained noise.
    // --------------------------------------------------------

    const float noiseStdThreshold =
        std::max(
            minimumNoiseDifference,
            baselineDiffStd * 3.8f);

    const float robustNoiseThreshold =
        minimumNoiseDifference * 0.45f;

    const bool noiseCandidate =
        window.valid &&
        window.count >= 30 &&
        window.differenceStandardDeviation > noiseStdThreshold &&
        window.medianAbsoluteDifference > robustNoiseThreshold;

    updatePersistence(
        tracker.noisePersistence,
        noiseCandidate);

    result.excessiveNoiseEvidence =
        tracker.noisePersistence >= 3;

    // --------------------------------------------------------
    // Rapid change is reported immediately as unusual behaviour, but
    // by itself it is NOT enough to call the sensor failed. Real refill,
    // environmental or handling events can legitimately move signals.
    // --------------------------------------------------------

    const float rapidThreshold =
        std::max(
            rapidChangeFloor,
            baselineDiffStd * 7.0f);

    result.rapidChangeEvidence =
        window.valid &&
        std::fabs(window.latestDifference) > rapidThreshold;

    // --------------------------------------------------------
    // Changed dynamics are split into amplification and suppression.
    // Suppression is only suspicious when another driver has moved;
    // otherwise a quiet environment is simply a quiet environment.
    // --------------------------------------------------------

    const bool baselineDynamicEnough =
        baselineDiffStd > minimumNoiseDifference * 0.20f;

    const bool amplifiedCandidate =
        baselineDynamicEnough &&
        window.valid &&
        window.count >= 40 &&
        window.differenceStandardDeviation >
            baselineDiffStd * 3.2f;

    const bool suppressedCandidate =
        baselineDynamicEnough &&
        suppressionReferenceMoved &&
        window.valid &&
        window.count >= 40 &&
        window.differenceStandardDeviation <
            baselineDiffStd * 0.25f;

    updatePersistence(
        tracker.dynamicsPersistence,
        amplifiedCandidate || suppressedCandidate);

    result.amplifiedDynamicsEvidence =
        tracker.dynamicsPersistence >= 3 && amplifiedCandidate;

    result.suppressedDynamicsEvidence =
        tracker.dynamicsPersistence >= 3 && suppressedCandidate;

    result.changedDynamicsEvidence =
        result.amplifiedDynamicsEvidence ||
        result.suppressedDynamicsEvidence;

    result.stuckEvidencePersistence =
        tracker.stuckPersistence;

    result.noiseEvidencePersistence =
        tracker.noisePersistence;

    result.dynamicsEvidencePersistence =
        tracker.dynamicsPersistence;

    float score = 0.0f;

    if (result.stuckOutputEvidence)
    {
        score += 70.0f;
    }

    if (result.excessiveNoiseEvidence)
    {
        score += 45.0f;
    }

    if (result.changedDynamicsEvidence)
    {
        score += 30.0f;
    }

    if (result.rapidChangeEvidence)
    {
        // A single rapid event is deliberately a weaker contribution.
        score += 15.0f;
    }

    result.anomalyScorePct =
        clampScore(score);

    result.condition =
        conditionFromScore(
            result.anomalyScorePct);

    return result;
}

// ============================================================
// CLEAN GENERAL BASELINE
// ============================================================

bool S3AnomalyEngine::generalBaselineSampleEligible(
    const S3SensorSnapshot &snapshot,
    const S3FeatureData &features) const
{
    // The snapshot remains authoritative for sensor availability.
    // Feature validity/source coherence ensures the derived thermal
    // relation belongs to this same genuine packet.
    return snapshot.coreSensorsAvailable() &&
           features.valid &&
           features.sourceSequence == snapshot.sequence &&
           features.thermalFeaturesValid &&
           std::isfinite(features.airWaterDeltaC) &&
           std::isfinite(snapshot.qualityScorePct) &&
           snapshot.qualityScorePct >=
               MINIMUM_CLEAN_EVIDENCE_QUALITY_PCT;
}

void S3AnomalyEngine::updateBaseline(
    const S3FeatureData &features,
    bool baselineEligible)
{
    if (generalBaselineReady_ ||
        !baselineEligible)
    {
        return;
    }

    if (validGeneralSamples_ < UINT16_MAX)
    {
        ++validGeneralSamples_;
    }

    // Eligibility already guarantees a finite, coherent thermal feature.
    baselineAirWaterDelta_.add(
        features.airWaterDeltaC);

    if (validGeneralSamples_ >= GENERAL_BASELINE_SAMPLES)
    {
        generalBaselineReady_ = true;
    }
}

// ============================================================
// HISTORY ACCESS
// ============================================================

bool S3AnomalyEngine::getValueFromSamplesAgo(
    const SensorTracker &tracker,
    uint16_t samplesAgo,
    float &value) const
{
    if (tracker.sampleCount == 0 ||
        samplesAgo >= tracker.sampleCount)
    {
        return false;
    }

    const uint16_t newestIndex =
        (tracker.writeIndex + WINDOW_SIZE - 1) % WINDOW_SIZE;

    const uint16_t index =
        (newestIndex + WINDOW_SIZE - samplesAgo) % WINDOW_SIZE;

    if (!tracker.valid[index] ||
        !std::isfinite(tracker.values[index]))
    {
        return false;
    }

    value = tracker.values[index];
    return true;
}

// ============================================================
// PHYSICALLY ELIGIBLE THERMAL RESPONSE ANALYSIS
// ============================================================

void S3AnomalyEngine::updateThermalResponseModel(
    const S3SensorSnapshot &snapshot,
    const S3FeatureData &features,
    const WindowStats &airWindow,
    const WindowStats &waterWindow,
    const WindowStats &distanceWindow,
    uint32_t nowMs,
    S3AnomalyResult &result)
{
    result.requiredThermalResponseBaselineEvents =
        REQUIRED_THERMAL_BASELINE_EVENTS;

    result.thermalResponseBaselineEvents =
        static_cast<uint16_t>(
            std::min<uint32_t>(
                baselineThermalResponseRatio_.count,
                UINT16_MAX));

    result.thermalResponseAssessmentEvents =
        thermalResponseAssessmentEvents_;

    result.eligibleThermalEventsObserved =
        eligibleThermalEventsObserved_;

    result.rejectedThermalEvents =
        rejectedThermalEvents_;

    result.responseAttenuationEvidence =
        persistentResponseAttenuationEvidence_;

    if (!generalBaselineReady_)
    {
        result.surfaceFoulingState =
            S3SurfaceFoulingState::LearningCleanResponse;
        return;
    }

    const bool waterSignalUsable =
        snapshot.waterTemperatureAvailable() &&
        !result.waterTemperature.excessiveNoiseEvidence &&
        !result.waterTemperature.stuckOutputEvidence;

    result.competingWaterSensorFaultEvidence =
        !waterSignalUsable;

    if (!snapshot.airEnvironmentAvailable() ||
        !snapshot.waterTemperatureAvailable() ||
        !snapshot.distanceAvailable() ||
        !waterSignalUsable)
    {
        result.surfaceFoulingState =
            S3SurfaceFoulingState::AssessmentNotPossible;
        return;
    }

    if (lastThermalEventEvaluationMs_ != 0 &&
        nowMs - lastThermalEventEvaluationMs_ <
            THERMAL_EVENT_MINIMUM_INTERVAL_MS)
    {
        return;
    }

    float oldAir = NAN;
    float oldWater = NAN;

    const bool oldAirAvailable =
        getValueFromSamplesAgo(
            airTemperature_,
            THERMAL_EVENT_WINDOW,
            oldAir);

    const bool oldWaterAvailable =
        getValueFromSamplesAgo(
            waterTemperature_,
            THERMAL_EVENT_WINDOW,
            oldWater);

    if (!oldAirAvailable || !oldWaterAvailable)
    {
        result.surfaceFoulingState =
            baselineThermalResponseRatio_.count >=
                    REQUIRED_THERMAL_BASELINE_EVENTS
                ? S3SurfaceFoulingState::InsufficientThermalEvidence
                : S3SurfaceFoulingState::LearningCleanResponse;
        return;
    }

    const float airChange =
        snapshot.airTemperatureC - oldAir;

    const float waterChange =
        snapshot.waterTemperatureC - oldWater;

    result.thermalExcitationStrongEnough =
        std::fabs(airChange) >=
        THERMAL_EVENT_MINIMUM_AIR_CHANGE_C;

    result.reservoirThermallyStable =
        distanceWindow.valid &&
        distanceWindow.count >= 40 &&
        distanceWindow.range <=
            MAXIMUM_THERMAL_EVENT_DISTANCE_RANGE_CM;

    result.thermalResponseDirectionCoherent =
        std::fabs(waterChange) >=
            THERMAL_EVENT_MINIMUM_WATER_CHANGE_C &&
        airChange * waterChange > 0.0f;

    // If the reservoir level changes materially, a refill/drain event may
    // dominate water temperature. That is useful operational information,
    // but it is NOT a clean biofouling experiment, so exclude it here.
    const bool dataQualityAcceptable =
        std::isfinite(snapshot.qualityScorePct) &&
        snapshot.qualityScorePct >=
            MINIMUM_CLEAN_EVIDENCE_QUALITY_PCT;

    const bool eventEligible =
        result.thermalExcitationStrongEnough &&
        result.reservoirThermallyStable &&
        result.thermalResponseDirectionCoherent &&
        dataQualityAcceptable &&
        airWindow.valid &&
        waterWindow.valid;

    result.currentThermalEventEligible =
        eventEligible;

    if (!eventEligible)
    {
        if (result.thermalExcitationStrongEnough)
        {
            if (rejectedThermalEvents_ < UINT16_MAX)
            {
                ++rejectedThermalEvents_;
            }

            result.rejectedThermalEvents =
                rejectedThermalEvents_;
        }

        result.surfaceFoulingState =
            baselineThermalResponseRatio_.count >=
                    REQUIRED_THERMAL_BASELINE_EVENTS
                ? S3SurfaceFoulingState::InsufficientThermalEvidence
                : S3SurfaceFoulingState::LearningCleanResponse;
        return;
    }

    lastThermalEventEvaluationMs_ = nowMs;

    if (eligibleThermalEventsObserved_ < UINT16_MAX)
    {
        ++eligibleThermalEventsObserved_;
    }

    result.eligibleThermalEventsObserved =
        eligibleThermalEventsObserved_;

    const float responseRatio =
        std::fabs(waterChange) /
        std::max(
            std::fabs(airChange),
            0.001f);

    result.currentThermalResponseRatio =
        responseRatio;

    // --------------------------------------------------------
    // Learn clean response only from physically eligible events.
    // --------------------------------------------------------

    if (baselineThermalResponseRatio_.count <
        REQUIRED_THERMAL_BASELINE_EVENTS)
    {
        baselineThermalResponseRatio_.add(responseRatio);
        baselineThermalAirChangeMagnitude_.add(
            std::fabs(airChange));
        baselineThermalReservoirDistance_.add(
            snapshot.stableDistanceCm);

        result.thermalResponseBaselineEvents =
            static_cast<uint16_t>(
                baselineThermalResponseRatio_.count);

        result.cleanThermalResponseRatio =
            static_cast<float>(
                baselineThermalResponseRatio_.mean);

        result.surfaceFoulingState =
            S3SurfaceFoulingState::LearningCleanResponse;
        return;
    }

    const float cleanResponseRatio =
        static_cast<float>(
            baselineThermalResponseRatio_.mean);

    const float cleanResponseStd =
        baselineThermalResponseRatio_.standardDeviation();

    const float cleanAirChange =
        static_cast<float>(
            baselineThermalAirChangeMagnitude_.mean);

    const float cleanReservoirDistance =
        static_cast<float>(
            baselineThermalReservoirDistance_.mean);

    const float cleanReservoirDistanceStd =
        baselineThermalReservoirDistance_.standardDeviation();

    result.cleanThermalResponseRatio =
        cleanResponseRatio;

    if (cleanResponseRatio > 0.001f)
    {
        result.responseRatioRelativeToCleanPct =
            100.0f *
            responseRatio /
            cleanResponseRatio;
    }

    // --------------------------------------------------------
    // Compare only similar excitations and similar reservoir state.
    // This reduces false fouling alarms caused by different thermal
    // mass or wildly different ambient disturbances.
    // --------------------------------------------------------

    const float excitationRatio =
        cleanAirChange > 0.001f
            ? std::fabs(airChange) /
                  cleanAirChange
            : 1.0f;

    const float reservoirTolerance =
        std::max(
            MINIMUM_COMPARABLE_RESERVOIR_TOLERANCE_CM,
            std::isfinite(cleanReservoirDistanceStd)
                ? cleanReservoirDistanceStd * 3.0f
                : MINIMUM_COMPARABLE_RESERVOIR_TOLERANCE_CM);

    const bool comparableExcitation =
        excitationRatio >=
            MINIMUM_COMPARABLE_EXCITATION_RATIO &&
        excitationRatio <=
            MAXIMUM_COMPARABLE_EXCITATION_RATIO;

    const bool comparableReservoir =
        std::fabs(
            snapshot.stableDistanceCm -
            cleanReservoirDistance) <=
        reservoirTolerance;

    result.thermalEventComparableToBaseline =
        comparableExcitation &&
        comparableReservoir;

    if (!result.thermalEventComparableToBaseline)
    {
        if (rejectedThermalEvents_ < UINT16_MAX)
        {
            ++rejectedThermalEvents_;
        }

        result.rejectedThermalEvents =
            rejectedThermalEvents_;

        result.surfaceFoulingState =
            S3SurfaceFoulingState::InsufficientThermalEvidence;
        return;
    }

    if (thermalResponseAssessmentEvents_ < UINT16_MAX)
    {
        ++thermalResponseAssessmentEvents_;
    }

    result.thermalResponseAssessmentEvents =
        thermalResponseAssessmentEvents_;

    const float fractionThreshold =
        cleanResponseRatio *
        RESPONSE_ATTENUATION_FRACTION_OF_CLEAN;

    const float statisticalThreshold =
        std::isfinite(cleanResponseStd)
            ? cleanResponseRatio -
                  RESPONSE_ATTENUATION_STANDARD_DEVIATIONS *
                      cleanResponseStd
            : fractionThreshold;

    const float attenuationThreshold =
        std::min(
            fractionThreshold,
            statisticalThreshold);

    const bool attenuation =
        responseRatio <
        std::max(
            0.01f,
            attenuationThreshold);

    recentResponseAttenuation_[recentThermalEventWriteIndex_] =
        attenuation;

    recentThermalEventWriteIndex_ =
        (recentThermalEventWriteIndex_ + 1) %
        RECENT_THERMAL_EVENT_MEMORY;

    if (recentThermalEventCount_ <
        RECENT_THERMAL_EVENT_MEMORY)
    {
        ++recentThermalEventCount_;
    }

    uint16_t attenuationCount = 0;

    for (uint16_t index = 0;
         index < recentThermalEventCount_;
         ++index)
    {
        if (recentResponseAttenuation_[index])
        {
            ++attenuationCount;
        }
    }

    persistentResponseAttenuationEvidence_ =
        recentThermalEventCount_ >=
            REQUIRED_THERMAL_ASSESSMENT_EVENTS &&
        attenuationCount >=
            MINIMUM_ATTENUATION_EVENTS_FOR_PERSISTENCE;

    result.responseAttenuationEvidence =
        persistentResponseAttenuationEvidence_;

    // --------------------------------------------------------
    // Supporting thermal relationship evidence.
    // It is intentionally secondary because air-water difference can
    // change for genuine environmental reasons.
    // --------------------------------------------------------

    if (features.thermalFeaturesValid &&
        std::isfinite(features.airWaterDeltaC) &&
        baselineAirWaterDelta_.count >= 30)
    {
        const float baselineDeltaMean =
            static_cast<float>(
                baselineAirWaterDelta_.mean);

        const float baselineDeltaStd =
            baselineAirWaterDelta_.standardDeviation();

        const float shiftThreshold =
            std::max(
                THERMAL_RELATIONSHIP_MINIMUM_SHIFT_C,
                std::isfinite(baselineDeltaStd)
                    ? baselineDeltaStd * 4.0f
                    : THERMAL_RELATIONSHIP_MINIMUM_SHIFT_C);

        result.thermalRelationshipShiftEvidence =
            std::fabs(
                features.airWaterDeltaC -
                baselineDeltaMean) >
            shiftThreshold;
    }

    // --------------------------------------------------------
    // Supporting response-suppression evidence.
    // Only considered during a strong, stable, comparable event.
    // --------------------------------------------------------

    const float baselineWaterDiffStd =
        waterTemperature_.baselineDifference.standardDeviation();

    if (std::isfinite(baselineWaterDiffStd) &&
        baselineWaterDiffStd > 0.005f &&
        waterWindow.valid)
    {
        result.waterResponseSuppressionEvidence =
            waterWindow.differenceStandardDeviation <
            baselineWaterDiffStd *
                WATER_DYNAMIC_SUPPRESSION_RATIO;
    }
}

// ============================================================
// FOULING EVIDENCE INDEX
// ============================================================

float S3AnomalyEngine::calculateSurfaceFoulingEvidenceIndex(
    const S3AnomalyResult &result) const
{
    if (!result.surfaceFoulingAssessmentAvailable ||
        result.competingWaterSensorFaultEvidence)
    {
        return 0.0f;
    }

    // Main evidence: repeated attenuation across multiple comparable
    // thermal events. Without this, the score cannot become high.
    float score =
        result.responseAttenuationEvidence
            ? 70.0f
            : 0.0f;

    // Supporting evidence only. These are partially correlated with
    // attenuation, so their weights are deliberately limited.
    if (result.waterResponseSuppressionEvidence)
    {
        score += 20.0f;
    }

    if (result.thermalRelationshipShiftEvidence)
    {
        score += 10.0f;
    }

    // If the water sensor is noisy or showing abrupt electrical-like
    // behaviour, surface fouling is not the preferred explanation.
    if (result.waterTemperature.excessiveNoiseEvidence)
    {
        score -= 25.0f;
    }

    if (result.waterTemperature.rapidChangeEvidence)
    {
        score -= 10.0f;
    }

    // No repeated attenuation -> maximum "monitor" evidence.
    if (!result.responseAttenuationEvidence)
    {
        score = std::min(score, 35.0f);
    }

    return clampScore(score);
}

// ============================================================
// FOULING ASSESSMENT CONFIDENCE
// ============================================================

float S3AnomalyEngine::calculateFoulingAssessmentConfidence(
    const S3SensorSnapshot &snapshot,
    const S3AnomalyResult &result) const
{
    if (baselineThermalResponseRatio_.count <
        REQUIRED_THERMAL_BASELINE_EVENTS)
    {
        return 0.0f;
    }

    float confidence = 40.0f;

    confidence +=
        std::min(
            30.0f,
            static_cast<float>(
                result.thermalResponseAssessmentEvents) *
                5.0f);

    if (snapshot.qualityScorePct >= 90.0f)
    {
        confidence += 10.0f;
    }

    if (result.reservoirThermallyStable)
    {
        confidence += 10.0f;
    }

    if (result.thermalEventComparableToBaseline)
    {
        confidence += 10.0f;
    }

    if (result.competingWaterSensorFaultEvidence)
    {
        confidence = std::min(confidence, 25.0f);
    }

    return clampScore(confidence);
}

// ============================================================
// COMPLETE UPDATE
// ============================================================

S3AnomalyResult S3AnomalyEngine::update(
    const S3SensorSnapshot &snapshot,
    const S3FeatureData &features,
    const S3TDSResult &tdsResult,
    uint32_t nowMs)
{
    S3AnomalyResult result;
    result.clear();

    result.sourceSequence = snapshot.sequence;
    result.evaluatedAtMs = nowMs;
    result.requiredGeneralBaselineSamples =
        GENERAL_BASELINE_SAMPLES;
    result.requiredThermalResponseBaselineEvents =
        REQUIRED_THERMAL_BASELINE_EVENTS;

    const bool airValid =
        snapshot.airEnvironmentAvailable() &&
        std::isfinite(snapshot.airTemperatureC);

    const bool humidityValid =
        snapshot.airEnvironmentAvailable() &&
        std::isfinite(snapshot.humidityPct);

    const bool waterValid =
        snapshot.waterTemperatureAvailable() &&
        std::isfinite(snapshot.waterTemperatureC);

    const bool distanceValid =
        snapshot.distanceAvailable() &&
        std::isfinite(snapshot.stableDistanceCm);

    const bool baselineEligible =
        !generalBaselineReady_ &&
        generalBaselineSampleEligible(
            snapshot,
            features);

    pushSensorValue(
        airTemperature_,
        snapshot.airTemperatureC,
        airValid,
        AIR_UNCHANGED_EPSILON_C,
        baselineEligible);

    pushSensorValue(
        humidity_,
        snapshot.humidityPct,
        humidityValid,
        HUMIDITY_UNCHANGED_EPSILON_PCT,
        baselineEligible);

    pushSensorValue(
        waterTemperature_,
        snapshot.waterTemperatureC,
        waterValid,
        WATER_UNCHANGED_EPSILON_C,
        baselineEligible);

    pushSensorValue(
        reservoirDistance_,
        snapshot.stableDistanceCm,
        distanceValid,
        DISTANCE_UNCHANGED_EPSILON_CM,
        baselineEligible);

    updateBaseline(
        features,
        baselineEligible);

    result.generalBaselineReady =
        generalBaselineReady_;

    result.generalBaselineSamples =
        validGeneralSamples_;

    const WindowStats airWindow =
        calculateWindowStats(airTemperature_);

    const WindowStats waterWindow =
        calculateWindowStats(waterTemperature_);

    const WindowStats distanceWindow =
        calculateWindowStats(reservoirDistance_);

    // DHT22 temperature and humidity are the same physical module.
    // We deliberately do not use one channel as hard independent proof
    // that the other channel is stuck.
    const bool noHardDhtStuckReference = false;

    const bool waterReferenceMoved =
        airWindow.valid &&
        airWindow.range >=
            WATER_REFERENCE_AIR_RANGE_C;

    // HC-SR04 may legitimately remain unchanged for a long time in a
    // stable reservoir. Without a second independent level sensor,
    // flatness alone is never labelled a stuck ultrasonic sensor.
    const bool noHardDistanceStuckReference = false;

    result.airTemperature =
        analyseSensor(
            airTemperature_,
            airValid,
            snapshot.airTemperatureC,
            AIR_MINIMUM_NOISE_DIFFERENCE_C,
            AIR_RAPID_CHANGE_FLOOR_C,
            false,
            0,
            noHardDhtStuckReference);

    result.humidity =
        analyseSensor(
            humidity_,
            humidityValid,
            snapshot.humidityPct,
            HUMIDITY_MINIMUM_NOISE_DIFFERENCE_PCT,
            HUMIDITY_RAPID_CHANGE_FLOOR_PCT,
            false,
            0,
            noHardDhtStuckReference);

    result.waterTemperature =
        analyseSensor(
            waterTemperature_,
            waterValid,
            snapshot.waterTemperatureC,
            WATER_MINIMUM_NOISE_DIFFERENCE_C,
            WATER_RAPID_CHANGE_FLOOR_C,
            waterReferenceMoved,
            WATER_STUCK_MINIMUM_SAMPLES,
            true);

    result.reservoirDistance =
        analyseSensor(
            reservoirDistance_,
            distanceValid,
            snapshot.stableDistanceCm,
            DISTANCE_MINIMUM_NOISE_DIFFERENCE_CM,
            DISTANCE_RAPID_CHANGE_FLOOR_CM,
            false,
            0,
            noHardDistanceStuckReference);

    updateThermalResponseModel(
        snapshot,
        features,
        airWindow,
        waterWindow,
        distanceWindow,
        nowMs,
        result);

    result.thermalResponseBaselineEvents =
        static_cast<uint16_t>(
            std::min<uint32_t>(
                baselineThermalResponseRatio_.count,
                UINT16_MAX));

    result.thermalResponseAssessmentEvents =
        thermalResponseAssessmentEvents_;

    result.eligibleThermalEventsObserved =
        eligibleThermalEventsObserved_;

    result.rejectedThermalEvents =
        rejectedThermalEvents_;

    result.surfaceFoulingAssessmentAvailable =
        generalBaselineReady_ &&
        !result.competingWaterSensorFaultEvidence &&
        baselineThermalResponseRatio_.count >=
            REQUIRED_THERMAL_BASELINE_EVENTS &&
        thermalResponseAssessmentEvents_ >=
            REQUIRED_THERMAL_ASSESSMENT_EVENTS;

    result.surfaceFoulingAssessmentConfidencePct =
        calculateFoulingAssessmentConfidence(
            snapshot,
            result);

    result.surfaceFoulingRiskIndexPct =
        calculateSurfaceFoulingEvidenceIndex(
            result);

    if (!generalBaselineReady_ ||
        baselineThermalResponseRatio_.count <
            REQUIRED_THERMAL_BASELINE_EVENTS)
    {
        result.surfaceFoulingState =
            S3SurfaceFoulingState::LearningCleanResponse;
    }
    else if (result.competingWaterSensorFaultEvidence)
    {
        result.surfaceFoulingState =
            S3SurfaceFoulingState::AssessmentNotPossible;
    }
    else if (!result.surfaceFoulingAssessmentAvailable)
    {
        result.surfaceFoulingState =
            S3SurfaceFoulingState::InsufficientThermalEvidence;
    }
    else if (result.surfaceFoulingRiskIndexPct >= 65.0f &&
             result.surfaceFoulingAssessmentConfidencePct >= 70.0f)
    {
        result.surfaceFoulingState =
            S3SurfaceFoulingState::FoulingCompatibleDegradation;
    }
    else if (result.surfaceFoulingRiskIndexPct >= 35.0f)
    {
        result.surfaceFoulingState =
            S3SurfaceFoulingState::PossibleResponseDegradation;
    }
    else
    {
        result.surfaceFoulingState =
            S3SurfaceFoulingState::LowEvidence;
    }

    // TDS is derived from the same sensor stream, therefore it remains
    // context only and is never double-counted as independent evidence.
    result.tinyMlTdsAvailable =
        tdsResult.valid();

    result.tinyMlInputSaturated =
        tdsResult.inputSaturated;

    // The strongest sensor condition defines the overall sensor warning.
    // This prevents one bad sensor from being averaged away by others.
    result.overallSensorAnomalyScorePct =
        std::max(
            std::max(
                result.airTemperature.anomalyScorePct,
                result.humidity.anomalyScorePct),
            std::max(
                result.waterTemperature.anomalyScorePct,
                result.reservoirDistance.anomalyScorePct));

    result.overallCondition =
        generalBaselineReady_
            ? conditionFromScore(
                  result.overallSensorAnomalyScorePct)
            : S3SensorCondition::LearningBaseline;

    return result;
}

// ============================================================
// SCORE / TEXT HELPERS
// ============================================================

float S3AnomalyEngine::clampScore(float value)
{
    return std::max(
        0.0f,
        std::min(
            100.0f,
            value));
}

S3SensorCondition S3AnomalyEngine::conditionFromScore(
    float scorePct)
{
    if (scorePct >= SENSOR_WARNING_SCORE)
    {
        return S3SensorCondition::Warning;
    }

    if (scorePct >= SENSOR_MONITOR_SCORE)
    {
        return S3SensorCondition::Monitor;
    }

    return S3SensorCondition::Normal;
}

const char *S3AnomalyEngine::sensorConditionText(
    S3SensorCondition condition)
{
    switch (condition)
    {
    case S3SensorCondition::MeasurementUnavailable:
        return "MEASUREMENT UNAVAILABLE";

    case S3SensorCondition::LearningBaseline:
        return "LEARNING NORMAL SENSOR BEHAVIOUR";

    case S3SensorCondition::Normal:
        return "NORMAL SENSOR BEHAVIOUR";

    case S3SensorCondition::Monitor:
        return "UNUSUAL SENSOR BEHAVIOUR - MONITOR";

    case S3SensorCondition::Warning:
        return "PERSISTENT SENSOR RELIABILITY WARNING";

    default:
        return "UNKNOWN SENSOR CONDITION";
    }
}

const char *S3AnomalyEngine::surfaceFoulingStateText(
    S3SurfaceFoulingState state)
{
    switch (state)
    {
    case S3SurfaceFoulingState::LearningCleanResponse:
        return "LEARNING CLEAN WATER-PROBE RESPONSE";

    case S3SurfaceFoulingState::AssessmentNotPossible:
        return "FOULING ASSESSMENT PAUSED - SENSOR SIGNAL UNRELIABLE";

    case S3SurfaceFoulingState::InsufficientThermalEvidence:
        return "NOT ENOUGH COMPARABLE THERMAL EVENTS YET";

    case S3SurfaceFoulingState::LowEvidence:
        return "NO SIGNIFICANT FOULING-COMPATIBLE DEGRADATION";

    case S3SurfaceFoulingState::PossibleResponseDegradation:
        return "POSSIBLE WATER-PROBE RESPONSE DEGRADATION - MONITOR";

    case S3SurfaceFoulingState::FoulingCompatibleDegradation:
        return "FOULING-COMPATIBLE RESPONSE DEGRADATION - INSPECT PROBE";

    default:
        return "UNKNOWN SURFACE-FOULING ASSESSMENT";
    }
}
