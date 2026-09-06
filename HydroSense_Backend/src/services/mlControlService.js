// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

const fs = require("node:fs");
const mongoose = require("mongoose");

const MLControlConfig = require("../models/MLControlConfig");
const MLBaselineSession = require("../models/MLBaselineSession");
const MLModelArtifact = require("../models/MLModelArtifact");
const MLExperiment = require("../models/MLExperiment");
const MLExperimentSample = require("../models/MLExperimentSample");

const {
    FEATURE_DEFINITIONS,
    finite,
    validCalibration,
    extractFeatures,
    featureCatalog
} = require("./mlFeatureService");

const collectionCache = new Map();

function error(message, statusCode = 400) {
    const e = new Error(message);
    e.statusCode = statusCode;
    return e;
}

function requiredNumber(value, name, options = {}) {
    const n = Number(value);

    if (!finite(n)) {
        throw error(`${name} must be a finite number`);
    }

    if (
        options.integer &&
        !Number.isInteger(n)
    ) {
        throw error(
            `${name} must be an integer`
        );
    }

    if (
        options.min !== undefined &&
        n < options.min
    ) {
        throw error(
            `${name} must be >= ${options.min}`
        );
    }

    if (
        options.max !== undefined &&
        n > options.max
    ) {
        throw error(
            `${name} must be <= ${options.max}`
        );
    }

    if (
        options.gt !== undefined &&
        n <= options.gt
    ) {
        throw error(
            `${name} must be > ${options.gt}`
        );
    }

    return n;
}

function median(values) {
    if (!values.length) {
        return null;
    }

    const sorted =
        [...values].sort(
            (a, b) => a - b
        );

    const middle =
        Math.floor(
            sorted.length / 2
        );

    return sorted.length % 2
        ? sorted[middle]
        : (
            sorted[middle - 1] +
            sorted[middle]
        ) / 2;
}

function quantile(values, q) {
    if (!values.length) {
        return null;
    }

    const sorted =
        [...values].sort(
            (a, b) => a - b
        );

    const position =
        (sorted.length - 1) * q;

    const lower =
        Math.floor(position);

    const upper =
        Math.ceil(position);

    if (lower === upper) {
        return sorted[lower];
    }

    return (
        sorted[lower] +
        (
            sorted[upper] -
            sorted[lower]
        ) *
        (
            position -
            lower
        )
    );
}

function longestRun(flags) {
    let current = 0;
    let longest = 0;

    for (const flag of flags) {
        current =
            flag
                ? current + 1
                : 0;

        longest =
            Math.max(
                longest,
                current
            );
    }

    return longest;
}

function validateReservoir(
    input = {}
) {
    const shape =
        String(
            input.shape ||
            "UNCONFIGURED"
        )
            .trim()
            .toUpperCase();

    const allowed =
        new Set([
            "UNCONFIGURED",
            "RECTANGULAR",
            "VERTICAL_CYLINDER",
            "DEPTH_ONLY"
        ]);

    if (!allowed.has(shape)) {
        throw error(
            "Unsupported reservoir shape"
        );
    }

    const output = {
        shape,

        heightCm: null,

        lengthCm: null,

        widthCm: null,

        diameterCm: null,

        fullDistanceCm: null,

        emptyDistanceCm: null
    };

    if (
        shape ===
        "UNCONFIGURED"
    ) {
        return output;
    }

    output.fullDistanceCm =
        requiredNumber(
            input.fullDistanceCm,
            "reservoir.fullDistanceCm",
            {
                min: 0
            }
        );

    output.emptyDistanceCm =
        requiredNumber(
            input.emptyDistanceCm,
            "reservoir.emptyDistanceCm",
            {
                min: 0
            }
        );

    if (
        output.emptyDistanceCm <=
        output.fullDistanceCm
    ) {
        throw error(
            "reservoir.emptyDistanceCm must be greater than reservoir.fullDistanceCm"
        );
    }

    if (
        shape ===
        "DEPTH_ONLY"
    ) {
        return output;
    }

    output.heightCm =
        requiredNumber(
            input.heightCm,
            "reservoir.heightCm",
            {
                gt: 0
            }
        );

    const usableDepthCm =
        output.emptyDistanceCm -
        output.fullDistanceCm;

    if (
        usableDepthCm >
        output.heightCm
    ) {
        throw error(
            "Reservoir FULL-to-EMPTY calibration depth must not exceed reservoir.heightCm"
        );
    }

    if (
        shape ===
        "RECTANGULAR"
    ) {
        output.lengthCm =
            requiredNumber(
                input.lengthCm,
                "reservoir.lengthCm",
                {
                    gt: 0
                }
            );

        output.widthCm =
            requiredNumber(
                input.widthCm,
                "reservoir.widthCm",
                {
                    gt: 0
                }
            );
    }

    if (
        shape ===
        "VERTICAL_CYLINDER"
    ) {
        output.diameterCm =
            requiredNumber(
                input.diameterCm,
                "reservoir.diameterCm",
                {
                    gt: 0
                }
            );
    }

    return output;
}

function validateBaseline(
    input = {}
) {
    const features =
        [
            ...new Set(
                Array.isArray(
                    input.features
                )
                    ? input.features.map(
                        String
                    )
                    : []
            )
        ];

    if (!features.length) {
        throw error(
            "Select at least one ML feature"
        );
    }

    for (
        const feature
        of features
    ) {
        if (
            !FEATURE_DEFINITIONS[
                feature
            ]
        ) {
            throw error(
                `Unknown ML feature: ${feature}`
            );
        }
    }

    return {
        targetDurationHours:
            requiredNumber(
                input.targetDurationHours,
                "baseline.targetDurationHours",
                {
                    gt: 0
                }
            ),

        minimumCoveragePct:
            requiredNumber(
                input.minimumCoveragePct,
                "baseline.minimumCoveragePct",
                {
                    min: 0,
                    max: 100
                }
            ),

        maximumGapMultiplier:
            requiredNumber(
                input.maximumGapMultiplier,
                "baseline.maximumGapMultiplier",
                {
                    gt: 1
                }
            ),

        robustZThreshold:
            requiredNumber(
                input.robustZThreshold,
                "baseline.robustZThreshold",
                {
                    gt: 0
                }
            ),

        sustainedStrangeSamples:
            requiredNumber(
                input.sustainedStrangeSamples,
                "baseline.sustainedStrangeSamples",
                {
                    min: 2,
                    integer: true
                }
            ),

        missingWarningSeconds:
            requiredNumber(
                input.missingWarningSeconds,
                "baseline.missingWarningSeconds",
                {
                    gt: 0
                }
            ),

        unchangedWarningSeconds:
            requiredNumber(
                input.unchangedWarningSeconds,
                "baseline.unchangedWarningSeconds",
                {
                    gt: 0
                }
            ),

        minimumCalibrationSamplesPerClass:
            requiredNumber(
                input.minimumCalibrationSamplesPerClass,
                "baseline.minimumCalibrationSamplesPerClass",
                {
                    min: 1,
                    integer: true
                }
            ),

        features
    };
}

function validateIsolationForest(
    input = {}
) {
    return {
        nEstimators:
            requiredNumber(
                input.nEstimators,
                "isolationForest.nEstimators",
                {
                    min: 1,
                    integer: true
                }
            ),

        trainFraction:
            requiredNumber(
                input.trainFraction,
                "isolationForest.trainFraction",
                {
                    gt: 0,
                    max: 0.99
                }
            ),

        randomState:
            requiredNumber(
                input.randomState,
                "isolationForest.randomState",
                {
                    integer: true
                }
            )
    };
}

async function getConfig(
    deviceId
) {
    let config =
        await MLControlConfig
            .findOne({
                deviceId
            })
            .lean();

    if (!config) {
        config =
            (
                await MLControlConfig
                    .create({
                        deviceId
                    })
            )
                .toObject();
    }

    return config;
}

async function updateConfig(
    deviceId,
    body
) {
    const reservoir =
        validateReservoir(
            body?.reservoir
        );

    const baseline =
        validateBaseline(
            body?.baseline
        );

    const isolationForest =
        validateIsolationForest(
            body?.isolationForest
        );

    const geometryUsed =
        baseline.features.some(
            (feature) =>
                feature.startsWith(
                    "geometry"
                )
        );

    if (
        geometryUsed &&
        !validCalibration(
            reservoir
        )
    ) {
        throw error(
            "Geometry-derived features need valid FULL and EMPTY ultrasonic calibration"
        );
    }

    if (
        baseline.features.includes(
            "geometryWaterVolumeL"
        ) &&
        ![
            "RECTANGULAR",
            "VERTICAL_CYLINDER"
        ].includes(
            reservoir.shape
        )
    ) {
        throw error(
            "geometryWaterVolumeL requires a RECTANGULAR or VERTICAL_CYLINDER reservoir"
        );
    }

    return MLControlConfig
        .findOneAndUpdate(
            {
                deviceId
            },
            {
                $set: {
                    reservoir,
                    baseline,
                    isolationForest
                }
            },
            {
                new: true,
                upsert: true,
                setDefaultsOnInsert: true
            }
        )
        .lean();
}

function isTelemetry(
    document
) {
    return Boolean(
        document &&
        typeof document.deviceId ===
            "string" &&
        document.measured &&
        document.derived &&
        document.estimated &&
        document.quality
    );
}

function timestampInfo(
    document
) {
    const candidates = [
        "serverTimestamp",
        "timestamp",
        "sourceTimestamp",
        "createdAt",
        "receivedAt",
        "updatedAt"
    ];

    for (
        const field
        of candidates
    ) {
        const value =
            document?.[field];

        if (
            value instanceof Date ||
            (
                typeof value ===
                    "string" &&
                !Number.isNaN(
                    new Date(
                        value
                    ).getTime()
                )
            )
        ) {
            return {
                field,
                objectId: false
            };
        }
    }

    if (
        document?._id &&
        typeof document._id
            .getTimestamp ===
            "function"
    ) {
        return {
            field: "_id",
            objectId: true
        };
    }

    throw error(
        "Historical telemetry has no usable timestamp",
        500
    );
}

function documentTime(
    document,
    info
) {
    if (info.objectId) {
        return document._id
            .getTimestamp();
    }

    const date =
        document[info.field]
            instanceof Date
            ? document[
                info.field
            ]
            : new Date(
                document[
                    info.field
                ]
            );

    return Number.isNaN(
        date.getTime()
    )
        ? null
        : date;
}

async function telemetryCollection(
    deviceId
) {
    if (
        mongoose.connection
            .readyState !== 1 ||
        !mongoose.connection.db
    ) {
        throw error(
            "MongoDB is not connected",
            503
        );
    }

    if (
        collectionCache.has(
            deviceId
        )
    ) {
        return collectionCache.get(
            deviceId
        );
    }

    const names =
        await mongoose
            .connection
            .db
            .listCollections(
                {},
                {
                    nameOnly: true
                }
            )
            .toArray();

    const matches = [];

    for (
        const item
        of names
    ) {
        const collection =
            mongoose
                .connection
                .db
                .collection(
                    item.name
                );

        const sample =
            await collection
                .findOne({
                    deviceId
                });

        if (
            sample &&
            isTelemetry(
                sample
            )
        ) {
            matches.push({
                collection,

                name:
                    item.name,

                time:
                    timestampInfo(
                        sample
                    )
            });
        }
    }

    if (!matches.length) {
        throw error(
            `No historical telemetry collection found for ${deviceId}`,
            404
        );
    }

    if (
        matches.length > 1
    ) {
        throw error(
            `More than one telemetry collection matches ${deviceId}`,
            409
        );
    }

    collectionCache.set(
        deviceId,
        matches[0]
    );

    return matches[0];
}

function historicalPayload(
    document
) {
    const measured =
        document.measured || {};

    const derived =
        document.derived || {};

    const estimated =
        document.estimated || {};

    return {
        measured: {
            ...measured,

            airTemperatureValid:
                typeof measured
                    .airTemperatureValid ===
                    "boolean"
                    ? measured
                        .airTemperatureValid
                    : finite(
                        measured
                            .airTemperatureC
                    ),

            humidityValid:
                typeof measured
                    .humidityValid ===
                    "boolean"
                    ? measured
                        .humidityValid
                    : finite(
                        measured
                            .humidityPct
                    ),

            waterTemperatureValid:
                typeof measured
                    .waterTemperatureValid ===
                    "boolean"
                    ? measured
                        .waterTemperatureValid
                    : finite(
                        measured
                            .waterTemperatureC
                    ),

            stableDistanceValid:
                typeof measured
                    .stableDistanceValid ===
                    "boolean"
                    ? measured
                        .stableDistanceValid
                    : finite(
                        measured
                            .stableDistanceCm
                    ),

            waterLevelValid:
                typeof measured
                    .waterLevelValid ===
                    "boolean"
                    ? measured
                        .waterLevelValid
                    : finite(
                        measured
                            .waterLevelPct
                    )
        },

        derived: {
            ...derived,

            environmentalValid:
                typeof derived
                    .environmentalValid ===
                    "boolean"
                    ? derived
                        .environmentalValid
                    : [
                        derived
                            .dewPointC,

                        derived
                            .saturationVaporPressureKPa,

                        derived
                            .actualVaporPressureKPa,

                        derived
                            .vpdKPa,

                        derived
                            .absoluteHumidityGm3
                    ].every(
                        finite
                    ),

            thermalValid:
                typeof derived
                    .thermalValid ===
                    "boolean"
                    ? derived
                        .thermalValid
                    : [
                        derived
                            .airWaterDeltaC,

                        derived
                            .absoluteAirWaterDeltaC
                    ].every(
                        finite
                    )
        },

        estimated: {
            ...estimated,

            valid:
                typeof estimated
                    .valid ===
                    "boolean"
                    ? estimated.valid
                    : finite(
                        estimated
                            .tdsPpm
                    )
        },

        quality:
            document.quality ||
            {}
    };
}

async function readTelemetry(
    deviceId,
    from,
    to
) {
    const source =
        await telemetryCollection(
            deviceId
        );

    const filter = {
        deviceId
    };

    if (
        !source.time
            .objectId
    ) {
        filter[
            source.time.field
        ] = {
            $gte: from,
            $lte: to
        };
    }

    const documents =
        await source
            .collection
            .find(
                filter
            )
            .sort({
                [
                    source
                        .time
                        .field
                ]: 1
            })
            .toArray();

    const rows = [];

    for (
        const document
        of documents
    ) {
        const timestamp =
            documentTime(
                document,
                source.time
            );

        if (
            timestamp &&
            timestamp >= from &&
            timestamp <= to
        ) {
            rows.push({
                timestamp,
                document
            });
        }
    }

    return {
        source,
        rows
    };
}

async function latestStoredTelemetry(
    deviceId
) {
    const source =
        await telemetryCollection(
            deviceId
        );

    const document =
        await source
            .collection
            .find({
                deviceId
            })
            .sort({
                [
                    source.time.field
                ]: -1
            })
            .limit(1)
            .next();

    if (!document) {
        return null;
    }

    return {
        collection:
            source.name,

        timestamp:
            documentTime(
                document,
                source.time
            ),

        document
    };
}

function featureAudit(
    values,
    settings
) {
    const finiteValues =
        values.filter(
            finite
        );

    const center =
        median(
            finiteValues
        );

    const mad =
        center === null
            ? null
            : median(
                finiteValues.map(
                    (value) =>
                        Math.abs(
                            value -
                            center
                        )
                )
            );

    const outlierFlags =
        values.map(
            (value) =>
                finite(value) &&
                finite(mad) &&
                mad > 0
                    ? Math.abs(
                        0.6744897501960817 *
                        (
                            value -
                            center
                        ) /
                        mad
                    ) >
                    settings
                        .robustZThreshold
                    : false
        );

    let repeat = 0;
    let longestRepeat = 0;
    let previous =
        Symbol("start");

    for (
        const value
        of values
    ) {
        if (
            finite(value) &&
            value === previous
        ) {
            repeat += 1;
        } else {
            repeat =
                finite(value)
                    ? 1
                    : 0;
        }

        previous = value;

        longestRepeat =
            Math.max(
                longestRepeat,
                repeat
            );
    }

    return {
        total:
            values.length,

        present:
            finiteValues.length,

        missing:
            values.length -
            finiteValues.length,

        minimum:
            finiteValues.length
                ? Math.min(
                    ...finiteValues
                )
                : null,

        maximum:
            finiteValues.length
                ? Math.max(
                    ...finiteValues
                )
                : null,

        median:
            center,

        mad,

        q05:
            quantile(
                finiteValues,
                0.05
            ),

        q95:
            quantile(
                finiteValues,
                0.95
            ),

        robustOutlierCount:
            outlierFlags.filter(
                Boolean
            ).length,

        longestRobustOutlierRun:
            longestRun(
                outlierFlags
            ),

        longestExactRepeatRun:
            longestRepeat,

        zeroVariance:
            finiteValues.length > 1 &&
            Math.min(
                ...finiteValues
            ) ===
            Math.max(
                ...finiteValues
            )
    };
}

function issue(
    severity,
    code,
    message,
    feature = null
) {
    return {
        severity,
        code,
        message,
        feature
    };
}

async function analyzeBaseline(
    deviceId,
    suppliedSession = null
) {
    const config =
        await getConfig(
            deviceId
        );

    const session =
        suppliedSession ||
        await MLBaselineSession
            .findOne({
                deviceId
            })
            .sort({
                startedAt: -1
            })
            .lean();

    if (!session) {
        return {
            session: null,

            trainingEligible:
                false,

            blockers: [
                issue(
                    "BLOCKER",
                    "NO_SESSION",
                    "Start a healthy baseline first"
                )
            ],

            warnings: []
        };
    }

    const snapshot =
        session
            .configurationSnapshot ||
        config;

    const settings =
        snapshot.baseline;

    const features =
        settings.features;

    const from =
        new Date(
            session.startedAt
        );

    const to =
        session.status ===
            "ACTIVE"
            ? new Date()
            : new Date(
                session.stoppedAt ||
                new Date()
            );

    const {
        source,
        rows: stored
    } =
        await readTelemetry(
            deviceId,
            from,
            to
        );

    const rows =
        stored.map(
            ({
                timestamp,
                document
            }) => {
                const result =
                    extractFeatures(
                        historicalPayload(
                            document
                        ),

                        features,

                        snapshot
                    );

                return {
                    timestamp,

                    sequence:
                        document
                            .sequence ??
                        null,

                    values:
                        result.values,

                    complete:
                        result.complete
                };
            }
        );

    const completeRows =
        rows.filter(
            (row) =>
                row.complete
        );

    const times =
        rows.map(
            (row) =>
                row.timestamp
                    .getTime()
        );

    const gaps = [];

    for (
        let index = 1;
        index < times.length;
        index += 1
    ) {
        if (
            times[index] >
            times[index - 1]
        ) {
            gaps.push(
                (
                    times[index] -
                    times[index - 1]
                ) /
                1000
            );
        }
    }

    const cadence =
        median(
            gaps
        );

    const elapsedSeconds =
        Math.max(
            0,
            (
                to -
                from
            ) /
            1000
        );

    const targetSeconds =
        settings
            .targetDurationHours *
        3600;

    const requiredSamples =
        finite(cadence) &&
        cadence > 0
            ? Math.ceil(
                targetSeconds /
                cadence
            ) + 1
            : null;

    const coveragePct =
        requiredSamples
            ? Math.min(
                100,

                completeRows.length /
                requiredSamples *
                100
            )
            : null;

    const longGapLimit =
        finite(cadence)
            ? cadence *
                settings
                    .maximumGapMultiplier
            : null;

    const longGaps =
        finite(
            longGapLimit
        )
            ? gaps.filter(
                (gap) =>
                    gap >
                    longGapLimit
            )
            : [];

    const audits = {};
    const blockers = [];
    const warnings = [];

    for (
        const feature
        of features
    ) {
        const audit =
            featureAudit(
                rows.map(
                    (row) =>
                        row.values[
                            feature
                        ]
                ),

                settings
            );

        audits[
            feature
        ] = audit;

        if (
            audit.zeroVariance
        ) {
            blockers.push(
                issue(
                    "BLOCKER",

                    "ZERO_VARIANCE",

                    `${feature} has zero variance`,

                    feature
                )
            );
        }

        if (
            audit.missing > 0
        ) {
            warnings.push(
                issue(
                    "WARNING",

                    "MISSING_VALUES",

                    `${feature} is missing in ${audit.missing} stored record(s); those rows are excluded, never filled`,

                    feature
                )
            );
        }

        if (
            audit
                .longestRobustOutlierRun >=
            settings
                .sustainedStrangeSamples
        ) {
            blockers.push(
                issue(
                    "BLOCKER",

                    "SUSTAINED_STRANGE_DATA",

                    `${feature} has ${audit.longestRobustOutlierRun} consecutive robust outlier candidates`,

                    feature
                )
            );
        } else if (
            audit
                .robustOutlierCount >
            0
        ) {
            warnings.push(
                issue(
                    "WARNING",

                    "ROBUST_OUTLIERS",

                    `${feature} has ${audit.robustOutlierCount} robust outlier candidate(s); none are automatically deleted`,

                    feature
                )
            );
        }

        if (
            audit
                .longestExactRepeatRun >=
            settings
                .sustainedStrangeSamples
        ) {
            warnings.push(
                issue(
                    "WARNING",

                    "REPEATED_VALUE",

                    `${feature} repeated exactly for ${audit.longestExactRepeatRun} stored samples; review evidence only`,

                    feature
                )
            );
        }
    }

    if (
        elapsedSeconds <
        targetSeconds
    ) {
        blockers.push(
            issue(
                "BLOCKER",

                "DURATION",

                "Configured healthy-baseline duration has not elapsed"
            )
        );
    }

    if (
        requiredSamples ===
        null
    ) {
        blockers.push(
            issue(
                "BLOCKER",

                "NO_CADENCE",

                "At least two stored records are required to establish real MongoDB cadence"
            )
        );
    }

    if (
        coveragePct !==
            null &&
        coveragePct <
            settings
                .minimumCoveragePct
    ) {
        blockers.push(
            issue(
                "BLOCKER",

                "COVERAGE",

                `Complete-data coverage ${coveragePct.toFixed(1)}% is below configured ${settings.minimumCoveragePct}%`
            )
        );
    }

    if (
        completeRows.length <
        2
    ) {
        blockers.push(
            issue(
                "BLOCKER",

                "TOO_FEW_COMPLETE",

                "At least two complete records are required for train/validation splitting"
            )
        );
    }

    if (
        longGaps.length
    ) {
        warnings.push(
            issue(
                "WARNING",

                "LONG_GAPS",

                `${longGaps.length} storage gap(s) exceed ${settings.maximumGapMultiplier}× the observed median cadence`
            )
        );
    }

    const geometry =
        snapshot.reservoir;

    if (
        features.some(
            (feature) =>
                feature.startsWith(
                    "geometry"
                )
        ) &&
        !validCalibration(
            geometry
        )
    ) {
        blockers.push(
            issue(
                "BLOCKER",

                "GEOMETRY",

                "Geometry-derived features need valid reservoir calibration"
            )
        );
    }

    if (
        validCalibration(
            geometry
        ) &&
        features.includes(
            "stableDistanceCm"
        )
    ) {
        const outside =
            rows.filter(
                (row) =>
                    finite(
                        row.values
                            .stableDistanceCm
                    ) &&
                    (
                        row.values
                            .stableDistanceCm <
                            geometry
                                .fullDistanceCm ||

                        row.values
                            .stableDistanceCm >
                            geometry
                                .emptyDistanceCm
                    )
            ).length;

        if (outside) {
            warnings.push(
                issue(
                    "WARNING",

                    "DISTANCE_CALIBRATION",

                    `${outside} reservoir-distance sample(s) are outside the configured FULL-to-EMPTY interval`,

                    "stableDistanceCm"
                )
            );
        }
    }

    return {
        session,

        collection:
            source.name,

        storedSamples:
            rows.length,

        completeSamples:
            completeRows.length,

        incompleteSamples:
            rows.length -
            completeRows.length,

        elapsedHours:
            elapsedSeconds /
            3600,

        targetHours:
            settings
                .targetDurationHours,

        observedMedianCadenceSeconds:
            cadence,

        requiredSamples,

        coveragePct,

        minimumCoveragePct:
            settings
                .minimumCoveragePct,

        longGapCount:
            longGaps.length,

        largestGapSeconds:
            gaps.length
                ? Math.max(
                    ...gaps
                )
                : null,

        featureAudit:
            audits,

        warnings,

        blockers,

        trainingEligible:
            session.status !==
                "ABORTED" &&
            blockers.length === 0,

        _rows:
            completeRows
    };
}

async function startBaseline(
    deviceId,
    operatorNote = ""
) {
    const existing =
        await MLBaselineSession
            .exists({
                deviceId,
                status:
                    "ACTIVE"
            });

    if (existing) {
        throw error(
            "A baseline session is already active",
            409
        );
    }

    const config =
        await getConfig(
            deviceId
        );

    validateBaseline(
        config.baseline
    );

    validateIsolationForest(
        config.isolationForest
    );

    const session =
        await MLBaselineSession
            .create({
                deviceId,

                status:
                    "ACTIVE",

                startedAt:
                    new Date(),

                configurationSnapshot: {
                    reservoir:
                        config.reservoir,

                    baseline:
                        config.baseline,

                    isolationForest:
                        config
                            .isolationForest
                },

                operatorNote:
                    String(
                        operatorNote ||
                        ""
                    ).trim()
            });

    return session
        .toObject();
}

async function finishBaseline(
    deviceId,
    status
) {
    const session =
        await MLBaselineSession
            .findOne({
                deviceId,

                status:
                    "ACTIVE"
            });

    if (!session) {
        throw error(
            "No active baseline session",
            404
        );
    }

    session.status =
        status;

    session.stoppedAt =
        new Date();

    await session.save();

    return session
        .toObject();
}

const stopBaseline =
    (deviceId) =>
        finishBaseline(
            deviceId,
            "STOPPED"
        );

const abortBaseline =
    (deviceId) =>
        finishBaseline(
            deviceId,
            "ABORTED"
        );

async function activeModel(
    deviceId
) {
    return MLModelArtifact
        .findOne({
            deviceId,

            status:
                "READY",

            active:
                true
        })
        .sort({
            activatedAt:
                -1
        })
        .lean();
}

async function listModels(
    deviceId
) {
    return MLModelArtifact
        .find({
            deviceId
        })
        .sort({
            createdAt:
                -1
        })
        .limit(50)
        .lean();
}

async function activateModel(
    deviceId,
    modelId
) {
    const model =
        await MLModelArtifact
            .findOne({
                _id:
                    modelId,

                deviceId
            });

    if (!model) {
        throw error(
            "Model not found",
            404
        );
    }

    if (
        model.status !==
        "READY"
    ) {
        throw error(
            "Only a READY model can be activated",
            409
        );
    }

    if (
        !model.artifactPath ||
        !fs.existsSync(
            model.artifactPath
        )
    ) {
        throw error(
            "Model artifact file is missing",
            409
        );
    }

    await MLModelArtifact
        .updateMany(
            {
                deviceId,

                _id: {
                    $ne:
                        model._id
                }
            },
            {
                $set: {
                    active:
                        false
                }
            }
        );

    model.active =
        true;

    model.activatedAt =
        new Date();

    await model.save();

    return model
        .toObject();
}

async function startExperiment(
    deviceId,
    label,
    operatorNote = ""
) {
    const existing =
        await MLExperiment
            .exists({
                deviceId,

                status:
                    "ACTIVE"
            });

    if (existing) {
        throw error(
            "An experiment is already active",
            409
        );
    }

    const model =
        await activeModel(
            deviceId
        );

    if (!model) {
        throw error(
            "Activate a trained Isolation Forest first",
            409
        );
    }

    const labels =
        new Set([
            "HEALTHY_CONTROL",

            "SENSOR_DISCONNECT",

            "STUCK_OUTPUT",

            "EXCESSIVE_NOISE",

            "SUDDEN_PERTURBATION",

            "PROBE_DEGRADATION",

            "OTHER_FAULT"
        ]);

    if (
        !labels.has(
            label
        )
    ) {
        throw error(
            "Unsupported experiment label"
        );
    }

    const experiment =
        await MLExperiment
            .create({
                deviceId,

                modelArtifactId:
                    model._id,

                label,

                isFault:
                    label !==
                    "HEALTHY_CONTROL",

                status:
                    "ACTIVE",

                startedAt:
                    new Date(),

                operatorNote:
                    String(
                        operatorNote ||
                        ""
                    ).trim()
            });

    return experiment
        .toObject();
}

async function stopExperiment(
    deviceId
) {
    const experiment =
        await MLExperiment
            .findOne({
                deviceId,

                status:
                    "ACTIVE"
            });

    if (!experiment) {
        throw error(
            "No active experiment",
            404
        );
    }

    experiment.status =
        "STOPPED";

    experiment.stoppedAt =
        new Date();

    await experiment.save();

    return experiment
        .toObject();
}

async function listExperiments(
    deviceId
) {
    return MLExperiment
        .find({
            deviceId
        })
        .sort({
            startedAt:
                -1
        })
        .limit(50)
        .lean();
}

function metrics(
    samples,
    threshold
) {
    let tp = 0;
    let fp = 0;
    let tn = 0;
    let fn = 0;

    for (
        const sample
        of samples
    ) {
        const predictedFault =
            sample
                .decisionFunction <
            threshold;

        if (
            predictedFault &&
            sample.isFault
        ) {
            tp += 1;
        } else if (
            predictedFault
        ) {
            fp += 1;
        } else if (
            sample.isFault
        ) {
            fn += 1;
        } else {
            tn += 1;
        }
    }

    const precision =
        tp + fp
            ? tp /
                (
                    tp +
                    fp
                )
            : 0;

    const recall =
        tp + fn
            ? tp /
                (
                    tp +
                    fn
                )
            : 0;

    const f1 =
        precision +
            recall
            ? 2 *
                precision *
                recall /
                (
                    precision +
                    recall
                )
            : 0;

    const specificity =
        tn + fp
            ? tn /
                (
                    tn +
                    fp
                )
            : 0;

    return {
        tp,
        fp,
        tn,
        fn,

        precision,
        recall,
        f1,

        balancedAccuracy:
            (
                recall +
                specificity
            ) / 2
    };
}

async function calibrateThreshold(
    deviceId
) {
    const config =
        await getConfig(
            deviceId
        );

    const model =
        await MLModelArtifact
            .findOne({
                deviceId,

                status:
                    "READY",

                active:
                    true
            });

    if (!model) {
        throw error(
            "No active READY Isolation Forest",
            409
        );
    }

    const experiments =
        await MLExperiment
            .find({
                deviceId,

                modelArtifactId:
                    model._id,

                status:
                    "STOPPED"
            })
            .select({
                _id: 1
            })
            .lean();

    const ids =
        experiments.map(
            (item) =>
                item._id
        );

    const samples =
        ids.length
            ? await MLExperimentSample
                .find({
                    experimentId: {
                        $in:
                            ids
                    },

                    modelArtifactId:
                        model._id,

                    scorable:
                        true,

                    decisionFunction: {
                        $ne:
                            null
                    }
                })
                .lean()
            : [];

    const scored =
        samples.filter(
            (sample) =>
                finite(
                    sample
                        .decisionFunction
                )
        );

    const healthy =
        scored.filter(
            (sample) =>
                !sample.isFault
        ).length;

    const fault =
        scored.filter(
            (sample) =>
                sample.isFault
        ).length;

    const minimum =
        config
            .baseline
            .minimumCalibrationSamplesPerClass;

    if (
        healthy < minimum ||
        fault < minimum
    ) {
        throw error(
            `Need at least ${minimum} scored healthy and ${minimum} scored fault samples. Current healthy=${healthy}, fault=${fault}`,
            409
        );
    }

    const uniqueScores =
        [
            ...new Set(
                scored.map(
                    (sample) =>
                        sample
                            .decisionFunction
                )
            )
        ].sort(
            (a, b) =>
                a - b
        );

    const candidates = [];

    if (
        uniqueScores.length ===
        1
    ) {
        candidates.push(
            uniqueScores[0]
        );
    } else {
        candidates.push(
            uniqueScores[0] -
            Number.EPSILON
        );

        for (
            let index = 0;
            index <
            uniqueScores.length -
                1;
            index += 1
        ) {
            candidates.push(
                (
                    uniqueScores[
                        index
                    ] +
                    uniqueScores[
                        index + 1
                    ]
                ) /
                2
            );
        }

        candidates.push(
            uniqueScores[
                uniqueScores.length -
                1
            ] +
            Number.EPSILON
        );
    }

    let best = null;

    for (
        const threshold
        of candidates
    ) {
        const result =
            metrics(
                scored,
                threshold
            );

        if (
            !best ||
            result.f1 >
                best.f1 ||
            (
                result.f1 ===
                    best.f1 &&
                result
                    .balancedAccuracy >
                    best
                        .balancedAccuracy
            )
        ) {
            best = {
                threshold,
                ...result
            };
        }
    }

    model.threshold = {
        calibrated:
            true,

        decisionFunctionThreshold:
            best.threshold,

        calibratedAt:
            new Date(),

        healthySampleCount:
            healthy,

        faultSampleCount:
            fault,

        selectedMetric:
            "F1_WITH_BALANCED_ACCURACY_TIE_BREAK",

        metricValue:
            best.f1
    };

    await model.save();

    return {
        model:
            model.toObject(),

        calibration:
            best
    };
}

async function status(
    deviceId
) {
    const config =
        await getConfig(
            deviceId
        );

    const session =
        await MLBaselineSession
            .findOne({
                deviceId
            })
            .sort({
                startedAt:
                    -1
            })
            .lean();

    let baseline;

    try {
        baseline =
            await analyzeBaseline(
                deviceId,
                session
            );

        delete baseline._rows;
    } catch (err) {
        baseline = {
            session,

            trainingEligible:
                false,

            blockers: [
                issue(
                    "BLOCKER",

                    "AUDIT_ERROR",

                    err.message
                )
            ],

            warnings: []
        };
    }

    const latest =
        await latestStoredTelemetry(
            deviceId
        ).catch(
            () => null
        );

    const model =
        await activeModel(
            deviceId
        );

    return {
        deviceId,

        featureCatalog:
            featureCatalog(),

        config,

        baseline,

        historicalStorage:
            latest
                ? {
                    collection:
                        latest
                            .collection,

                    timestamp:
                        latest
                            .timestamp
                            ?.toISOString() ||
                        null,

                    sequence:
                        latest
                            .document
                            .sequence ??
                        null,

                    measured:
                        latest
                            .document
                            .measured ||
                        null,

                    derived:
                        latest
                            .document
                            .derived ||
                        null,

                    estimated:
                        latest
                            .document
                            .estimated ||
                        null,

                    quality:
                        latest
                            .document
                            .quality ||
                        null
                }
                : null,

        model,

        models:
            await listModels(
                deviceId
            ),

        experiments:
            await listExperiments(
                deviceId
            )
    };
}

module.exports = {
    getConfig,
    updateConfig,

    startBaseline,
    stopBaseline,
    abortBaseline,

    analyzeBaseline,

    latestStoredTelemetry,

    activeModel,
    listModels,
    activateModel,

    startExperiment,
    stopExperiment,
    listExperiments,

    calibrateThreshold,

    status
};