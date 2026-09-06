const express =
    require(
        "express"
    );

const {
    createTelemetry,
    getLatestTelemetry,
    getTelemetryHistory,
    getTelemetrySummary
} =
    require(
        "../controllers/telemetryController"
    );

const router =
    express.Router();

// Historical MongoDB write from ESP32-S3.
router.post(
    "/",
    createTelemetry
);

// Control-room read APIs.
router.get(
    "/latest",
    getLatestTelemetry
);

router.get(
    "/history",
    getTelemetryHistory
);

router.get(
    "/summary",
    getTelemetrySummary
);

module.exports =
    router;