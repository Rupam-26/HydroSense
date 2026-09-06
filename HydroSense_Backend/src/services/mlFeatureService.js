// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

const FEATURE_DEFINITIONS = {
    airTemperatureC: {
        label: "Air temperature",
        unit: "°C",
        get: (payload) => valueIfValid(
            payload?.measured?.airTemperatureC,
            payload?.measured?.airTemperatureValid
        )
    },

    humidityPct: {
        label: "Humidity",
        unit: "%",
        get: (payload) => valueIfValid(
            payload?.measured?.humidityPct,
            payload?.measured?.humidityValid
        )
    },

    waterTemperatureC: {
        label: "Water temperature",
        unit: "°C",
        get: (payload) => valueIfValid(
            payload?.measured?.waterTemperatureC,
            payload?.measured?.waterTemperatureValid
        )
    },

    stableDistanceCm: {
        label: "Reservoir distance",
        unit: "cm",
        get: (payload) => valueIfValid(
            payload?.measured?.stableDistanceCm,
            payload?.measured?.stableDistanceValid
        )
    },

    waterLevelPct: {
        label: "Firmware water level",
        unit: "%",
        get: (payload) => valueIfValid(
            payload?.measured?.waterLevelPct,
            payload?.measured?.waterLevelValid
        )
    },

    geometryWaterLevelPct: {
        label: "Geometry-derived water level",
        unit: "%",
        get: (payload, config) =>
            geometryLevel(
                payload?.measured?.stableDistanceCm,
                payload?.measured?.stableDistanceValid,
                config?.reservoir
            )
    },

    geometryWaterVolumeL: {
        label: "Geometry-derived water volume",
        unit: "L",
        get: (payload, config) =>
            geometryVolume(
                payload?.measured?.stableDistanceCm,
                payload?.measured?.stableDistanceValid,
                config?.reservoir
            )
    },

    dewPointC: {
        label: "Dew point",
        unit: "°C",
        get: (payload) => valueIfValid(
            payload?.derived?.dewPointC,
            payload?.derived?.environmentalValid
        )
    },

    saturationVaporPressureKPa: {
        label: "Saturation vapour pressure",
        unit: "kPa",
        get: (payload) => valueIfValid(
            payload?.derived?.saturationVaporPressureKPa,
            payload?.derived?.environmentalValid
        )
    },

    actualVaporPressureKPa: {
        label: "Actual vapour pressure",
        unit: "kPa",
        get: (payload) => valueIfValid(
            payload?.derived?.actualVaporPressureKPa,
            payload?.derived?.environmentalValid
        )
    },

    vpdKPa: {
        label: "VPD",
        unit: "kPa",
        get: (payload) => valueIfValid(
            payload?.derived?.vpdKPa,
            payload?.derived?.environmentalValid
        )
    },

    absoluteHumidityGm3: {
        label: "Absolute humidity",
        unit: "g/m³",
        get: (payload) => valueIfValid(
            payload?.derived?.absoluteHumidityGm3,
            payload?.derived?.environmentalValid
        )
    },

    airWaterDeltaC: {
        label: "Air-water ΔT",
        unit: "°C",
        get: (payload) => valueIfValid(
            payload?.derived?.airWaterDeltaC,
            payload?.derived?.thermalValid
        )
    },

    absoluteAirWaterDeltaC: {
        label: "Absolute air-water ΔT",
        unit: "°C",
        get: (payload) => valueIfValid(
            payload?.derived?.absoluteAirWaterDeltaC,
            payload?.derived?.thermalValid
        )
    },

    tdsPpm: {
        label: "TinyML TDS estimate",
        unit: "ppm",
        get: (payload) => valueIfValid(
            payload?.estimated?.tdsPpm,
            payload?.estimated?.valid
        )
    }
};


function finite(value) {
    return (
        typeof value === "number" &&
        Number.isFinite(value)
    );
}


function valueIfValid(value, valid) {
    return (
        valid === true &&
        finite(value)
    )
        ? value
        : null;
}


function clamp(value, minimum, maximum) {
    return Math.min(
        maximum,
        Math.max(
            minimum,
            value
        )
    );
}


function validCalibration(reservoir) {
    return (
        reservoir &&
        finite(reservoir.fullDistanceCm) &&
        reservoir.fullDistanceCm >= 0 &&
        finite(reservoir.emptyDistanceCm) &&
        reservoir.emptyDistanceCm >
            reservoir.fullDistanceCm
    );
}


function geometryFraction(
    distanceCm,
    valid,
    reservoir
) {
    if (
        valid !== true ||
        !finite(distanceCm) ||
        !validCalibration(reservoir)
    ) {
        return null;
    }

    const fraction = (
        reservoir.emptyDistanceCm -
        distanceCm
    ) / (
        reservoir.emptyDistanceCm -
        reservoir.fullDistanceCm
    );

    return clamp(
        fraction,
        0,
        1
    );
}


function geometryLevel(
    distanceCm,
    valid,
    reservoir
) {
    const fraction =
        geometryFraction(
            distanceCm,
            valid,
            reservoir
        );

    return fraction === null
        ? null
        : fraction * 100;
}


function geometryVolume(
    distanceCm,
    valid,
    reservoir
) {
    const fraction =
        geometryFraction(
            distanceCm,
            valid,
            reservoir
        );

    if (
        fraction === null ||
        !finite(reservoir?.heightCm) ||
        reservoir.heightCm <= 0
    ) {
        return null;
    }

    const usableDepthCm =
        reservoir.emptyDistanceCm -
        reservoir.fullDistanceCm;

    if (
        !finite(usableDepthCm) ||
        usableDepthCm <= 0 ||
        usableDepthCm > reservoir.heightCm
    ) {
        return null;
    }

    const waterDepthCm =
        fraction * usableDepthCm;


    // ========================================================
    // RECTANGULAR RESERVOIR
    // ========================================================

    if (
        reservoir.shape === "RECTANGULAR" &&
        finite(reservoir.lengthCm) &&
        reservoir.lengthCm > 0 &&
        finite(reservoir.widthCm) &&
        reservoir.widthCm > 0
    ) {
        return (
            reservoir.lengthCm *
            reservoir.widthCm *
            waterDepthCm
        ) / 1000;
    }


    // ========================================================
    // VERTICAL CYLINDRICAL / DRUM RESERVOIR
    // ========================================================

    if (
        reservoir.shape ===
            "VERTICAL_CYLINDER" &&
        finite(reservoir.diameterCm) &&
        reservoir.diameterCm > 0
    ) {
        const radius =
            reservoir.diameterCm / 2;

        return (
            Math.PI *
            radius *
            radius *
            waterDepthCm
        ) / 1000;
    }


    return null;
}


function extractFeatures(
    payload,
    featureNames,
    config
) {
    const values = {};
    const missing = [];

    for (
        const name of
        featureNames || []
    ) {
        const definition =
            FEATURE_DEFINITIONS[name];

        if (!definition) {
            missing.push(name);
            continue;
        }

        const value =
            definition.get(
                payload,
                config
            );

        if (!finite(value)) {
            values[name] = null;
            missing.push(name);
        } else {
            values[name] = value;
        }
    }

    return {
        values,
        missing,

        complete:
            missing.length === 0
    };
}


function featureCatalog() {
    return Object.entries(
        FEATURE_DEFINITIONS
    ).map(
        ([key, value]) => ({
            key,

            label:
                value.label,

            unit:
                value.unit,

            geometryRequired:
                key.startsWith(
                    "geometry"
                )
        })
    );
}


module.exports = {
    FEATURE_DEFINITIONS,

    finite,

    validCalibration,

    geometryFraction,

    geometryLevel,

    geometryVolume,

    extractFeatures,

    featureCatalog
};