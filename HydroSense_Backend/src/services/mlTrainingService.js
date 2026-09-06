// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { spawn } = require("node:child_process");

const MLBaselineSession =
    require("../models/MLBaselineSession");

const MLModelArtifact =
    require("../models/MLModelArtifact");

const mlControlService =
    require("./mlControlService");


const PROJECT_ROOT =
    path.resolve(
        __dirname,
        "../.."
    );

const RUNTIME_ROOT =
    path.join(
        PROJECT_ROOT,
        "ml_runtime"
    );

const TRAINING_SCRIPT =
    path.join(
        PROJECT_ROOT,
        "tools",
        "train_isolation_forest_web.py"
    );


const trainingJobs =
    new Map();


function createError(
    message,
    statusCode = 400
) {
    const error =
        new Error(message);

    error.statusCode =
        statusCode;

    return error;
}


function pythonExecutable() {
    if (
        process.env
            .PYTHON_EXECUTABLE
    ) {
        return process.env
            .PYTHON_EXECUTABLE;
    }

    return process.platform ===
        "win32"
        ? "python"
        : "python3";
}


function ensureRuntimeDirectory() {
    fs.mkdirSync(
        RUNTIME_ROOT,
        {
            recursive: true
        }
    );
}


function safeDeviceName(
    deviceId
) {
    return String(
        deviceId
    )
        .replace(
            /[^a-zA-Z0-9_-]/g,
            "_"
        );
}


function makeVersionLabel() {
    const now =
        new Date();

    const stamp =
        now
            .toISOString()
            .replace(
                /[-:.TZ]/g,
                ""
            )
            .slice(
                0,
                14
            );

    return `IF_${stamp}`;
}


function emitProgress(
    io,
    deviceId,
    data
) {
    if (!io) {
        return;
    }

    const payload = {
        deviceId,

        ...data,

        serverTimestamp:
            new Date()
                .toISOString()
    };

    io.emit(
        "hydrosense:ml-training",
        payload
    );

    io.emit(
        `hydrosense:ml-training:${deviceId}`,
        payload
    );
}


function publicJob(
    job
) {
    if (!job) {
        return null;
    }

    return {
        jobId:
            job.jobId,

        deviceId:
            job.deviceId,

        modelArtifactId:
            job.modelArtifactId,

        versionLabel:
            job.versionLabel,

        status:
            job.status,

        progressPct:
            job.progressPct,

        stage:
            job.stage,

        message:
            job.message,

        startedAt:
            job.startedAt,

        finishedAt:
            job.finishedAt,

        error:
            job.error
    };
}


function getTrainingState(
    deviceId
) {
    return publicJob(
        trainingJobs.get(
            deviceId
        )
    );
}


function statistics(
    values
) {
    if (
        !Array.isArray(
            values
        ) ||
        values.length === 0
    ) {
        return {
            count: 0,

            minimum: null,

            maximum: null,

            mean: null,

            median: null,

            q01: null,

            q05: null,

            q95: null,

            q99: null
        };
    }

    const data =
        values
            .filter(
                (value) =>
                    typeof value ===
                        "number" &&
                    Number.isFinite(
                        value
                    )
            )
            .sort(
                (a, b) =>
                    a - b
            );

    if (!data.length) {
        return {
            count: 0,

            minimum: null,

            maximum: null,

            mean: null,

            median: null,

            q01: null,

            q05: null,

            q95: null,

            q99: null
        };
    }

    function quantile(q) {
        if (
            data.length === 1
        ) {
            return data[0];
        }

        const position =
            (
                data.length -
                1
            ) * q;

        const lower =
            Math.floor(
                position
            );

        const upper =
            Math.ceil(
                position
            );

        if (
            lower === upper
        ) {
            return data[
                lower
            ];
        }

        return (
            data[lower] +
            (
                data[upper] -
                data[lower]
            ) *
            (
                position -
                lower
            )
        );
    }

    const sum =
        data.reduce(
            (
                total,
                value
            ) =>
                total +
                value,
            0
        );

    return {
        count:
            data.length,

        minimum:
            data[0],

        maximum:
            data[
                data.length -
                1
            ],

        mean:
            sum /
            data.length,

        median:
            quantile(
                0.5
            ),

        q01:
            quantile(
                0.01
            ),

        q05:
            quantile(
                0.05
            ),

        q95:
            quantile(
                0.95
            ),

        q99:
            quantile(
                0.99
            )
    };
}


async function setModelFailure(
    artifact,
    message
) {
    artifact.status =
        "FAILED";

    artifact.trainingError =
        String(
            message ||
            "Unknown training error"
        );

    artifact.active =
        false;

    await artifact.save();
}


async function setModelCancelled(
    artifact
) {
    artifact.status =
        "CANCELLED";

    artifact.trainingError =
        "Training cancelled by operator";

    artifact.active =
        false;

    artifact.artifactPath =
        null;

    artifact.cancelledAt =
        new Date();

    await artifact.save();
}


async function completeTraining(
    {
        artifact,
        baseline,
        session,
        output,
        modelPath
    }
) {
    const trainingScores =
        Array.isArray(
            output.trainingScores
        )
            ? output.trainingScores
            : [];

    const validationScores =
        Array.isArray(
            output.validationScores
        )
            ? output.validationScores
            : [];

    artifact.status =
        "READY";

    artifact.artifactPath =
        modelPath;

    artifact.sampleCounts = {
        total:
            output.sampleCounts
                ?.total ??
            baseline
                .completeSamples,

        training:
            output.sampleCounts
                ?.training ??
            trainingScores.length,

        validation:
            output.sampleCounts
                ?.validation ??
            validationScores.length
    };

    artifact.trainingScores =
        statistics(
            trainingScores
        );

    artifact.validationScores =
        statistics(
            validationScores
        );

    artifact.baselineTimeRange = {
        from:
            session.startedAt,

        to:
            output.baselineTo
                ? new Date(
                    output
                        .baselineTo
                )
                : new Date()
    };

    artifact.trainingError =
        null;

    artifact.cancelledAt =
        null;

    artifact.trainedAt =
        new Date();

    /*
     * No anomaly threshold is generated during training.
     * A production threshold is calibrated later using real,
     * labelled healthy-control and fault experiments.
     */
    artifact.threshold = {
        calibrated:
            false,

        decisionFunctionThreshold:
            null,

        calibratedAt:
            null,

        healthySampleCount:
            0,

        faultSampleCount:
            0,

        selectedMetric:
            null,

        metricValue:
            null
    };

    const existingActive =
        await MLModelArtifact
            .findOne({
                deviceId:
                    artifact
                        .deviceId,

                status:
                    "READY",

                active:
                    true,

                _id: {
                    $ne:
                        artifact._id
                }
            });

    if (!existingActive) {
        artifact.active =
            true;

        artifact.activatedAt =
            new Date();
    } else {
        artifact.active =
            false;

        artifact.activatedAt =
            null;
    }

    await artifact.save();

    if (
        session.status ===
        "ACTIVE"
    ) {
        session.status =
            "TRAINED";

        session.stoppedAt =
            new Date();

        session.trainedAt =
            new Date();

        await session.save();
    } else {
        session.trainedAt =
            new Date();

        await session.save();
    }

    return artifact
        .toObject();
}


async function startTraining(
    deviceId,
    io = null
) {
    if (
        trainingJobs.has(
            deviceId
        )
    ) {
        const current =
            trainingJobs.get(
                deviceId
            );

        if (
            current.status ===
                "RUNNING" ||
            current.status ===
                "STARTING" ||
            current.status ===
                "FINALIZING"
        ) {
            throw createError(
                "Isolation Forest training is already running for this device",
                409
            );
        }
    }

    if (
        !fs.existsSync(
            TRAINING_SCRIPT
        )
    ) {
        throw createError(
            "Python training script is missing: tools/train_isolation_forest_web.py",
            500
        );
    }

    const session =
        await MLBaselineSession
            .findOne({
                deviceId
            })
            .sort({
                startedAt: -1
            });

    if (!session) {
        throw createError(
            "No baseline session exists",
            409
        );
    }

    if (
        session.status ===
        "ABORTED"
    ) {
        throw createError(
            "The latest baseline session was aborted",
            409
        );
    }

    if (
        session.status ===
        "TRAINED"
    ) {
        throw createError(
            "This baseline session has already been used for training. Start a new baseline for another model.",
            409
        );
    }

    const baseline =
        await mlControlService
            .analyzeBaseline(
                deviceId,
                session.toObject()
            );

    if (
        !baseline
            .trainingEligible
    ) {
        const reasons =
            Array.isArray(
                baseline.blockers
            )
                ? baseline
                    .blockers
                    .map(
                        (item) =>
                            item.message
                    )
                    .join("; ")
                : "Baseline is not eligible";

        throw createError(
            `Training blocked: ${reasons}`,
            409
        );
    }

    const rows =
        Array.isArray(
            baseline._rows
        )
            ? baseline._rows
            : [];

    if (
        rows.length < 3
    ) {
        throw createError(
            "At least 3 complete baseline samples are required",
            409
        );
    }

    const snapshot =
        session
            .configurationSnapshot;

    const features =
        snapshot
            ?.baseline
            ?.features;

    const trainingConfiguration =
        snapshot
            ?.isolationForest;

    if (
        !Array.isArray(
            features
        ) ||
        !features.length
    ) {
        throw createError(
            "Baseline has no ML features",
            409
        );
    }

    if (
        !trainingConfiguration
    ) {
        throw createError(
            "Isolation Forest configuration is missing",
            409
        );
    }

    const samples = [];

    for (
        const row
        of rows
    ) {
        const vector =
            features.map(
                (feature) =>
                    row
                        .values[
                            feature
                        ]
            );

        const validVector =
            vector.every(
                (value) =>
                    typeof value ===
                        "number" &&
                    Number.isFinite(
                        value
                    )
            );

        if (!validVector) {
            continue;
        }

        samples.push({
            timestamp:
                row.timestamp instanceof
                    Date
                    ? row
                        .timestamp
                        .toISOString()
                    : new Date(
                        row.timestamp
                    )
                        .toISOString(),

            sequence:
                row.sequence ??
                null,

            values:
                vector
        });
    }

    if (
        samples.length < 3
    ) {
        throw createError(
            "Not enough complete finite samples remain for training",
            409
        );
    }

    ensureRuntimeDirectory();

    const deviceDirectory =
        path.join(
            RUNTIME_ROOT,
            safeDeviceName(
                deviceId
            )
        );

    fs.mkdirSync(
        deviceDirectory,
        {
            recursive: true
        }
    );

    const jobId =
        crypto.randomUUID();

    const versionLabel =
        makeVersionLabel();

    const modelDirectory =
        path.join(
            deviceDirectory,
            versionLabel
        );

    fs.mkdirSync(
        modelDirectory,
        {
            recursive: true
        }
    );

    const inputPath =
        path.join(
            modelDirectory,
            "training_input.json"
        );

    const outputPath =
        path.join(
            modelDirectory,
            "training_output.json"
        );

    const modelPath =
        path.join(
            modelDirectory,
            "isolation_forest.joblib"
        );

    const trainingInput = {
        schemaVersion: 1,

        jobId,

        deviceId,

        versionLabel,

        features,

        samples,

        isolationForest: {
            nEstimators:
                trainingConfiguration
                    .nEstimators,

            trainFraction:
                trainingConfiguration
                    .trainFraction,

            randomState:
                trainingConfiguration
                    .randomState
        },

        productionThresholdDefined:
            false
    };

    fs.writeFileSync(
        inputPath,
        JSON.stringify(
            trainingInput,
            null,
            2
        ),
        "utf8"
    );

    const artifact =
        await MLModelArtifact
            .create({
                deviceId,

                baselineSessionId:
                    session._id,

                modelType:
                    "ISOLATION_FOREST",

                versionLabel,

                status:
                    "TRAINING",

                active:
                    false,

                artifactPath:
                    null,

                features,

                trainingConfiguration: {
                    nEstimators:
                        trainingConfiguration
                            .nEstimators,

                    trainFraction:
                        trainingConfiguration
                            .trainFraction,

                    randomState:
                        trainingConfiguration
                            .randomState
                },

                sampleCounts: {
                    total:
                        samples.length,

                    training:
                        0,

                    validation:
                        0
                },

                baselineTimeRange: {
                    from:
                        session
                            .startedAt,

                    to:
                        new Date()
                },

                threshold: {
                    calibrated:
                        false,

                    decisionFunctionThreshold:
                        null,

                    calibratedAt:
                        null,

                    healthySampleCount:
                        0,

                    faultSampleCount:
                        0,

                    selectedMetric:
                        null,

                    metricValue:
                        null
                }
            });

    const job = {
        jobId,

        deviceId,

        modelArtifactId:
            String(
                artifact._id
            ),

        versionLabel,

        status:
            "STARTING",

        progressPct:
            0,

        stage:
            "PREPARING",

        message:
            "Preparing real baseline samples",

        startedAt:
            new Date()
                .toISOString(),

        finishedAt:
            null,

        error:
            null,

        child:
            null,

        artifact
    };

    trainingJobs.set(
        deviceId,
        job
    );

    emitProgress(
        io,
        deviceId,
        publicJob(
            job
        )
    );

    const child =
        spawn(
            pythonExecutable(),
            [
                TRAINING_SCRIPT,

                "--input",
                inputPath,

                "--output",
                outputPath,

                "--model",
                modelPath
            ],
            {
                cwd:
                    PROJECT_ROOT,

                env:
                    process.env,

                windowsHide:
                    true
            }
        );

    job.child =
        child;

    job.status =
        "RUNNING";

    job.stage =
        "PYTHON_START";

    job.message =
        "Python Isolation Forest training started";

    job.progressPct =
        5;

    emitProgress(
        io,
        deviceId,
        publicJob(
            job
        )
    );

    let stdoutBuffer = "";
    let stderrBuffer = "";

    child.stdout.on(
        "data",
        (chunk) => {
            stdoutBuffer +=
                chunk.toString();

            const lines =
                stdoutBuffer.split(
                    /\r?\n/
                );

            stdoutBuffer =
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

                if (
                    message.type !==
                    "progress"
                ) {
                    continue;
                }

                if (
                    typeof message
                        .progressPct ===
                        "number" &&
                    Number.isFinite(
                        message
                            .progressPct
                    )
                ) {
                    job.progressPct =
                        Math.max(
                            0,
                            Math.min(
                                100,
                                message
                                    .progressPct
                            )
                        );
                }

                if (
                    typeof message
                        .stage ===
                    "string"
                ) {
                    job.stage =
                        message.stage;
                }

                if (
                    typeof message
                        .message ===
                    "string"
                ) {
                    job.message =
                        message.message;
                }

                emitProgress(
                    io,
                    deviceId,
                    publicJob(
                        job
                    )
                );
            }
        }
    );

    child.stderr.on(
        "data",
        (chunk) => {
            stderrBuffer +=
                chunk.toString();
        }
    );

    child.on(
        "error",
        async (
            processError
        ) => {
            if (
                job.status ===
                "CANCELLED"
            ) {
                return;
            }

            job.status =
                "FAILED";

            job.stage =
                "FAILED";

            job.error =
                processError
                    .message;

            job.message =
                "Unable to start Python training";

            job.finishedAt =
                new Date()
                    .toISOString();

            await setModelFailure(
                artifact,
                processError
                    .message
            ).catch(
                () => {}
            );

            emitProgress(
                io,
                deviceId,
                publicJob(
                    job
                )
            );
        }
    );

    child.on(
        "close",
        async (
            exitCode
        ) => {
            if (
                job.status ===
                    "FAILED" ||
                job.status ===
                    "CANCELLED"
            ) {
                return;
            }

            job.status =
                "FINALIZING";

            job.stage =
                "FINALIZING";

            job.message =
                "Finalizing trained model";

            try {
                if (
                    exitCode !== 0
                ) {
                    throw new Error(
                        stderrBuffer
                            .trim() ||
                        `Python training exited with code ${exitCode}`
                    );
                }

                if (
                    !fs.existsSync(
                        outputPath
                    )
                ) {
                    throw new Error(
                        "Python training completed without training_output.json"
                    );
                }

                if (
                    !fs.existsSync(
                        modelPath
                    )
                ) {
                    throw new Error(
                        "Python training completed without the joblib model"
                    );
                }

                const output =
                    JSON.parse(
                        fs.readFileSync(
                            outputPath,
                            "utf8"
                        )
                    );

                if (
                    output.success !==
                    true
                ) {
                    throw new Error(
                        output.error ||
                        "Python training reported failure"
                    );
                }

                const completedModel =
                    await completeTraining({
                        artifact,

                        baseline,

                        session,

                        output,

                        modelPath
                    });

                job.status =
                    "COMPLETED";

                job.stage =
                    "READY";

                job.progressPct =
                    100;

                job.message =
                    completedModel
                        .active
                        ? "Isolation Forest trained and activated"
                        : "Isolation Forest trained; existing active model was preserved";

                job.finishedAt =
                    new Date()
                        .toISOString();

                emitProgress(
                    io,
                    deviceId,
                    publicJob(
                        job
                    )
                );
            } catch (
                trainingError
            ) {
                job.status =
                    "FAILED";

                job.stage =
                    "FAILED";

                job.error =
                    trainingError
                        .message;

                job.message =
                    "Isolation Forest training failed";

                job.finishedAt =
                    new Date()
                        .toISOString();

                await setModelFailure(
                    artifact,
                    trainingError
                        .message
                ).catch(
                    () => {}
                );

                emitProgress(
                    io,
                    deviceId,
                    publicJob(
                        job
                    )
                );
            }
        }
    );

    return publicJob(
        job
    );
}


async function cancelTraining(
    deviceId
) {
    const job =
        trainingJobs.get(
            deviceId
        );

    if (
        !job ||
        (
            job.status !==
                "RUNNING" &&
            job.status !==
                "STARTING"
        )
    ) {
        throw createError(
            "No active Isolation Forest training job",
            404
        );
    }

    job.status =
        "CANCELLED";

    job.stage =
        "CANCELLED";

    job.message =
        "Training cancelled by operator";

    job.finishedAt =
        new Date()
            .toISOString();

    if (
        job.child &&
        !job.child.killed
    ) {
        job.child.kill();
    }

    await setModelCancelled(
        job.artifact
    );

    return publicJob(
        job
    );
}


module.exports = {
    startTraining,

    getTrainingState,

    cancelTraining
};