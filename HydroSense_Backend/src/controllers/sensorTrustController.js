const SensorTrustEvent = require("../models/SensorTrustEvent");
const liveStateService = require("../services/liveStateService");

// ============================================================
// HYDROSENSE SENSOR TRUST CONTROLLER
//
// ENDPOINTS PROVIDED THROUGH ROUTES:
//
// GET /api/v1/sensor-trust/latest
// GET /api/v1/sensor-trust/events
//
// This controller exposes:
//
// 1. CURRENT LIVE Sensor Trust state
//    from backend memory.
//
// 2. HISTORICAL Sensor Trust transition events
//    from MongoDB.
//
// IMPORTANT:
// The realtime trust state itself is NOT written to MongoDB
// every few seconds.
//
// Only meaningful transitions are stored by
// sensorTrustEventService.js.
// ============================================================

// ============================================================
// DATE PARSER
// ============================================================

function parseDate(
    value,
    fieldName)
{
    if (!value)
    {
        return null;
    }

    const date =
        new Date(value);

    if (
        Number.isNaN(
            date.getTime()))
    {
        const error =
            new Error(
                `${fieldName} must be a valid ISO date/time`
            );

        error.statusCode =
            400;

        throw error;
    }

    return date;
}

// ============================================================
// GET CURRENT SENSOR TRUST
//
// Example:
//
// /api/v1/sensor-trust/latest
//      ?deviceId=HydroSense_S3_01
//
// Source:
//
// liveStateService
//
// This means the response represents the latest realtime
// packet received since the backend started.
// ============================================================

async function getLatestSensorTrust(
    req,
    res,
    next)
{
    try
    {
        const deviceId =
            String(
                req.query.deviceId || ""
            ).trim();

        // ----------------------------------------------------
        // DEVICE ID REQUIRED
        // ----------------------------------------------------

        if (
            !deviceId)
        {
            return res
                .status(400)
                .json(
                    {
                        success:
                            false,

                        message:
                            "deviceId is required"
                    });
        }

        // ----------------------------------------------------
        // READ LATEST LIVE STATE
        // ----------------------------------------------------

        const live =
            liveStateService
                .getLiveState(
                    deviceId);

        // ----------------------------------------------------
        // NO SENSOR TRUST RECEIVED YET
        // ----------------------------------------------------

        if (
            !live
                ?.data
                ?.sensorTrust)
        {
            return res
                .status(404)
                .json(
                    {
                        success:
                            false,

                        message:
                            "No Sensor Trust data has been received for this device since backend startup"
                    });
        }

        // ----------------------------------------------------
        // RETURN LIVE SENSOR TRUST
        // ----------------------------------------------------

        return res
            .status(200)
            .json(
                {
                    success:
                        true,

                    deviceId,

                    // Backend-side connectivity interpretation.
                    backendStatus:
                        live.backendStatus,

                    // Age of the most recent packet according to
                    // the backend.
                    backendPacketAgeMs:
                        live.backendPacketAgeMs,

                    // Time the backend received the packet.
                    receivedAt:
                        live.receivedAt,

                    data:
                    {
                        // Cross-layer Sensor Trust output
                        // generated on ESP32-S3.
                        sensorTrust:
                            live.data
                                .sensorTrust,

                        // Edge anomaly / degradation evidence.
                        anomaly:
                            live.data
                                .anomaly
                            || null,

                        // Genuine measured sensor values.
                        measured:
                            live.data
                                .measured
                            || null,

                        // Upstream data quality information.
                        quality:
                            live.data
                                .quality
                            || null,

                        // C3 source sequence.
                        sequence:
                            live.data
                                .sequence,

                        // Prefer S3/backend payload timestamp if
                        // available; otherwise use reception time.
                        serverTimestamp:
                            live.data
                                .serverTimestamp
                            ||
                            live.receivedAt
                    }
                });
    }
    catch (error)
    {
        return next(error);
    }
}

// ============================================================
// GET SENSOR TRUST EVENT HISTORY
//
// Example:
//
// /api/v1/sensor-trust/events
//      ?deviceId=HydroSense_S3_01
//      &limit=100
//
// Optional filters:
//
// sensorKey
// eventType
// from
// to
//
// Example:
//
// /api/v1/sensor-trust/events
//      ?deviceId=HydroSense_S3_01
//      &sensorKey=waterTemperature
//      &eventType=TRUST_STATE_CHANGED
//      &limit=50
// ============================================================

async function getSensorTrustEvents(
    req,
    res,
    next)
{
    try
    {
        const deviceId =
            String(
                req.query.deviceId || ""
            ).trim();

        // ----------------------------------------------------
        // DEVICE ID REQUIRED
        // ----------------------------------------------------

        if (
            !deviceId)
        {
            return res
                .status(400)
                .json(
                    {
                        success:
                            false,

                        message:
                            "deviceId is required"
                    });
        }

        // ----------------------------------------------------
        // LIMIT
        //
        // Default : 100
        // Minimum : 1
        // Maximum : 500
        // ----------------------------------------------------

        const requestedLimit =
            Number(
                req.query.limit || 100
            );

        const limit =
            Number.isFinite(
                requestedLimit)
                ?
                Math.min(
                    Math.max(
                        Math.floor(
                            requestedLimit),
                        1),
                    500)
                :
                100;

        // ----------------------------------------------------
        // DATABASE FILTER
        // ----------------------------------------------------

        const filter =
        {
            deviceId
        };

        // ----------------------------------------------------
        // OPTIONAL TIME RANGE
        // ----------------------------------------------------

        const from =
            parseDate(
                req.query.from,
                "from");

        const to =
            parseDate(
                req.query.to,
                "to");

        if (
            from ||
            to)
        {
            filter.sourceTimestamp =
                {};

            if (
                from)
            {
                filter
                    .sourceTimestamp
                    .$gte =
                    from;
            }

            if (
                to)
            {
                filter
                    .sourceTimestamp
                    .$lte =
                    to;
            }
        }

        // ----------------------------------------------------
        // OPTIONAL SENSOR FILTER
        //
        // Examples:
        //
        // overall
        // airTemperature
        // humidity
        // waterTemperature
        // reservoirDistance
        // communication
        // ----------------------------------------------------

        if (
            req.query.sensorKey)
        {
            filter.sensorKey =
                String(
                    req.query.sensorKey
                ).trim();
        }

        // ----------------------------------------------------
        // OPTIONAL EVENT TYPE FILTER
        //
        // Examples:
        //
        // TRUST_STATE_CHANGED
        // SENSOR_ANOMALY_DETECTED
        // SENSOR_ANOMALY_CLEARED
        // SURFACE_FOULING_ASSESSMENT_CHANGED
        // COMMUNICATION_TRUST_CHANGED
        // ----------------------------------------------------

        if (
            req.query.eventType)
        {
            filter.eventType =
                String(
                    req.query.eventType
                ).trim();
        }

        // ----------------------------------------------------
        // DATABASE QUERY
        //
        // Latest events first.
        // ----------------------------------------------------

        const data =
            await SensorTrustEvent
                .find(filter)
                .sort(
                    {
                        sourceTimestamp:
                            -1
                    })
                .limit(limit)
                .lean();

        // ----------------------------------------------------
        // RESPONSE
        // ----------------------------------------------------

        return res
            .status(200)
            .json(
                {
                    success:
                        true,

                    query:
                    {
                        deviceId,

                        sensorKey:
                            filter.sensorKey
                            || null,

                        eventType:
                            filter.eventType
                            || null,

                        from:
                            from
                                ? from.toISOString()
                                : null,

                        to:
                            to
                                ? to.toISOString()
                                : null,

                        limit
                    },

                    count:
                        data.length,

                    data
                });
    }
    catch (error)
    {
        return next(error);
    }
}

// ============================================================
// EXPORT
// ============================================================

module.exports =
{
    getLatestSensorTrust,
    getSensorTrustEvents
};