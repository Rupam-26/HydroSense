const express =
    require(
        "express"
    );

const {
    receiveRealtime
} =
    require(
        "../controllers/realtimeController"
    );

const router =
    express.Router();

// Existing ESP32-S3 realtime endpoint.
//
// This data is sent to Socket.IO and the live state system.
// It does NOT become a historical Telemetry document.
router.post(
    "/",
    receiveRealtime
);

module.exports =
    router;