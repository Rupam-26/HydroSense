const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");

const MLControlConfig = require("../models/MLControlConfig");
const MLModelArtifact = require("../models/MLModelArtifact");
const MLExperiment = require("../models/MLExperiment");
const MLExperimentSample = require("../models/MLExperimentSample");

const {
    finite,
    extractFeatures,
    geometryLevel,
    validCalibration
} = require("./mlFeatureService");

const PROJECT_ROOT = path.resolve(__dirname, "../..");

const WORKER_SCRIPT = path.join(
    PROJECT_ROOT,
    "tools",
    "isolation_worker.py"
);

const workers = new Map();
const trackers = new Map();
const latestByDevice = new Map();

const configCache = new Map();
const modelCache = new Map();
const experimentCache = new Map();


function pythonExecutable() {
    if (process.env.PYTHON_EXECUTABLE) {
        return process.env.PYTHON_EXECUTABLE;
    }

    return process.platform === "win32"
        ? "python"
        : "python3";
}


function nowIso(date = new Date()) {
    return date.toISOString();
}


function createTracker() {
    return {
        sensors: new Map(),

        consecutiveIsolationAnomalies: 0,

        lastUpdate: null
    };
}


function sensorValueDescriptors(payload) {
    return [
        [
            "airTemperatureC",
            payload?.measured?.airTemperatureValid === true,
            payload?.measured?.airTemperatureC
        ],

        [
            "humidityPct",
            payload?.measured?.humidityValid === true,
            payload?.measured?.humidityPct
        ],

        [
            "waterTemperatureC",
            payload?.measured?.waterTemperatureValid === true,
            payload?.measured?.waterTemperatureC
        ],

        [
            "stableDistanceCm",
            payload?.measured?.stableDistanceValid === true,
            payload?.measured?.stableDistanceCm
        ],

        [
            "waterLevelPct",
            payload?.measured?.waterLevelValid === true,
            payload?.measured?.waterLevelPct
        ],

        [
            "tdsPpm",
            payload?.estimated?.valid === true,
            payload?.estimated?.tdsPpm
        ]
    ];
}


// ============================================================
// CONFIG CACHE
// ============================================================

async function cachedConfig(deviceId) {
    const cached =
        configCache.get(deviceId);

    if (
        cached &&
        Date.now() -
            cached.loadedAt <
            5000
    ) {
        return cached.value;
    }

    const value =
        await MLControlConfig
            .findOne({
                deviceId
            })
            .lean();

    configCache.set(
        deviceId,
        {
            value,
            loadedAt:
                Date.now()
        }
    );

    return value;
}


// ============================================================
// ACTIVE MODEL CACHE
// ============================================================

async function cachedActiveModel(deviceId) {
    const cached =
        modelCache.get(deviceId);

    if (
        cached &&
        Date.now() -
            cached.loadedAt <
            5000
    ) {
        return cached.value;
    }

    const value =
        await MLModelArtifact
            .findOne({
                deviceId,

                status:
                    "READY",

                active:
                    true
            })
            .sort({
                activatedAt:
                    -1,

                createdAt:
                    -1
            })
            .lean();

    modelCache.set(
        deviceId,
        {
            value,
            loadedAt:
                Date.now()
        }
    );

    return value;
}


// ============================================================
// ACTIVE EXPERIMENT CACHE
// ============================================================

async function cachedExperiment(deviceId) {
    const cached =
        experimentCache.get(
            deviceId
        );

    if (
        cached &&
        Date.now() -
            cached.loadedAt <
            3000
    ) {
        return cached.value;
    }

    const value =
        await MLExperiment
            .findOne({
                deviceId,

                status:
                    "ACTIVE"
            })
            .sort({
                startedAt:
                    -1
            })
            .lean();

    experimentCache.set(
        deviceId,
        {
            value,
            loadedAt:
                Date.now()
        }
    );

    return value;
}


// ============================================================
// WORKER SHUTDOWN
// ============================================================

function stopWorker(key) {
    const worker =
        workers.get(key);

    if (!worker) {
        return;
    }

    worker.closed =
        true;

    try {
        worker.child.kill();
    } catch (_) {
        // Nothing else required.
    }

    if (worker.readyReject) {
        worker.readyReject(
            new Error(
                "Isolation Forest worker stopped"
            )
        );
    }

    while (
        worker.responseQueue.length
    ) {
        const pending =
            worker.responseQueue.shift();

        pending.reject(
            new Error(
                "Isolation Forest worker stopped"
            )
        );
    }

    workers.delete(key);
}


function stopWorkersForDevice(
    deviceId
) {
    for (
        const [key, worker]
        of workers.entries()
    ) {
        if (
            worker.deviceId ===
            deviceId
        ) {
            stopWorker(key);
        }
    }
}


// ============================================================
// CACHE INVALIDATION
// ============================================================

function invalidateCaches(
    deviceId = null
) {
    if (!deviceId) {
        configCache.clear();
        modelCache.clear();
        experimentCache.clear();

        for (
            const key
            of [...workers.keys()]
        ) {
            stopWorker(key);
        }

        return;
    }

    configCache.delete(
        deviceId
    );

    modelCache.delete(
        deviceId
    );

    experimentCache.delete(
        deviceId
    );

    stopWorkersForDevice(
        deviceId
    );
}


// ============================================================
// REALTIME DATA WARNINGS
// ============================================================

function buildRealtimeWarnings(
    deviceId,
    payload,
    config,
    receivedAt
) {
    const tracker =
        trackers.get(deviceId) ||
        createTracker();

    trackers.set(
        deviceId,
        tracker
    );

    tracker.lastUpdate =
        receivedAt;

    const warnings = [];

    const missingSeconds =
        config?.baseline
            ?.missingWarningSeconds;

    const unchangedSeconds =
        config?.baseline
            ?.unchangedWarningSeconds;

    for (
        const [
            key,
            valid,
            value
        ]
        of
        sensorValueDescriptors(
            payload
        )
    ) {
        let state =
            tracker.sensors.get(
                key
            );

        if (!state) {
            state = {
                missingSince:
                    null,

                unchangedSince:
                    null,

                lastValue:
                    null,

                lastValidAt:
                    null
            };

            tracker.sensors.set(
                key,
                state
            );
        }


        // ====================================================
        // MISSING DATA
        // ====================================================

        if (
            !valid ||
            !finite(value)
        ) {
            if (
                !state
                    .missingSince
            ) {
                state.missingSince =
                    receivedAt;
            }

            state.unchangedSince =
                null;

            state.lastValue =
                null;

            if (
                finite(
                    missingSeconds
                )
            ) {
                const durationSeconds =
                    (
                        receivedAt
                            .getTime() -
                        state
                            .missingSince
                            .getTime()
                    ) /
                    1000;

                if (
                    durationSeconds >=
                    missingSeconds
                ) {
                    warnings.push({
                        severity:
                            "WARNING",

                        code:
                            "LIVE_MISSING_DATA",

                        feature:
                            key,

                        message:
                            `${key} has been unavailable for ` +
                            `${durationSeconds.toFixed(1)} s.`
                    });
                }
            }

            continue;
        }


        state.missingSince =
            null;

        state.lastValidAt =
            receivedAt;


        // ====================================================
        // REPEATED EXACT VALUE
        // ====================================================

        if (
            state.lastValue !==
                null &&
            value ===
                state.lastValue
        ) {
            if (
                !state
                    .unchangedSince
            ) {
                state.unchangedSince =
                    receivedAt;
            }

            if (
                finite(
                    unchangedSeconds
                )
            ) {
                const durationSeconds =
                    (
                        receivedAt
                            .getTime() -
                        state
                            .unchangedSince
                            .getTime()
                    ) /
                    1000;

                if (
                    durationSeconds >=
                    unchangedSeconds
                ) {
                    warnings.push({
                        severity:
                            "NOTICE",

                        code:
                            "LIVE_UNCHANGED_DATA",

                        feature:
                            key,

                        message:
                            `${key} has remained exactly unchanged for ` +
                            `${durationSeconds.toFixed(1)} s. ` +
                            "This is review evidence, not an automatic " +
                            "sensor-failure claim."
                    });
                }
            }
        } else {
            state.unchangedSince =
                null;

            state.lastValue =
                value;
        }
    }


    // ========================================================
    // RESERVOIR CALIBRATION CHECK
    // ========================================================

    const reservoir =
        config?.reservoir;

    const distanceValid =
        payload?.measured
            ?.stableDistanceValid ===
        true;

    const distance =
        payload?.measured
            ?.stableDistanceCm;

    if (
        distanceValid &&
        finite(distance) &&
        validCalibration(
            reservoir
        )
    ) {
        const rawLevel =
            geometryLevel(
                distance,
                true,
                reservoir
            );

        if (
            finite(rawLevel) &&
            (
                rawLevel < 0 ||
                rawLevel > 100
            )
        ) {
            warnings.push({
                severity:
                    "WARNING",

                code:
                    "RESERVOIR_DISTANCE_OUTSIDE_CALIBRATION",

                feature:
                    "stableDistanceCm",

                message:
                    `Current ultrasonic distance ${distance.toFixed(2)} cm ` +
                    `maps to ${rawLevel.toFixed(1)}% using the configured ` +
                    "FULL/EMPTY calibration, outside the physical 0–100% range."
            });
        }
    }


    // ========================================================
    // COMMUNICATION / PACKET WARNINGS
    // ========================================================

    if (
        payload?.health
            ?.packetValid ===
        false
    ) {
        warnings.push({
            severity:
                "WARNING",

            code:
                "PACKET_INVALID",

            feature:
                null,

            message:
                "The current C3 packet is invalid."
        });
    }


    if (
        payload?.health
            ?.bleConnected ===
        false
    ) {
        warnings.push({
            severity:
                "WARNING",

            code:
                "BLE_DISCONNECTED",

            feature:
                null,

            message:
                "C3-to-S3 BLE is disconnected."
        });
    }


    if (
        payload?.health
            ?.wifiConnected ===
        false
    ) {
        warnings.push({
            severity:
                "WARNING",

            code:
                "WIFI_DISCONNECTED",

            feature:
                null,

            message:
                "S3 Wi-Fi is disconnected."
        });
    }


    return {
        tracker,
        warnings
    };
}


// ============================================================
// PYTHON WORKER
// ============================================================

function workerKey(model) {
    return String(
        model._id
    );
}


function getWorker(model) {
    const key =
        workerKey(model);

    const existing =
        workers.get(key);


    if (
        existing &&
        existing.artifactPath ===
            model.artifactPath &&
        !existing.closed
    ) {
        return existing;
    }


    if (existing) {
        stopWorker(key);
    }


    if (
        !model.artifactPath
    ) {
        throw new Error(
            "Active Isolation Forest has no artifactPath"
        );
    }


    if (
        !fs.existsSync(
            model.artifactPath
        )
    ) {
        throw new Error(
            `Isolation Forest artifact not found: ${model.artifactPath}`
        );
    }


    if (
        !fs.existsSync(
            WORKER_SCRIPT
        )
    ) {
        throw new Error(
            "Isolation Forest worker script is missing: tools/isolation_worker.py"
        );
    }


    const child =
        spawn(
            pythonExecutable(),

            [
                WORKER_SCRIPT,

                "--model",

                model.artifactPath
            ],

            {
                cwd:
                    PROJECT_ROOT,

                env:
                    process.env,

                windowsHide:
                    true,

                stdio: [
                    "pipe",
                    "pipe",
                    "pipe"
                ]
            }
        );


    let readyResolve;
    let readyReject;


    const readyPromise =
        new Promise(
            (
                resolve,
                reject
            ) => {
                readyResolve =
                    resolve;

                readyReject =
                    reject;
            }
        );


    const worker = {
        key,

        deviceId:
            model.deviceId,

        artifactPath:
            model.artifactPath,

        child,

        readyPromise,

        readyResolve,

        readyReject,

        ready:
            false,

        closed:
            false,

        stdoutBuffer:
            "",

        stderrBuffer:
            "",

        responseQueue:
            [],

        requestChain:
            Promise.resolve()
    };


    workers.set(
        key,
        worker
    );


    const readyTimeout =
        setTimeout(
            () => {
                if (
                    !worker.ready &&
                    !worker.closed
                ) {
                    worker.readyReject(
                        new Error(
                            "Isolation Forest worker did not become ready in time"
                        )
                    );

                    stopWorker(
                        key
                    );
                }
            },

            10000
        );


    // ========================================================
    // PYTHON STDOUT
    // ========================================================

    child.stdout.on(
        "data",

        (chunk) => {
            worker.stdoutBuffer +=
                chunk.toString();

            const lines =
                worker
                    .stdoutBuffer
                    .split(
                        /\r?\n/
                    );

            worker.stdoutBuffer =
                lines.pop() ||
                "";


            for (
                const line
                of lines
            ) {
                const text =
                    line.trim();

                if (!text) {
                    continue;
                }

                let message;

                try {
                    message =
                        JSON.parse(
                            text
                        );
                } catch (_) {
                    continue;
                }


                // ============================================
                // WORKER READY
                // ============================================

                if (
                    message.type ===
                    "ready"
                ) {
                    worker.ready =
                        true;

                    clearTimeout(
                        readyTimeout
                    );

                    worker.readyResolve(
                        message
                    );

                    continue;
                }


                // ============================================
                // FATAL PYTHON ERROR
                // ============================================

                if (
                    message.type ===
                    "fatal"
                ) {
                    const fatalError =
                        new Error(
                            message.error ||
                            "Isolation Forest worker reported a fatal error"
                        );

                    if (
                        !worker.ready
                    ) {
                        clearTimeout(
                            readyTimeout
                        );

                        worker.readyReject(
                            fatalError
                        );
                    }

                    const pending =
                        worker
                            .responseQueue
                            .shift();

                    if (pending) {
                        pending.reject(
                            fatalError
                        );
                    }

                    continue;
                }


                // ============================================
                // NORMAL SCORE RESPONSE
                // ============================================

                const pending =
                    worker
                        .responseQueue
                        .shift();

                if (!pending) {
                    continue;
                }


                if (
                    message.success ===
                    false
                ) {
                    pending.reject(
                        new Error(
                            message.error ||
                            message.reason ||
                            "Isolation Forest worker request failed"
                        )
                    );
                } else {
                    pending.resolve(
                        message
                    );
                }
            }
        }
    );


    // ========================================================
    // PYTHON STDERR
    // ========================================================

    child.stderr.on(
        "data",

        (chunk) => {
            worker.stderrBuffer +=
                chunk.toString();
        }
    );


    // ========================================================
    // PROCESS ERROR
    // ========================================================

    child.on(
        "error",

        (error) => {
            if (
                !worker.ready
            ) {
                clearTimeout(
                    readyTimeout
                );

                worker.readyReject(
                    error
                );
            }
        }
    );


    // ========================================================
    // PROCESS CLOSED
    // ========================================================

    child.on(
        "close",

        () => {
            worker.closed =
                true;

            clearTimeout(
                readyTimeout
            );

            const message =
                worker
                    .stderrBuffer
                    .trim() ||
                "Isolation Forest worker exited";


            if (
                !worker.ready
            ) {
                worker.readyReject(
                    new Error(
                        message
                    )
                );
            }


            while (
                worker
                    .responseQueue
                    .length
            ) {
                const pending =
                    worker
                        .responseQueue
                        .shift();

                pending.reject(
                    new Error(
                        message
                    )
                );
            }


            workers.delete(
                key
            );
        }
    );


    return worker;
}


// ============================================================
// SCORE WITH PYTHON
// ============================================================

function scoreWithWorker(
    model,
    values
) {
    const worker =
        getWorker(model);


    const run =
        async () => {
            await worker
                .readyPromise;


            if (
                worker.closed
            ) {
                throw new Error(
                    "Isolation Forest worker is not running"
                );
            }


            return new Promise(
                (
                    resolve,
                    reject
                ) => {
                    let wrappedResolve;


                    const timeout =
                        setTimeout(
                            () => {
                                const index =
                                    worker
                                        .responseQueue
                                        .findIndex(
                                            (item) =>
                                                item.resolve ===
                                                wrappedResolve
                                        );


                                if (
                                    index >= 0
                                ) {
                                    worker
                                        .responseQueue
                                        .splice(
                                            index,
                                            1
                                        );
                                }


                                reject(
                                    new Error(
                                        "Isolation Forest worker timed out"
                                    )
                                );
                            },

                            5000
                        );


                    wrappedResolve =
                        (value) => {
                            clearTimeout(
                                timeout
                            );

                            resolve(
                                value
                            );
                        };


                    const wrappedReject =
                        (error) => {
                            clearTimeout(
                                timeout
                            );

                            reject(
                                error
                            );
                        };


                    worker
                        .responseQueue
                        .push({
                            resolve:
                                wrappedResolve,

                            reject:
                                wrappedReject
                        });


                    worker.child.stdin.write(
                        JSON.stringify({
                            command:
                                "score",

                            values
                        }) +
                        "\n",

                        (error) => {
                            if (!error) {
                                return;
                            }


                            const pending =
                                worker
                                    .responseQueue
                                    .pop();


                            if (pending) {
                                pending.reject(
                                    error
                                );
                            }
                        }
                    );
                }
            );
        };


    /*
     * One worker processes one request at a time.
     *
     * This guarantees that Python responses stay matched to the
     * correct live packet without requiring fake request IDs.
     */
    worker.requestChain =
        worker
            .requestChain
            .then(
                run,
                run
            );


    return worker.requestChain;
}


// ============================================================
// EXPERIMENT SAMPLE STORAGE
// ============================================================

async function persistExperimentSample(
    deviceId,
    experiment,
    model,
    payload,
    receivedAt,
    extracted,
    score
) {
    if (!experiment) {
        return;
    }


    if (
        String(
            experiment
                .modelArtifactId
        ) !==
        String(
            model._id
        )
    ) {
        return;
    }


    const threshold =
        model
            ?.threshold
            ?.calibrated ===
            true &&
        finite(
            model
                ?.threshold
                ?.decisionFunctionThreshold
        )
            ? model
                .threshold
                .decisionFunctionThreshold
            : null;


    const scorable =
        extracted.complete ===
            true &&
        score?.scorable ===
            true &&
        finite(
            score
                ?.decisionFunction
        );


    let reason =
        null;


    if (
        !extracted.complete
    ) {
        reason =
            `Missing model feature(s): ${extracted.missing.join(", ")}`;
    } else if (
        !scorable
    ) {
        reason =
            score?.reason ||
            "Isolation Forest did not produce a finite score";
    }


    await MLExperimentSample
        .create({
            deviceId,

            experimentId:
                experiment._id,

            modelArtifactId:
                model._id,

            sourceTimestamp:
                receivedAt,

            sequence:
                Number.isInteger(
                    payload.sequence
                )
                    ? payload.sequence
                    : null,

            isFault:
                experiment
                    .isFault ===
                true,

            label:
                experiment.label,

            scorable,

            features:
                extracted.values,

            decisionFunction:
                scorable
                    ? score
                        .decisionFunction
                    : null,

            scoreSamples:
                scorable &&
                finite(
                    score
                        ?.scoreSamples
                )
                    ? score
                        .scoreSamples
                    : null,

            thresholdAvailable:
                threshold !==
                null,

            thresholdAtCapture:
                threshold,

            predictedFault:
                scorable &&
                threshold !==
                    null
                    ? score
                        .decisionFunction <
                        threshold
                    : null,

            unscorableReason:
                reason
        });


    await MLExperiment
        .updateOne(
            {
                _id:
                    experiment._id
            },

            scorable
                ? {
                    $inc: {
                        scoredSampleCount:
                            1
                    }
                }
                : {
                    $inc: {
                        unscorableSampleCount:
                            1
                    }
                }
        );
}


// ============================================================
// EDGE ANOMALY
// ============================================================

function edgeAnomalySummary(
    payload
) {
    const anomaly =
        payload?.anomaly;

    if (!anomaly) {
        return null;
    }

    return anomaly;
}


// ============================================================
// PROCESS ONE REALTIME PACKET
// ============================================================

async function observeRealtime(
    payload,
    receivedAt = new Date(),
    io = null
) {
    const deviceId =
        payload?.deviceId;


    if (!deviceId) {
        return null;
    }


    const [
        config,
        model,
        experiment
    ] =
        await Promise.all([
            cachedConfig(
                deviceId
            ),

            cachedActiveModel(
                deviceId
            ),

            cachedExperiment(
                deviceId
            )
        ]);


    const warningState =
        buildRealtimeWarnings(
            deviceId,
            payload,
            config,
            receivedAt
        );


    const tracker =
        warningState
            .tracker;


    const warnings =
        warningState
            .warnings;


    const ml = {
        available:
            Boolean(
                model
            ),

        modelArtifactId:
            model?._id ||
            null,

        versionLabel:
            model?.versionLabel ||
            null,

        features:
            Array.isArray(
                model?.features
            )
                ? model.features
                : [],

        completeForModel:
            false,

        missingFeatures:
            [],

        decisionFunction:
            null,

        scoreSamples:
            null,

        thresholdCalibrated:
            model
                ?.threshold
                ?.calibrated ===
            true,

        decisionFunctionThreshold:
            finite(
                model
                    ?.threshold
                    ?.decisionFunctionThreshold
            )
                ? model
                    .threshold
                    .decisionFunctionThreshold
                : null,

        classification:
            model
                ? "WAITING_FOR_SCORE"
                : "NO_ACTIVE_MODEL",

        consecutiveAnomalousScores:
            tracker
                .consecutiveIsolationAnomalies,

        scoreIsProbability:
            false
    };


    // ========================================================
    // SCORE ACTIVE MODEL
    // ========================================================

    if (
        model &&
        config
    ) {
        const extracted =
            extractFeatures(
                payload,

                model.features,

                config
            );


        ml.completeForModel =
            extracted.complete;


        ml.missingFeatures =
            extracted.missing;


        // ====================================================
        // INPUT MISSING
        // ====================================================

        if (
            !extracted.complete
        ) {
            tracker
                .consecutiveIsolationAnomalies =
                0;


            ml.classification =
                "UNSCORABLE_MISSING_INPUT";


            warnings.push({
                severity:
                    "WARNING",

                code:
                    "MODEL_INPUT_MISSING",

                feature:
                    extracted
                        .missing
                        .join(","),

                message:
                    "Isolation Forest cannot score this packet because " +
                    `required input(s) are unavailable: ${extracted.missing.join(", ")}.`
            });


            if (experiment) {
                await persistExperimentSample(
                    deviceId,

                    experiment,

                    model,

                    payload,

                    receivedAt,

                    extracted,

                    null
                );
            }
        }

        // ====================================================
        // SCORE
        // ====================================================

        else {
            try {
                const result =
                    await scoreWithWorker(
                        model,

                        extracted.values
                    );


                if (
                    result.scorable !==
                        true ||
                    !finite(
                        result
                            .decisionFunction
                    )
                ) {
                    tracker
                        .consecutiveIsolationAnomalies =
                        0;


                    ml.classification =
                        "UNSCORABLE";


                    warnings.push({
                        severity:
                            "WARNING",

                        code:
                            "MODEL_UNSCORABLE",

                        feature:
                            null,

                        message:
                            result.reason ||
                            "Isolation Forest did not produce a valid score."
                    });
                } else {
                    ml.decisionFunction =
                        result
                            .decisionFunction;


                    ml.scoreSamples =
                        finite(
                            result
                                .scoreSamples
                        )
                            ? result
                                .scoreSamples
                            : null;


                    const threshold =
                        ml
                            .thresholdCalibrated &&
                        finite(
                            ml
                                .decisionFunctionThreshold
                        )
                            ? ml
                                .decisionFunctionThreshold
                            : null;


                    // ========================================
                    // NO LABELLED THRESHOLD YET
                    // ========================================

                    if (
                        threshold ===
                        null
                    ) {
                        tracker
                            .consecutiveIsolationAnomalies =
                            0;


                        ml.classification =
                            "SCORE_ONLY_THRESHOLD_NOT_CALIBRATED";
                    }

                    // ========================================
                    // CALIBRATED CLASSIFICATION
                    // ========================================

                    else {
                        const predictedFault =
                            ml
                                .decisionFunction <
                            threshold;


                        ml.classification =
                            predictedFault
                                ? "ANOMALOUS"
                                : "BASELINE_LIKE";


                        if (
                            predictedFault
                        ) {
                            tracker
                                .consecutiveIsolationAnomalies +=
                                1;


                            warnings.push({
                                severity:
                                    "NOTICE",

                                code:
                                    "ISOLATION_FOREST_ANOMALY",

                                feature:
                                    null,

                                message:
                                    `Isolation Forest score ${ml.decisionFunction.toFixed(6)} ` +
                                    `is below calibrated threshold ${threshold.toFixed(6)}.`
                            });
                        } else {
                            tracker
                                .consecutiveIsolationAnomalies =
                                0;
                        }


                        // ====================================
                        // PERSISTENT ANOMALY
                        // ====================================

                        const persistenceRequired =
                            config
                                ?.baseline
                                ?.sustainedStrangeSamples;


                        if (
                            predictedFault &&
                            finite(
                                persistenceRequired
                            ) &&
                            tracker
                                .consecutiveIsolationAnomalies >=
                                persistenceRequired
                        ) {
                            warnings.push({
                                severity:
                                    "WARNING",

                                code:
                                    "SUSTAINED_ISOLATION_FOREST_ANOMALY",

                                feature:
                                    null,

                                message:
                                    "Isolation Forest has remained outside the " +
                                    "calibrated healthy decision boundary for " +
                                    `${tracker.consecutiveIsolationAnomalies} consecutive live samples.`
                            });
                        }
                    }
                }


                ml.consecutiveAnomalousScores =
                    tracker
                        .consecutiveIsolationAnomalies;


                // ============================================
                // STORE ONLY WHEN EXPERIMENT IS ACTIVE
                // ============================================

                if (experiment) {
                    await persistExperimentSample(
                        deviceId,

                        experiment,

                        model,

                        payload,

                        receivedAt,

                        extracted,

                        result
                    );
                }
            } catch (
                runtimeError
            ) {
                tracker
                    .consecutiveIsolationAnomalies =
                    0;


                ml.classification =
                    "RUNTIME_ERROR";


                warnings.push({
                    severity:
                        "WARNING",

                    code:
                        "MODEL_RUNTIME_ERROR",

                    feature:
                        null,

                    message:
                        `Isolation Forest scoring failed: ${runtimeError.message}`
                });


                if (experiment) {
                    await persistExperimentSample(
                        deviceId,

                        experiment,

                        model,

                        payload,

                        receivedAt,

                        extracted,

                        {
                            scorable:
                                false,

                            reason:
                                runtimeError
                                    .message
                        }
                    );
                }
            }
        }
    }


    // ========================================================
    // COMPLETE WEBSITE ML STATE
    // ========================================================

    const live = {
        deviceId,

        receivedAt:
            nowIso(
                receivedAt
            ),

        sequence:
            Number.isInteger(
                payload.sequence
            )
                ? payload.sequence
                : null,


        // ====================================================
        // ORIGINAL S3 DATA
        // ====================================================

        livePacket: {
            measured:
                payload.measured ||
                null,

            derived:
                payload.derived ||
                null,

            estimated:
                payload.estimated ||
                null,

            quality:
                payload.quality ||
                null,

            health:
                payload.health ||
                null
        },


        // ====================================================
        // EXISTING TINYML TDS MODEL
        // ====================================================

        tinyMLTds: {
            available:
                payload
                    ?.estimated !=
                null,

            valid:
                payload
                    ?.estimated
                    ?.valid ===
                true,

            tdsPpm:
                finite(
                    payload
                        ?.estimated
                        ?.tdsPpm
                )
                    ? payload
                        .estimated
                        .tdsPpm
                    : null,

            model:
                payload
                    ?.estimated
                    ?.model ||
                null,

            status:
                payload
                    ?.estimated
                    ?.status ||
                null,

            inferenceMicros:
                finite(
                    payload
                        ?.estimated
                        ?.inferenceMicros
                )
                    ? payload
                        .estimated
                        .inferenceMicros
                    : null
        },


        // ====================================================
        // EXISTING S3 ANOMALY ENGINE
        // ====================================================

        edgeAnomaly:
            edgeAnomalySummary(
                payload
            ),


        // ====================================================
        // SENSOR TRUST
        // ====================================================

        sensorTrust:
            payload.sensorTrust ||
            null,


        // ====================================================
        // SERVER ISOLATION FOREST
        // ====================================================

        isolationForest:
            ml,


        // ====================================================
        // WARNINGS
        // ====================================================

        warnings,


        // ====================================================
        // ACTIVE CONTROLLED EXPERIMENT
        // ====================================================

        activeExperiment:
            experiment
                ? {
                    id:
                        experiment._id,

                    label:
                        experiment.label,

                    isFault:
                        experiment.isFault,

                    startedAt:
                        experiment.startedAt
                }
                : null
    };


    latestByDevice.set(
        deviceId,
        live
    );


    // ========================================================
    // PUSH TO CONTROL ROOM
    // ========================================================

    if (io) {
        io.emit(
            "hydrosense:ml-live",
            live
        );


        io.emit(
            `hydrosense:ml-live:${deviceId}`,
            live
        );
    }


    return live;
}


// ============================================================
// GET LATEST WEBSITE ML STATE
// ============================================================

function latest(deviceId) {
    return (
        latestByDevice.get(
            deviceId
        ) ||
        null
    );
}


// ============================================================
// SHUTDOWN
// ============================================================

function shutdownAllWorkers() {
    for (
        const key
        of [...workers.keys()]
    ) {
        stopWorker(
            key
        );
    }
}


module.exports = {
    observeRealtime,

    latest,

    invalidateCaches,

    shutdownAllWorkers
};