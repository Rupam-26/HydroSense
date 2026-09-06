const liveStateService = require("../services/liveStateService");
const deviceService = require("../services/deviceService");
const sensorTrustEventService = require("../services/sensorTrustEventService");
const { validateRealtimeTelemetry } = require("../utils/telemetryValidation");
const mlRuntimeService = require("../services/mlRuntimeService");

async function receiveRealtime(req, res, next) {
    try {
        const errors = validateRealtimeTelemetry(req.body);

        if (errors.length > 0) {
            return res.status(400).json({
                success: false,
                message: "Invalid HydroSense real-time payload",
                errors
            });
        }

        const serverTimestamp = new Date();

        // Preserve the S3 payload exactly. The backend only adds its receive
        // timestamp; it never substitutes missing sensor values or invents
        // Sensor Trust/anomaly evidence.
        const liveData = {
            ...req.body,
            serverTimestamp: serverTimestamp.toISOString()
        };

        const state = liveStateService.setLiveState(req.body.deviceId, liveData);

        // Device metadata is throttled internally, so MongoDB is not written
        // on every ~3-second live packet.
        void deviceService
            .touchRealtime(liveData, state.receivedAt)
            .catch((deviceError) => {
                console.error(
                    "[DEVICE] Realtime metadata update failed:",
                    deviceError.message
                );
            });

        const io = req.app.get("io");

        if (io) {
            io.emit("hydrosense:live", liveData);
            io.emit(`hydrosense:live:${req.body.deviceId}`, liveData);

            io.emit("hydrosense:device-status", {
                deviceId: req.body.deviceId,
                status: liveStateService.getLiveState(req.body.deviceId)?.backendStatus || "UNKNOWN",
                serverTimestamp: serverTimestamp.toISOString()
            });
        }

        // Server-side ML Control observes the same genuine realtime payload.
        // It never replaces the S3 values. It can emit data-integrity warnings,
        // Isolation-Forest scores, and labelled experiment samples when the user
        // explicitly starts an experiment from the Control Room.
        void mlRuntimeService
            .observeRealtime(liveData, state.receivedAt, io)
            .catch((mlError) => {
                console.error("[ML CONTROL] Realtime observation failed:", mlError.message);
            });

        // Sensor Trust history is transition/event based. We deliberately do
        // NOT store every live trust calculation in MongoDB. The first packet
        // after backend startup seeds state; only meaningful later changes are
        // persisted and emitted.
        void sensorTrustEventService
            .processRealtimeTrust(liveData, state.receivedAt)
            .then((events) => {
                if (!Array.isArray(events) || events.length === 0) {
                    return;
                }

                console.log(
                    `[SENSOR TRUST] STORED ${events.length} event(s) | device=${req.body.deviceId}`
                );

                if (io) {
                    for (const event of events) {
                        io.emit("hydrosense:sensor-trust-event", event);
                        io.emit(
                            `hydrosense:sensor-trust-event:${req.body.deviceId}`,
                            event
                        );
                    }
                }
            })
            .catch((eventError) => {
                console.error(
                    "[SENSOR TRUST] Event persistence failed:",
                    eventError.message
                );
            });

        return res.status(200).json({
            success: true,
            message: "Real-time telemetry accepted",
            storedInTelemetryHistory: false,
            sensorTrustEventStorage: "TRANSITIONS_ONLY",
            serverTimestamp: serverTimestamp.toISOString()
        });
    } catch (error) {
        return next(error);
    }
}

module.exports = {
    receiveRealtime
};