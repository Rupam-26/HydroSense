// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

const SensorTrustEvent = require("../models/SensorTrustEvent");

const previousStateByDevice = new Map();
const processingQueueByDevice = new Map();

const SENSOR_DESCRIPTORS = [
    ["overall", "Overall Measurement System"],
    ["airTemperature", "Air Temperature Sensor"],
    ["humidity", "Humidity Sensor"],
    ["waterTemperature", "Water Temperature Sensor"],
    ["reservoirDistance", "Reservoir Distance Sensor"]
];

const ANOMALY_DESCRIPTORS = [
    ["possibleStuckOutput", "Possible stuck sensor output"],
    ["excessiveMeasurementNoise", "Excessive measurement noise"],
    ["changedResponseDynamics", "Changed sensor response dynamics"],
    ["suppressedResponseDynamics", "Suppressed sensor response dynamics"],
    ["amplifiedResponseDynamics", "Amplified sensor response dynamics"],
    ["suddenMeasurementChange", "Sudden measurement change"]
];

// ============================================================
// HELPERS
// ============================================================

function finite(value)
{
    return (
        typeof value === "number" &&
        Number.isFinite(value)
    );
}

function copyBooleanEvidence(evidence)
{
    const result = {};

    for (const [key] of ANOMALY_DESCRIPTORS)
    {
        result[key] =
            evidence?.[key] === true;
    }

    return result;
}

// ============================================================
// SENSOR TRUST SNAPSHOT EXTRACTION
// ============================================================

function sensorTrustState(
    payload,
    sensorKey)
{
    const sensorTrust =
        payload?.sensorTrust;

    if (
        !sensorTrust ||
        typeof sensorTrust !== "object")
    {
        return null;
    }

    if (
        sensorKey === "overall")
    {
        return {
            state:
                sensorTrust.overallState || null,

            trustScorePct:
                finite(
                    sensorTrust.overallTrustScorePct)
                    ? sensorTrust.overallTrustScorePct
                    : null,

            evidenceCoveragePct:
                finite(
                    sensorTrust.overallEvidenceCoveragePct)
                    ? sensorTrust.overallEvidenceCoveragePct
                    : null,

            primaryReason:
                null
        };
    }

    const sensor =
        sensorTrust[sensorKey];

    if (
        !sensor ||
        typeof sensor !== "object")
    {
        return null;
    }

    return {
        state:
            sensor.state || null,

        trustScorePct:
            finite(
                sensor.trustScorePct)
                ? sensor.trustScorePct
                : null,

        evidenceCoveragePct:
            finite(
                sensor.evidenceCoveragePct)
                ? sensor.evidenceCoveragePct
                : null,

        primaryReason:
            typeof sensor.primaryReason === "string"
                ? sensor.primaryReason
                : null
    };
}

// ============================================================
// ANOMALY SNAPSHOT EXTRACTION
// ============================================================

function anomalyState(
    payload,
    sensorKey)
{
    if (
        sensorKey === "overall")
    {
        return {};
    }

    const sensor =
        payload?.anomaly?.[sensorKey];

    return copyBooleanEvidence(
        sensor?.evidence);
}

// ============================================================
// COMMUNICATION HEALTH BAND
// ============================================================

function communicationBand(value)
{
    if (
        !finite(value))
    {
        return "UNKNOWN";
    }

    if (
        value >= 90)
    {
        return "COMMUNICATION PATH HEALTHY";
    }

    if (
        value >= 70)
    {
        return "COMMUNICATION PATH DEGRADED";
    }

    return "COMMUNICATION PATH UNSTABLE";
}

// ============================================================
// SEVERITY MAPPING
// ============================================================

function trustSeverity(state)
{
    if (
        state ===
        "MEASUREMENT NOT RELIABLE FOR OPERATIONAL DECISIONS")
    {
        return "CRITICAL";
    }

    if (
        state ===
        "LOW SENSOR TRUST - INSPECTION RECOMMENDED")
    {
        return "WARNING";
    }

    if (
        state ===
            "TRUST REDUCED - MONITOR SENSOR" ||
        state ===
            "PARTIAL SENSOR COVERAGE - ONE OR MORE MEASUREMENTS UNAVAILABLE")
    {
        return "NOTICE";
    }

    return "INFO";
}

function anomalySeverity(
    anomalyKey)
{
    if (
        anomalyKey ===
        "possibleStuckOutput")
    {
        return "WARNING";
    }

    if (
        anomalyKey ===
            "excessiveMeasurementNoise" ||
        anomalyKey ===
            "changedResponseDynamics" ||
        anomalyKey ===
            "suppressedResponseDynamics" ||
        anomalyKey ===
            "amplifiedResponseDynamics")
    {
        return "NOTICE";
    }

    return "INFO";
}

function foulingSeverity(state)
{
    if (
        state ===
        "FOULING-COMPATIBLE RESPONSE DEGRADATION - INSPECT PROBE")
    {
        return "WARNING";
    }

    if (
        state ===
        "POSSIBLE WATER-PROBE RESPONSE DEGRADATION - MONITOR")
    {
        return "NOTICE";
    }

    return "INFO";
}

// ============================================================
// CREATE COMPACT STATE SNAPSHOT
// ============================================================

function snapshotPayload(payload)
{
    const trustSensors = {};
    const anomalySensors = {};

    for (
        const [sensorKey]
        of SENSOR_DESCRIPTORS)
    {
        trustSensors[sensorKey] =
            sensorTrustState(
                payload,
                sensorKey);

        anomalySensors[sensorKey] =
            anomalyState(
                payload,
                sensorKey);
    }

    return {
        trustSensors,

        anomalySensors,

        communicationBand:
            communicationBand(
                payload?.sensorTrust
                    ?.communicationIntegrityPct),

        communicationIntegrityPct:
            finite(
                payload?.sensorTrust
                    ?.communicationIntegrityPct)
                ? payload.sensorTrust
                    .communicationIntegrityPct
                : null,

        foulingState:
            payload?.anomaly
                ?.surfaceFouling
                ?.state || null
    };
}

// ============================================================
// BASE EVENT
// ============================================================

function baseEvent(
    payload,
    receivedAt,
    sensorKey,
    sensorLabel)
{
    return {
        deviceId:
            payload.deviceId,

        sequence:
            Number.isInteger(
                payload.sequence)
                ? payload.sequence
                : null,

        sensorKey,

        sensorLabel,

        communicationIntegrityPct:
            finite(
                payload?.sensorTrust
                    ?.communicationIntegrityPct)
                ? payload.sensorTrust
                    .communicationIntegrityPct
                : null,

        sourceTimestamp:
            receivedAt
    };
}

// ============================================================
// TRUST STATE TRANSITIONS
// ============================================================

function buildTrustEvents(
    payload,
    receivedAt,
    previous,
    current)
{
    const events = [];

    for (
        const [
            sensorKey,
            sensorLabel
        ]
        of SENSOR_DESCRIPTORS)
    {
        const before =
            previous
                .trustSensors[
                    sensorKey
                ];

        const after =
            current
                .trustSensors[
                    sensorKey
                ];

        if (
            !after?.state ||
            !before?.state ||
            before.state ===
                after.state)
        {
            continue;
        }

        const recovered =
            after.state ===
                "MEASUREMENT CURRENTLY TRUSTED" &&
            before.state !==
                "MEASUREMENT CURRENTLY TRUSTED";

        events.push(
            {
                ...baseEvent(
                    payload,
                    receivedAt,
                    sensorKey,
                    sensorLabel),

                eventType:
                    "TRUST_STATE_CHANGED",

                severity:
                    recovered
                        ? "INFO"
                        : trustSeverity(
                            after.state),

                title:
                    recovered
                        ? `${sensorLabel} trust recovered`
                        : `${sensorLabel} trust state changed`,

                message:
                    recovered
                        ? `Sensor Trust returned from “${before.state}” to “${after.state}”.`
                        : `Sensor Trust changed from “${before.state}” to “${after.state}”.`,

                fromState:
                    before.state,

                toState:
                    after.state,

                trustScorePct:
                    after
                        .trustScorePct,

                evidenceCoveragePct:
                    after
                        .evidenceCoveragePct,

                primaryReason:
                    after
                        .primaryReason
            });
    }

    return events;
}

// ============================================================
// ANOMALY TRANSITIONS
// ============================================================

function buildAnomalyEvents(
    payload,
    receivedAt,
    previous,
    current)
{
    const events = [];

    for (
        const [
            sensorKey,
            sensorLabel
        ]
        of SENSOR_DESCRIPTORS)
    {
        if (
            sensorKey ===
            "overall")
        {
            continue;
        }

        const before =
            previous
                .anomalySensors[
                    sensorKey
                ] || {};

        const after =
            current
                .anomalySensors[
                    sensorKey
                ] || {};

        for (
            const [
                anomalyKey,
                anomalyLabel
            ]
            of ANOMALY_DESCRIPTORS)
        {
            const wasActive =
                before[
                    anomalyKey
                ] === true;

            const isActive =
                after[
                    anomalyKey
                ] === true;

            if (
                wasActive ===
                isActive)
            {
                continue;
            }

            const trust =
                current
                    .trustSensors[
                        sensorKey
                    ];

            events.push(
                {
                    ...baseEvent(
                        payload,
                        receivedAt,
                        sensorKey,
                        sensorLabel),

                    eventType:
                        isActive
                            ? "SENSOR_ANOMALY_DETECTED"
                            : "SENSOR_ANOMALY_CLEARED",

                    severity:
                        isActive
                            ? anomalySeverity(
                                anomalyKey)
                            : "INFO",

                    title:
                        isActive
                            ? `${anomalyLabel} detected`
                            : `${anomalyLabel} cleared`,

                    message:
                        isActive
                            ? `${sensorLabel}: ${anomalyLabel} is supported by the current edge evidence.`
                            : `${sensorLabel}: the previous ${anomalyLabel.toLowerCase()} evidence is no longer active.`,

                    fromState:
                        wasActive
                            ? "ACTIVE"
                            : "INACTIVE",

                    toState:
                        isActive
                            ? "ACTIVE"
                            : "INACTIVE",

                    trustScorePct:
                        trust
                            ?.trustScorePct
                            ?? null,

                    evidenceCoveragePct:
                        trust
                            ?.evidenceCoveragePct
                            ?? null,

                    primaryReason:
                        trust
                            ?.primaryReason
                            ?? null,

                    anomalyType:
                        anomalyLabel
                });
        }
    }

    return events;
}

// ============================================================
// SUBMERGED-PROBE DEGRADATION EVENTS
//
// This is response-degradation evidence.
// It is not direct algae identification.
// ============================================================

function buildFoulingEvents(
    payload,
    receivedAt,
    previous,
    current)
{
    if (
        !current.foulingState ||
        !previous.foulingState ||
        current.foulingState ===
            previous.foulingState)
    {
        return [];
    }

    const surface =
        payload
            ?.anomaly
            ?.surfaceFouling
        || {};

    const waterTrust =
        current
            .trustSensors
            .waterTemperature;

    return [
        {
            ...baseEvent(
                payload,
                receivedAt,
                "waterTemperature",
                "Water Temperature Sensor"),

            eventType:
                "SURFACE_FOULING_ASSESSMENT_CHANGED",

            severity:
                foulingSeverity(
                    current
                        .foulingState),

            title:
                "Submerged probe degradation assessment changed",

            message:
                `Surface-fouling assessment changed from “${previous.foulingState}” to “${current.foulingState}”. HydroSense is assessing response degradation; it is not directly identifying algae.`,

            fromState:
                previous
                    .foulingState,

            toState:
                current
                    .foulingState,

            trustScorePct:
                waterTrust
                    ?.trustScorePct
                    ?? null,

            evidenceCoveragePct:
                waterTrust
                    ?.evidenceCoveragePct
                    ?? null,

            primaryReason:
                waterTrust
                    ?.primaryReason
                    ?? null,

            surfaceFoulingEvidenceIndexPct:
                finite(
                    surface
                        .evidenceIndexPct)
                    ? surface
                        .evidenceIndexPct
                    : null,

            surfaceFoulingConfidencePct:
                finite(
                    surface
                        .assessmentConfidencePct)
                    ? surface
                        .assessmentConfidencePct
                    : null
        }
    ];
}

// ============================================================
// COMMUNICATION TRUST TRANSITIONS
// ============================================================

function buildCommunicationEvents(
    payload,
    receivedAt,
    previous,
    current)
{
    if (
        current.communicationBand ===
            "UNKNOWN" ||
        previous.communicationBand ===
            "UNKNOWN" ||
        current.communicationBand ===
            previous.communicationBand)
    {
        return [];
    }

    const severity =
        current.communicationBand ===
            "COMMUNICATION PATH UNSTABLE"
            ? "WARNING"
            :
        current.communicationBand ===
            "COMMUNICATION PATH DEGRADED"
            ? "NOTICE"
            : "INFO";

    return [
        {
            ...baseEvent(
                payload,
                receivedAt,
                "communication",
                "C3-to-S3 Communication"),

            eventType:
                "COMMUNICATION_TRUST_CHANGED",

            severity,

            title:
                "C3-to-S3 communication integrity changed",

            message:
                `Communication integrity changed from “${previous.communicationBand}” to “${current.communicationBand}”.`,

            fromState:
                previous
                    .communicationBand,

            toState:
                current
                    .communicationBand,

            communicationIntegrityPct:
                current
                    .communicationIntegrityPct
        }
    ];
}

// ============================================================
// INTERNAL REALTIME TRUST PROCESSOR
// ============================================================

async function processRealtimeTrustInternal(
    payload,
    receivedAt)
{
    const current =
        snapshotPayload(
            payload);

    const previous =
        previousStateByDevice.get(
            payload.deviceId);

    // First packet seeds the state without creating a false
    // transition event after a backend restart.
    if (
        !previous)
    {
        previousStateByDevice.set(
            payload.deviceId,
            current);

        return [];
    }

    const pending = [
        ...buildTrustEvents(
            payload,
            receivedAt,
            previous,
            current),

        ...buildAnomalyEvents(
            payload,
            receivedAt,
            previous,
            current),

        ...buildFoulingEvents(
            payload,
            receivedAt,
            previous,
            current),

        ...buildCommunicationEvents(
            payload,
            receivedAt,
            previous,
            current)
    ];

    if (
        pending.length === 0)
    {
        previousStateByDevice.set(
            payload.deviceId,
            current);

        return [];
    }

    const inserted =
        await SensorTrustEvent.insertMany(
            pending,
            {
                ordered:
                    true
            });

    // Update memory only after MongoDB successfully stores every
    // event. A failed write can therefore be retried later.
    previousStateByDevice.set(
        payload.deviceId,
        current);

    return inserted.map(
        (event) =>
            event.toObject());
}

// ============================================================
// PUBLIC REALTIME TRUST PROCESSOR
//
// Packets belonging to the same device are processed in order.
// Different HydroSense devices can still run independently.
// ============================================================

async function processRealtimeTrust(
    payload,
    receivedAt = new Date())
{
    if (
        !payload?.deviceId ||
        !payload.sensorTrust ||
        typeof payload.sensorTrust !==
            "object")
    {
        return [];
    }

    const deviceId =
        payload.deviceId;

    const previousTask =
        processingQueueByDevice.get(
            deviceId) ||
        Promise.resolve();

    const currentTask =
        previousTask
            .catch(
                () =>
                {
                    // A failed database operation must not block
                    // later packets for this device.
                })
            .then(
                () =>
                    processRealtimeTrustInternal(
                        payload,
                        receivedAt));

    const queueTail =
        currentTask.then(
            () => undefined,
            () => undefined);

    processingQueueByDevice.set(
        deviceId,
        queueTail);

    void queueTail.then(
        () =>
        {
            if (
                processingQueueByDevice.get(
                    deviceId) === queueTail)
            {
                processingQueueByDevice.delete(
                    deviceId);
            }
        });

    return currentTask;
}

// ============================================================
// EXPORT
// ============================================================

module.exports = {
    processRealtimeTrust
};