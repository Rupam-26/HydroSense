// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

const UINT8_MAX = 255;
const UINT16_MAX = 65535;
const UINT32_MAX = 4294967295;

const DEVICE_ID_PATTERN =
    /^[A-Za-z0-9][A-Za-z0-9._:-]{0,63}$/;

const STATUS_VALUES =
    new Set([
        "VALID",
        "INVALID"
    ]);


function isPlainObject(value) {
    return (
        value !== null &&
        typeof value === "object" &&
        !Array.isArray(value)
    );
}


function isFiniteNumber(value) {
    return (
        typeof value === "number" &&
        Number.isFinite(value)
    );
}


function isNullableFiniteNumber(value) {
    return (
        value === null ||
        isFiniteNumber(value)
    );
}


function requireObject(
    parent,
    key,
    errors
) {
    if (!isPlainObject(parent?.[key])) {
        errors.push(
            `${key} must be an object`
        );

        return {};
    }

    return parent[key];
}


function requireBoolean(
    object,
    key,
    errors,
    prefix = ""
) {
    if (
        typeof object?.[key] !==
        "boolean"
    ) {
        errors.push(
            `${prefix}${key} must be a boolean`
        );
    }
}


function requireFiniteRange(
    object,
    key,
    errors,
    options = {}
) {
    const value = object?.[key];
    const name =
        `${options.prefix || ""}${key}`;

    if (!isFiniteNumber(value)) {
        errors.push(
            `${name} must be a finite number`
        );

        return;
    }

    if (
        options.min !== undefined &&
        value < options.min
    ) {
        errors.push(
            `${name} must be >= ${options.min}`
        );
    }

    if (
        options.max !== undefined &&
        value > options.max
    ) {
        errors.push(
            `${name} must be <= ${options.max}`
        );
    }

    if (
        options.integer === true &&
        !Number.isSafeInteger(value)
    ) {
        errors.push(
            `${name} must be an integer`
        );
    }
}


function requireNullableRange(
    object,
    key,
    errors,
    options = {}
) {
    const value = object?.[key];
    const name =
        `${options.prefix || ""}${key}`;

    if (!isNullableFiniteNumber(value)) {
        errors.push(
            `${name} must be a finite number or null`
        );

        return;
    }

    if (value === null) {
        return;
    }

    if (
        options.min !== undefined &&
        value < options.min
    ) {
        errors.push(
            `${name} must be >= ${options.min}`
        );
    }

    if (
        options.max !== undefined &&
        value > options.max
    ) {
        errors.push(
            `${name} must be <= ${options.max}`
        );
    }
}


function requireDeviceId(
    payload,
    errors
) {
    if (
        typeof payload.deviceId !==
            "string" ||
        !DEVICE_ID_PATTERN.test(
            payload.deviceId
                .trim()
        )
    ) {
        errors.push(
            "deviceId must be 1-64 characters using letters, numbers, dot, underscore, colon or hyphen"
        );
    }
}


function requireStatus(
    object,
    key,
    errors,
    prefix = ""
) {
    if (
        typeof object?.[key] !==
            "string" ||
        !STATUS_VALUES.has(
            object[key]
        )
    ) {
        errors.push(
            `${prefix}${key} must be VALID or INVALID`
        );
    }
}


function requireModelName(
    estimated,
    errors
) {
    if (
        typeof estimated?.model !==
            "string" ||
        estimated.model.trim() === "" ||
        estimated.model.length > 128
    ) {
        errors.push(
            "estimated.model must be a non-empty string of at most 128 characters"
        );
    }
}


function requireSequence(
    payload,
    errors
) {
    requireFiniteRange(
        payload,
        "sequence",
        errors,
        {
            min: 0,
            max: UINT8_MAX,
            integer: true
        }
    );
}


function requireQuality(
    quality,
    errors
) {
    requireFiniteRange(
        quality,
        "scorePct",
        errors,
        {
            min: 0,
            max: 100,
            prefix: "quality."
        }
    );

    requireFiniteRange(
        quality,
        "flags",
        errors,
        {
            min: 0,
            max: UINT16_MAX,
            integer: true,
            prefix: "quality."
        }
    );

    [
        "crcFailures",
        "wrongLengths",
        "badHeaders",
        "badVersions",
        "queueDrops"
    ].forEach(
        (key) =>
            requireFiniteRange(
                quality,
                key,
                errors,
                {
                    min: 0,
                    max: UINT32_MAX,
                    integer: true,
                    prefix: "quality."
                }
            )
    );
}


function validateHistoricalTelemetry(
    payload
) {
    const errors = [];

    if (!isPlainObject(payload)) {
        return [
            "Request body must be a JSON object"
        ];
    }

    requireDeviceId(payload, errors);
    requireSequence(payload, errors);

    const measured =
        requireObject(payload, "measured", errors);

    const derived =
        requireObject(payload, "derived", errors);

    const estimated =
        requireObject(payload, "estimated", errors);

    const quality =
        requireObject(payload, "quality", errors);

    requireFiniteRange(
        measured,
        "airTemperatureC",
        errors,
        {
            min: -40,
            max: 80,
            prefix: "measured."
        }
    );

    requireFiniteRange(
        measured,
        "humidityPct",
        errors,
        {
            min: 0,
            max: 100,
            prefix: "measured."
        }
    );

    requireFiniteRange(
        measured,
        "waterTemperatureC",
        errors,
        {
            min: -55,
            max: 125,
            prefix: "measured."
        }
    );

    requireFiniteRange(
        measured,
        "stableDistanceCm",
        errors,
        {
            min: 0,
            prefix: "measured."
        }
    );

    requireFiniteRange(
        measured,
        "waterLevelPct",
        errors,
        {
            min: 0,
            max: 100,
            prefix: "measured."
        }
    );

    [
        "dewPointC",
        "airWaterDeltaC"
    ].forEach(
        (key) =>
            requireFiniteRange(
                derived,
                key,
                errors,
                {
                    prefix: "derived."
                }
            )
    );

    [
        "saturationVaporPressureKPa",
        "actualVaporPressureKPa",
        "vpdKPa",
        "absoluteHumidityGm3",
        "absoluteAirWaterDeltaC"
    ].forEach(
        (key) =>
            requireFiniteRange(
                derived,
                key,
                errors,
                {
                    min: 0,
                    prefix: "derived."
                }
            )
    );

    requireFiniteRange(
        estimated,
        "tdsPpm",
        errors,
        {
            min: 0,
            prefix: "estimated."
        }
    );

    requireFiniteRange(
        estimated,
        "inferenceMicros",
        errors,
        {
            min: 0,
            max: UINT32_MAX,
            integer: true,
            prefix: "estimated."
        }
    );

    requireModelName(
        estimated,
        errors
    );

    if (
        estimated?.status !==
        "VALID"
    ) {
        errors.push(
            "estimated.status must be VALID for historical telemetry"
        );
    }

    requireQuality(
        quality,
        errors
    );

    requireFiniteRange(
        payload,
        "packetAgeMs",
        errors,
        {
            min: 0,
            max: UINT32_MAX,
            integer: true
        }
    );

    requireBoolean(
        payload,
        "bleConnected",
        errors
    );

    requireBoolean(
        payload,
        "wifiConnected",
        errors
    );

    return errors;
}


function requireSensorPair(
    measured,
    valueKey,
    validKey,
    errors,
    options = {}
) {
    requireNullableRange(
        measured,
        valueKey,
        errors,
        {
            ...options,
            prefix: "measured."
        }
    );

    requireBoolean(
        measured,
        validKey,
        errors,
        "measured."
    );

    const value =
        measured?.[valueKey];

    const valid =
        measured?.[validKey];

    if (
        valid === true &&
        !isFiniteNumber(value)
    ) {
        errors.push(
            `measured.${valueKey} must be a finite number when measured.${validKey} is true`
        );
    }

    if (
        valid === false &&
        value !== null
    ) {
        errors.push(
            `measured.${valueKey} must be null when measured.${validKey} is false`
        );
    }
}


function requireDerivedGroup(
    derived,
    keys,
    validKey,
    errors,
    nonnegativeKeys = []
) {
    const nonnegative =
        new Set(
            nonnegativeKeys
        );

    keys.forEach(
        (key) =>
            requireNullableRange(
                derived,
                key,
                errors,
                {
                    min:
                        nonnegative.has(key)
                            ? 0
                            : undefined,

                    prefix: "derived."
                }
            )
    );

    requireBoolean(
        derived,
        validKey,
        errors,
        "derived."
    );

    if (
        derived?.[validKey] === true &&
        !keys.every(
            (key) =>
                isFiniteNumber(
                    derived?.[key]
                )
        )
    ) {
        errors.push(
            `all ${validKey} values must be finite when derived.${validKey} is true`
        );
    }

    if (
        derived?.[validKey] === false &&
        !keys.every(
            (key) =>
                derived?.[key] === null
        )
    ) {
        errors.push(
            `all ${validKey} values must be null when derived.${validKey} is false`
        );
    }
}


function requireMatchingBoolean(
    left,
    right,
    message,
    errors
) {
    if (
        typeof left === "boolean" &&
        typeof right === "boolean" &&
        left !== right
    ) {
        errors.push(message);
    }
}


function validateRealtimeTelemetry(
    payload
) {
    const errors = [];

    if (!isPlainObject(payload)) {
        return [
            "Request body must be a JSON object"
        ];
    }

    requireDeviceId(payload, errors);
    requireSequence(payload, errors);
    requireStatus(payload, "status", errors);

    const health =
        requireObject(payload, "health", errors);

    const measured =
        requireObject(payload, "measured", errors);

    const derived =
        requireObject(payload, "derived", errors);

    const estimated =
        requireObject(payload, "estimated", errors);

    const quality =
        requireObject(payload, "quality", errors);

    const healthKeys = [
        "packetValid",
        "dht22Valid",
        "ds18b20Valid",
        "ultrasonicValid",
        "reservoirValid",
        "calibrationValid",
        "environmentalFeaturesValid",
        "thermalFeaturesValid",
        "tdsValid",
        "bleConnected",
        "wifiConnected"
    ];

    healthKeys.forEach(
        (key) =>
            requireBoolean(
                health,
                key,
                errors,
                "health."
            )
    );

    requireSensorPair(
        measured,
        "airTemperatureC",
        "airTemperatureValid",
        errors,
        {
            min: -40,
            max: 80
        }
    );

    requireSensorPair(
        measured,
        "humidityPct",
        "humidityValid",
        errors,
        {
            min: 0,
            max: 100
        }
    );

    requireSensorPair(
        measured,
        "waterTemperatureC",
        "waterTemperatureValid",
        errors,
        {
            min: -55,
            max: 125
        }
    );

    requireSensorPair(
        measured,
        "stableDistanceCm",
        "stableDistanceValid",
        errors,
        {
            min: 0
        }
    );

    requireSensorPair(
        measured,
        "waterLevelPct",
        "waterLevelValid",
        errors,
        {
            min: 0,
            max: 100
        }
    );

    const environmentalKeys = [
        "dewPointC",
        "saturationVaporPressureKPa",
        "actualVaporPressureKPa",
        "vpdKPa",
        "absoluteHumidityGm3"
    ];

    const thermalKeys = [
        "airWaterDeltaC",
        "absoluteAirWaterDeltaC"
    ];

    requireDerivedGroup(
        derived,
        environmentalKeys,
        "environmentalValid",
        errors,
        [
            "saturationVaporPressureKPa",
            "actualVaporPressureKPa",
            "vpdKPa",
            "absoluteHumidityGm3"
        ]
    );

    requireDerivedGroup(
        derived,
        thermalKeys,
        "thermalValid",
        errors,
        [
            "absoluteAirWaterDeltaC"
        ]
    );

    requireBoolean(
        derived,
        "valid",
        errors,
        "derived."
    );

    if (
        typeof derived.environmentalValid ===
            "boolean" &&
        typeof derived.thermalValid ===
            "boolean" &&
        typeof derived.valid ===
            "boolean" &&
        derived.valid !==
            (
                derived.environmentalValid &&
                derived.thermalValid
            )
    ) {
        errors.push(
            "derived.valid must equal environmentalValid AND thermalValid"
        );
    }

    requireNullableRange(
        estimated,
        "tdsPpm",
        errors,
        {
            min: 0,
            prefix: "estimated."
        }
    );

    requireModelName(
        estimated,
        errors
    );

    requireStatus(
        estimated,
        "status",
        errors,
        "estimated."
    );

    requireBoolean(
        estimated,
        "valid",
        errors,
        "estimated."
    );

    requireFiniteRange(
        estimated,
        "inferenceMicros",
        errors,
        {
            min: 0,
            max: UINT32_MAX,
            integer: true,
            prefix: "estimated."
        }
    );

    if (
        estimated.valid === true &&
        !isFiniteNumber(
            estimated.tdsPpm
        )
    ) {
        errors.push(
            "estimated.tdsPpm must be finite when estimated.valid is true"
        );
    }

    if (
        estimated.valid === false &&
        estimated.tdsPpm !== null
    ) {
        errors.push(
            "estimated.tdsPpm must be null when estimated.valid is false"
        );
    }

    if (
        typeof estimated.valid ===
            "boolean" &&
        STATUS_VALUES.has(
            estimated.status
        ) &&
        estimated.status !==
            (
                estimated.valid
                    ? "VALID"
                    : "INVALID"
            )
    ) {
        errors.push(
            "estimated.status must match estimated.valid"
        );
    }

    requireQuality(
        quality,
        errors
    );

    requireFiniteRange(
        payload,
        "packetAgeMs",
        errors,
        {
            min: 0,
            max: UINT32_MAX,
            integer: true
        }
    );

    requireBoolean(
        payload,
        "bleConnected",
        errors
    );

    requireBoolean(
        payload,
        "wifiConnected",
        errors
    );

    requireMatchingBoolean(
        health.dht22Valid,
        measured.airTemperatureValid === true &&
            measured.humidityValid === true,
        "health.dht22Valid must match the DHT22 measured validity flags",
        errors
    );

    requireMatchingBoolean(
        health.ds18b20Valid,
        measured.waterTemperatureValid,
        "health.ds18b20Valid must match measured.waterTemperatureValid",
        errors
    );

    requireMatchingBoolean(
        health.ultrasonicValid,
        measured.stableDistanceValid,
        "health.ultrasonicValid must match measured.stableDistanceValid",
        errors
    );

    requireMatchingBoolean(
        health.reservoirValid,
        measured.waterLevelValid,
        "health.reservoirValid must match measured.waterLevelValid",
        errors
    );

    requireMatchingBoolean(
        health.environmentalFeaturesValid,
        derived.environmentalValid,
        "health.environmentalFeaturesValid must match derived.environmentalValid",
        errors
    );

    requireMatchingBoolean(
        health.thermalFeaturesValid,
        derived.thermalValid,
        "health.thermalFeaturesValid must match derived.thermalValid",
        errors
    );

    requireMatchingBoolean(
        health.tdsValid,
        estimated.valid,
        "health.tdsValid must match estimated.valid",
        errors
    );

    requireMatchingBoolean(
        health.bleConnected,
        payload.bleConnected,
        "health.bleConnected must match bleConnected",
        errors
    );

    requireMatchingBoolean(
        health.wifiConnected,
        payload.wifiConnected,
        "health.wifiConnected must match wifiConnected",
        errors
    );

    if (
        healthKeys.every(
            (key) =>
                typeof health[key] ===
                "boolean"
        ) &&
        STATUS_VALUES.has(
            payload.status
        )
    ) {
        const overallValid =
            healthKeys.every(
                (key) =>
                    health[key] === true
            );

        if (
            payload.status !==
            (
                overallValid
                    ? "VALID"
                    : "INVALID"
            )
        ) {
            errors.push(
                "status must match the combined health state"
            );
        }
    }

    return errors;
}


module.exports = {
    validateHistoricalTelemetry,
    validateRealtimeTelemetry
};