// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

const test = require("node:test");
const assert = require("node:assert/strict");

const {
    validateHistoricalTelemetry,
    validateRealtimeTelemetry
} = require("../src/utils/telemetryValidation");

function validHistoricalPayload()
{
    return {
        deviceId: "HydroSense_S3_01",
        sequence: 42,

        measured: {
            airTemperatureC: 25.2,
            humidityPct: 68.4,
            waterTemperatureC: 23.7,
            stableDistanceCm: 12.5,
            waterLevelPct: 74.0
        },

        derived: {
            dewPointC: 19.0,
            saturationVaporPressureKPa: 3.21,
            actualVaporPressureKPa: 2.20,
            vpdKPa: 1.01,
            absoluteHumidityGm3: 15.8,
            airWaterDeltaC: 1.5,
            absoluteAirWaterDeltaC: 1.5
        },

        estimated: {
            tdsPpm: 812.4,
            model: "Tiny_32_16_8 INT8",
            status: "VALID",
            inferenceMicros: 4200
        },

        quality: {
            scorePct: 98.0,
            flags: 0,
            crcFailures: 0,
            wrongLengths: 0,
            badHeaders: 0,
            badVersions: 0,
            queueDrops: 0
        },

        packetAgeMs: 150,
        bleConnected: true,
        wifiConnected: true
    };
}

function validRealtimePayload()
{
    return {
        deviceId: "HydroSense_S3_01",
        sequence: 42,
        status: "VALID",

        health: {
            packetValid: true,
            dht22Valid: true,
            ds18b20Valid: true,
            ultrasonicValid: true,
            reservoirValid: true,
            calibrationValid: true,
            environmentalFeaturesValid: true,
            thermalFeaturesValid: true,
            tdsValid: true,
            bleConnected: true,
            wifiConnected: true
        },

        measured: {
            airTemperatureC: 25.2,
            airTemperatureValid: true,
            humidityPct: 68.4,
            humidityValid: true,
            waterTemperatureC: 23.7,
            waterTemperatureValid: true,
            stableDistanceCm: 12.5,
            stableDistanceValid: true,
            waterLevelPct: 74.0,
            waterLevelValid: true
        },

        derived: {
            dewPointC: 19.0,
            saturationVaporPressureKPa: 3.21,
            actualVaporPressureKPa: 2.20,
            vpdKPa: 1.01,
            absoluteHumidityGm3: 15.8,
            environmentalValid: true,
            airWaterDeltaC: 1.5,
            absoluteAirWaterDeltaC: 1.5,
            thermalValid: true,
            valid: true
        },

        estimated: {
            tdsPpm: 812.4,
            model: "Tiny_32_16_8 INT8",
            status: "VALID",
            valid: true,
            inferenceMicros: 4200
        },

        quality: {
            scorePct: 98.0,
            flags: 0,
            crcFailures: 0,
            wrongLengths: 0,
            badHeaders: 0,
            badVersions: 0,
            queueDrops: 0
        },

        packetAgeMs: 150,
        bleConnected: true,
        wifiConnected: true
    };
}

test(
    "accepts a valid historical HydroSense payload",
    () =>
    {
        const errors =
            validateHistoricalTelemetry(
                validHistoricalPayload()
            );

        assert.deepEqual(
            errors,
            []
        );
    }
);

test(
    "accepts a valid realtime HydroSense payload",
    () =>
    {
        const errors =
            validateRealtimeTelemetry(
                validRealtimePayload()
            );

        assert.deepEqual(
            errors,
            []
        );
    }
);

test(
    "rejects an out-of-range humidity value",
    () =>
    {
        const payload =
            validRealtimePayload();

        payload.measured.humidityPct =
            101;

        const errors =
            validateRealtimeTelemetry(
                payload
            );

        assert.ok(
            errors.includes(
                "measured.humidityPct must be <= 100"
            )
        );
    }
);

test(
    "rejects an unsafe device identifier",
    () =>
    {
        const payload =
            validRealtimePayload();

        payload.deviceId =
            "../../private-file";

        const errors =
            validateRealtimeTelemetry(
                payload
            );

        assert.ok(
            errors.some(
                (error) =>
                    error.startsWith(
                        "deviceId must be"
                    )
            )
        );
    }
);

test(
    "requires a null TDS value when the estimate is invalid",
    () =>
    {
        const payload =
            validRealtimePayload();

        payload.estimated.valid =
            false;

        payload.estimated.status =
            "INVALID";

        payload.health.tdsValid =
            false;

        payload.status =
            "INVALID";

        const errors =
            validateRealtimeTelemetry(
                payload
            );

        assert.ok(
            errors.includes(
                "estimated.tdsPpm must be null when estimated.valid is false"
            )
        );
    }
);

test(
    "accepts an unavailable TDS estimate when represented consistently",
    () =>
    {
        const payload =
            validRealtimePayload();

        payload.estimated.tdsPpm =
            null;

        payload.estimated.valid =
            false;

        payload.estimated.status =
            "INVALID";

        payload.health.tdsValid =
            false;

        payload.status =
            "INVALID";

        const errors =
            validateRealtimeTelemetry(
                payload
            );

        assert.deepEqual(
            errors,
            []
        );
    }
);