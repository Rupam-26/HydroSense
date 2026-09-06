const mongoose = require("mongoose");

// ============================================================
// HYDROSENSE
// SENSOR TRUST EVENT MODEL
//
// PURPOSE
// ------------------------------------------------------------
// This collection stores meaningful Sensor Trust / anomaly
// transitions only.
//
// It DOES NOT store every high-frequency realtime packet.
//
// Examples of records that belong here:
//
// - Trust state changed
// - Sensor anomaly detected
// - Sensor anomaly cleared
// - Surface-fouling assessment changed
// - C3-to-S3 communication trust changed
//
// IMPORTANT
// ------------------------------------------------------------
// Sensor Trust values are engineering reliability indexes.
// They are NOT probabilities that a sensor is correct.
//
// Surface-fouling evidence is also an engineering evidence
// index. It must NOT be interpreted as probability of algae.
// ============================================================

const sensorTrustEventSchema =
    new mongoose.Schema(
        {
            // ====================================================
            // DEVICE / SOURCE
            // ====================================================

            deviceId:
            {
                type:
                    String,

                required:
                    true,

                trim:
                    true,

                index:
                    true
            },

            // C3 source packet sequence when available.
            sequence:
            {
                type:
                    Number,

                default:
                    null,

                min:
                    0,

                max:
                    255
            },

            // ====================================================
            // SENSOR IDENTITY
            //
            // Examples:
            //
            // overall
            // airTemperature
            // humidity
            // waterTemperature
            // reservoirDistance
            // communication
            // ====================================================

            sensorKey:
            {
                type:
                    String,

                required:
                    true,

                trim:
                    true,

                index:
                    true
            },

            // Human-readable label for the Control Room.
            //
            // Examples:
            //
            // Overall Sensor Trust
            // Air Temperature Sensor
            // Water Temperature Sensor
            // C3-to-S3 Communication
            sensorLabel:
            {
                type:
                    String,

                required:
                    true,

                trim:
                    true
            },

            // ====================================================
            // EVENT CATEGORY
            // ====================================================

            eventType:
            {
                type:
                    String,

                required:
                    true,

                enum:
                [
                    "TRUST_STATE_CHANGED",

                    "SENSOR_ANOMALY_DETECTED",

                    "SENSOR_ANOMALY_CLEARED",

                    "SURFACE_FOULING_ASSESSMENT_CHANGED",

                    "COMMUNICATION_TRUST_CHANGED"
                ],

                index:
                    true
            },

            // ====================================================
            // EVENT SEVERITY
            //
            // INFO
            //      Recovery / normal informational transition.
            //
            // NOTICE
            //      Something unusual should be observed.
            //
            // WARNING
            //      Reliability is meaningfully reduced.
            //
            // CRITICAL
            //      Measurement should not currently be relied upon
            //      for operational decisions.
            // ====================================================

            severity:
            {
                type:
                    String,

                required:
                    true,

                enum:
                [
                    "INFO",
                    "NOTICE",
                    "WARNING",
                    "CRITICAL"
                ],

                default:
                    "INFO",

                index:
                    true
            },

            // ====================================================
            // HUMAN-READABLE EVENT DESCRIPTION
            // ====================================================

            title:
            {
                type:
                    String,

                required:
                    true,

                trim:
                    true
            },

            message:
            {
                type:
                    String,

                required:
                    true,

                trim:
                    true
            },

            // ====================================================
            // STATE TRANSITION
            //
            // Example:
            //
            // fromState:
            // "MEASUREMENT CURRENTLY TRUSTED"
            //
            // toState:
            // "LOW SENSOR TRUST - INSPECTION RECOMMENDED"
            // ====================================================

            fromState:
            {
                type:
                    String,

                default:
                    null,

                trim:
                    true
            },

            toState:
            {
                type:
                    String,

                default:
                    null,

                trim:
                    true
            },

            // ====================================================
            // SENSOR TRUST EVIDENCE
            // ====================================================

            trustScorePct:
            {
                type:
                    Number,

                default:
                    null,

                min:
                    0,

                max:
                    100
            },

            evidenceCoveragePct:
            {
                type:
                    Number,

                default:
                    null,

                min:
                    0,

                max:
                    100
            },

            primaryReason:
            {
                type:
                    String,

                default:
                    null,

                trim:
                    true
            },

            // ====================================================
            // ANOMALY INFORMATION
            //
            // Examples:
            //
            // possibleStuckOutput
            // excessiveMeasurementNoise
            // suddenMeasurementChange
            // changedResponseDynamics
            // suppressedResponseDynamics
            // amplifiedResponseDynamics
            // ====================================================

            anomalyType:
            {
                type:
                    String,

                default:
                    null,

                trim:
                    true
            },

            // ====================================================
            // C3 -> S3 COMMUNICATION TRUST
            // ====================================================

            communicationIntegrityPct:
            {
                type:
                    Number,

                default:
                    null,

                min:
                    0,

                max:
                    100
            },

            // ====================================================
            // SUBMERGED-PROBE DEGRADATION EVIDENCE
            //
            // These are NOT algae probabilities.
            // ====================================================

            surfaceFoulingEvidenceIndexPct:
            {
                type:
                    Number,

                default:
                    null,

                min:
                    0,

                max:
                    100
            },

            surfaceFoulingConfidencePct:
            {
                type:
                    Number,

                default:
                    null,

                min:
                    0,

                max:
                    100
            },

            // ====================================================
            // SOURCE TIME
            //
            // This is when HydroSense observed the event.
            // MongoDB's createdAt separately records when the
            // backend stored the event.
            // ====================================================

            sourceTimestamp:
            {
                type:
                    Date,

                required:
                    true,

                index:
                    true
            }
        },

        {
            // Automatically adds:
            //
            // createdAt
            // updatedAt
            timestamps:
                true,

            // No __v field needed for this event collection.
            versionKey:
                false,

            // Reject fields that are not part of this schema.
            strict:
                true
        }
    );

// ============================================================
// DATABASE INDEXES
//
// These support the queries that the Sensor Trust Control Room
// will perform most frequently.
// ============================================================

// Latest events for one HydroSense device.
sensorTrustEventSchema.index(
    {
        deviceId:
            1,

        sourceTimestamp:
            -1
    }
);

// Latest events for one specific sensor.
sensorTrustEventSchema.index(
    {
        deviceId:
            1,

        sensorKey:
            1,

        sourceTimestamp:
            -1
    }
);

// Severity-filtered event history.
sensorTrustEventSchema.index(
    {
        deviceId:
            1,

        severity:
            1,

        sourceTimestamp:
            -1
    }
);

// Event-type history.
sensorTrustEventSchema.index(
    {
        deviceId:
            1,

        eventType:
            1,

        sourceTimestamp:
            -1
    }
);

// ============================================================
// MODEL EXPORT
// ============================================================

module.exports =
    mongoose.model(
        "SensorTrustEvent",
        sensorTrustEventSchema
    );