const express = require("express");

const {
    getLatestSensorTrust,
    getSensorTrustEvents
} = require("../controllers/sensorTrustController");

const router = express.Router();

// ============================================================
// HYDROSENSE SENSOR TRUST ROUTES
//
// Mounted later in server.js as:
//
// /api/v1/sensor-trust
//
// Final endpoints:
//
// GET /api/v1/sensor-trust/latest
// GET /api/v1/sensor-trust/events
// ============================================================

// ------------------------------------------------------------
// CURRENT LIVE SENSOR TRUST
//
// Example:
// /api/v1/sensor-trust/latest?deviceId=HydroSense_S3_01
// ------------------------------------------------------------

router.get(
    "/latest",
    getLatestSensorTrust
);

// ------------------------------------------------------------
// HISTORICAL SENSOR TRUST EVENTS
//
// Example:
// /api/v1/sensor-trust/events?deviceId=HydroSense_S3_01&limit=100
// ------------------------------------------------------------

router.get(
    "/events",
    getSensorTrustEvents
);

module.exports = router;