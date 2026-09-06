#include "diagnostics/SensorHealthManager.h"

// ============================================================
// POLICY NORMALIZATION
// ============================================================

SensorHealthPolicy SensorHealthManager::sanitizePolicy(
    SensorHealthPolicy policy)
{
    // Zero is allowed only as an explicit immediate-expiry policy.
    // The important invariant is that FAULT cannot occur before the
    // end of the LIVE window.
    if (policy.faultTimeoutMs < policy.liveTimeoutMs)
    {
        policy.faultTimeoutMs = policy.liveTimeoutMs;
    }

    return policy;
}

// ============================================================
// CONSTRUCTOR
// ============================================================

SensorHealthManager::SensorHealthManager(
    SensorHealthPolicy dht22Policy,
    SensorHealthPolicy ds18b20Policy,
    SensorHealthPolicy ultrasonicPolicy)
    : dht22Timing(
          sanitizePolicy(dht22Policy)),
      ds18b20Timing(
          sanitizePolicy(ds18b20Policy)),
      ultrasonicTiming(
          sanitizePolicy(ultrasonicPolicy))
{
    reset();
}

// ============================================================
// RESET
// ============================================================

void SensorHealthManager::reset()
{
    dht22Health = InternalHealth{};
    ds18b20Health = InternalHealth{};
    ultrasonicHealth = InternalHealth{};
}

// ============================================================
// GENERIC PHYSICAL-ATTEMPT UPDATE
// ============================================================

void SensorHealthManager::updateSensor(
    InternalHealth &health,
    bool valid,
    uint32_t timestampMs)
{
    health.lastReadValid = valid;

    if (valid)
    {
        health.everValid = true;
        health.lastValidTimestampMs = timestampMs;
        ++health.successfulReads;
        health.consecutiveFailures = 0;
        return;
    }

    ++health.failedReads;
    ++health.consecutiveFailures;
}

void SensorHealthManager::updateDHT22(
    bool valid,
    uint32_t timestampMs)
{
    updateSensor(
        dht22Health,
        valid,
        timestampMs);
}

void SensorHealthManager::updateDS18B20(
    bool valid,
    uint32_t timestampMs)
{
    updateSensor(
        ds18b20Health,
        valid,
        timestampMs);
}

void SensorHealthManager::updateUltrasonic(
    bool valid,
    uint32_t timestampMs)
{
    updateSensor(
        ultrasonicHealth,
        valid,
        timestampMs);
}

// ============================================================
// PUBLIC HEALTH STATE
//
// This deliberately follows the first confirmed testing-phase
// principle:
//
// last genuine sample age <= live window  -> LIVE / usable
// live window < age <= fault window       -> STALE / unusable
// age > fault window                      -> FAULT / unusable
//
// A failed attempt is still recorded in failure counters and
// availability, but it does not erase a still-fresh genuine sample.
// ============================================================

SensorHealth SensorHealthManager::buildSensorHealth(
    const InternalHealth &health,
    const SensorHealthPolicy &policy,
    uint32_t nowMs) const
{
    SensorHealth result{};

    result.successfulReads =
        health.successfulReads;

    result.failedReads =
        health.failedReads;

    result.consecutiveFailures =
        health.consecutiveFailures;

    result.lastValidTimestampMs =
        health.lastValidTimestampMs;

    if (!health.everValid)
    {
        result.state = SensorState::NEVER;
        result.valid = false;
        result.ageMs = 0;
    }
    else
    {
        // Unsigned subtraction is intentionally wrap-safe for millis().
        result.ageMs =
            nowMs - health.lastValidTimestampMs;

        if (result.ageMs <= policy.liveTimeoutMs)
        {
            result.state = SensorState::LIVE;
            result.valid = true;
        }
        else if (result.ageMs <= policy.faultTimeoutMs)
        {
            result.state = SensorState::STALE;
            result.valid = false;
        }
        else
        {
            result.state = SensorState::FAULT;
            result.valid = false;
        }
    }

    const uint32_t totalReads =
        health.successfulReads +
        health.failedReads;

    if (totalReads > 0)
    {
        result.availabilityPct =
            (static_cast<float>(
                 health.successfulReads) /
             static_cast<float>(
                 totalReads)) *
            100.0f;
    }
    else
    {
        result.availabilityPct = 0.0f;
    }

    return result;
}

// ============================================================
// NODE SNAPSHOT
// ============================================================

NodeHealthData SensorHealthManager::snapshot(
    uint32_t nowMs,
    bool calibrationValid,
    bool reservoirValid) const
{
    NodeHealthData result{};

    result.uptimeMs = nowMs;

    result.dht22 =
        buildSensorHealth(
            dht22Health,
            dht22Timing,
            nowMs);

    result.ds18b20 =
        buildSensorHealth(
            ds18b20Health,
            ds18b20Timing,
            nowMs);

    result.ultrasonic =
        buildSensorHealth(
            ultrasonicHealth,
            ultrasonicTiming,
            nowMs);

    result.calibrationValid =
        calibrationValid;

    result.reservoirValid =
        reservoirValid;

    result.allCriticalSensorsValid =
        result.dht22.valid &&
        result.ds18b20.valid &&
        result.ultrasonic.valid;

    result.nodeHealthy =
        result.allCriticalSensorsValid;

    return result;
}
