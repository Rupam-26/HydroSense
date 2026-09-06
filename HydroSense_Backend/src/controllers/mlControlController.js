// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

const mlControlService =
    require("../services/mlControlService");

const mlTrainingService =
    require("../services/mlTrainingService");

const mlRuntimeService =
    require("../services/mlRuntimeService");


// ============================================================
// HELPERS
// ============================================================

function deviceIdFromRequest(req) {
    return String(
        req.params?.deviceId ||
        req.query?.deviceId ||
        req.body?.deviceId ||
        ""
    ).trim();
}


function requireDeviceId(req) {
    const deviceId =
        deviceIdFromRequest(req);

    if (!deviceId) {
        const error =
            new Error(
                "deviceId is required"
            );

        error.statusCode =
            400;

        throw error;
    }

    return deviceId;
}


function ioFromRequest(req) {
    return req.app.get("io") || null;
}


// ============================================================
// COMPLETE ML CONTROL STATUS
// ============================================================

async function getStatus(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const data =
            await mlControlService
                .status(
                    deviceId
                );

        const live =
            mlRuntimeService
                .latest(
                    deviceId
                );

        const training =
            mlTrainingService
                .getTrainingState(
                    deviceId
                );

        return res.status(200).json({
            success:
                true,

            deviceId,

            data: {
                ...data,

                realtimeML:
                    live,

                training
            }
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// SAVE CONFIGURATION
// ============================================================

async function saveConfiguration(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const configuration =
            await mlControlService
                .updateConfig(
                    deviceId,
                    req.body || {}
                );

        mlRuntimeService
            .invalidateCaches(
                deviceId
            );

        const io =
            ioFromRequest(req);

        if (io) {
            io.emit(
                "hydrosense:ml-config",
                {
                    deviceId,

                    configuration,

                    serverTimestamp:
                        new Date()
                            .toISOString()
                }
            );
        }

        return res.status(200).json({
            success:
                true,

            message:
                "ML configuration saved",

            deviceId,

            data:
                configuration
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// BASELINE START
// ============================================================

async function startBaseline(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const session =
            await mlControlService
                .startBaseline(
                    deviceId,
                    req.body
                        ?.operatorNote ||
                    ""
                );

        const io =
            ioFromRequest(req);

        if (io) {
            io.emit(
                "hydrosense:ml-baseline",
                {
                    deviceId,

                    action:
                        "STARTED",

                    session,

                    serverTimestamp:
                        new Date()
                            .toISOString()
                }
            );
        }

        return res.status(201).json({
            success:
                true,

            message:
                "Healthy baseline collection started",

            deviceId,

            data:
                session
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// BASELINE STOP
// ============================================================

async function stopBaseline(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const session =
            await mlControlService
                .stopBaseline(
                    deviceId
                );

        const io =
            ioFromRequest(req);

        if (io) {
            io.emit(
                "hydrosense:ml-baseline",
                {
                    deviceId,

                    action:
                        "STOPPED",

                    session,

                    serverTimestamp:
                        new Date()
                            .toISOString()
                }
            );
        }

        return res.status(200).json({
            success:
                true,

            message:
                "Baseline collection stopped",

            deviceId,

            data:
                session
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// BASELINE ABORT
// ============================================================

async function abortBaseline(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const session =
            await mlControlService
                .abortBaseline(
                    deviceId
                );

        const io =
            ioFromRequest(req);

        if (io) {
            io.emit(
                "hydrosense:ml-baseline",
                {
                    deviceId,

                    action:
                        "ABORTED",

                    session,

                    serverTimestamp:
                        new Date()
                            .toISOString()
                }
            );
        }

        return res.status(200).json({
            success:
                true,

            message:
                "Baseline collection aborted",

            deviceId,

            data:
                session
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// BASELINE AUDIT
// ============================================================

async function getBaselineAudit(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const audit =
            await mlControlService
                .analyzeBaseline(
                    deviceId
                );

        delete audit._rows;

        return res.status(200).json({
            success:
                true,

            deviceId,

            data:
                audit
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// START ISOLATION FOREST TRAINING
// ============================================================

async function trainIsolationForest(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const io =
            ioFromRequest(req);

        const job =
            await mlTrainingService
                .startTraining(
                    deviceId,
                    io
                );

        return res.status(202).json({
            success:
                true,

            message:
                "Isolation Forest training started",

            deviceId,

            data:
                job
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// TRAINING STATE
// ============================================================

async function getTrainingState(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const job =
            mlTrainingService
                .getTrainingState(
                    deviceId
                );

        return res.status(200).json({
            success:
                true,

            deviceId,

            data:
                job
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// CANCEL TRAINING
// ============================================================

async function cancelTraining(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const job =
            await mlTrainingService
                .cancelTraining(
                    deviceId
                );

        const io =
            ioFromRequest(req);

        if (io) {
            const payload = {
                deviceId,

                ...job,

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

        return res.status(200).json({
            success:
                true,

            message:
                "Training cancelled",

            deviceId,

            data:
                job
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// MODEL LIST
// ============================================================

async function getModels(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const models =
            await mlControlService
                .listModels(
                    deviceId
                );

        return res.status(200).json({
            success:
                true,

            deviceId,

            count:
                models.length,

            data:
                models
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// ACTIVATE MODEL
// ============================================================

async function activateModel(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const modelId =
            String(
                req.params
                    ?.modelId ||
                req.body
                    ?.modelId ||
                ""
            ).trim();

        if (!modelId) {
            const error =
                new Error(
                    "modelId is required"
                );

            error.statusCode =
                400;

            throw error;
        }

        const model =
            await mlControlService
                .activateModel(
                    deviceId,
                    modelId
                );

        mlRuntimeService
            .invalidateCaches(
                deviceId
            );

        const io =
            ioFromRequest(req);

        if (io) {
            io.emit(
                "hydrosense:ml-model",
                {
                    deviceId,

                    action:
                        "ACTIVATED",

                    model,

                    serverTimestamp:
                        new Date()
                            .toISOString()
                }
            );
        }

        return res.status(200).json({
            success:
                true,

            message:
                "Isolation Forest model activated",

            deviceId,

            data:
                model
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// START CONTROLLED EXPERIMENT
// ============================================================

async function startExperiment(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const label =
            String(
                req.body
                    ?.label ||
                ""
            ).trim();

        if (!label) {
            const error =
                new Error(
                    "Experiment label is required"
                );

            error.statusCode =
                400;

            throw error;
        }

        const experiment =
            await mlControlService
                .startExperiment(
                    deviceId,

                    label,

                    req.body
                        ?.operatorNote ||
                    ""
                );

        mlRuntimeService
            .invalidateCaches(
                deviceId
            );

        const io =
            ioFromRequest(req);

        if (io) {
            io.emit(
                "hydrosense:ml-experiment",
                {
                    deviceId,

                    action:
                        "STARTED",

                    experiment,

                    serverTimestamp:
                        new Date()
                            .toISOString()
                }
            );
        }

        return res.status(201).json({
            success:
                true,

            message:
                "Controlled experiment started",

            deviceId,

            data:
                experiment
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// STOP CONTROLLED EXPERIMENT
// ============================================================

async function stopExperiment(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const experiment =
            await mlControlService
                .stopExperiment(
                    deviceId
                );

        mlRuntimeService
            .invalidateCaches(
                deviceId
            );

        const io =
            ioFromRequest(req);

        if (io) {
            io.emit(
                "hydrosense:ml-experiment",
                {
                    deviceId,

                    action:
                        "STOPPED",

                    experiment,

                    serverTimestamp:
                        new Date()
                            .toISOString()
                }
            );
        }

        return res.status(200).json({
            success:
                true,

            message:
                "Controlled experiment stopped",

            deviceId,

            data:
                experiment
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// EXPERIMENT LIST
// ============================================================

async function getExperiments(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const experiments =
            await mlControlService
                .listExperiments(
                    deviceId
                );

        return res.status(200).json({
            success:
                true,

            deviceId,

            count:
                experiments.length,

            data:
                experiments
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// CALIBRATE PRODUCTION THRESHOLD
// ============================================================

async function calibrateThreshold(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const result =
            await mlControlService
                .calibrateThreshold(
                    deviceId
                );

        mlRuntimeService
            .invalidateCaches(
                deviceId
            );

        const io =
            ioFromRequest(req);

        if (io) {
            io.emit(
                "hydrosense:ml-threshold",
                {
                    deviceId,

                    model:
                        result.model,

                    calibration:
                        result.calibration,

                    serverTimestamp:
                        new Date()
                            .toISOString()
                }
            );
        }

        return res.status(200).json({
            success:
                true,

            message:
                "Isolation Forest threshold calibrated from labelled experiment data",

            deviceId,

            data:
                result
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// CURRENT REALTIME ML RESULT
// ============================================================

async function getRealtimeML(
    req,
    res,
    next
) {
    try {
        const deviceId =
            requireDeviceId(req);

        const data =
            mlRuntimeService
                .latest(
                    deviceId
                );

        if (!data) {
            return res.status(404).json({
                success:
                    false,

                message:
                    "No realtime ML state has been generated for this device since backend startup",

                deviceId
            });
        }

        return res.status(200).json({
            success:
                true,

            deviceId,

            data
        });
    } catch (error) {
        return next(error);
    }
}


// ============================================================
// EXPORT
// ============================================================

module.exports = {
    getStatus,

    saveConfiguration,

    startBaseline,
    stopBaseline,
    abortBaseline,
    getBaselineAudit,

    trainIsolationForest,
    getTrainingState,
    cancelTraining,

    getModels,
    activateModel,

    startExperiment,
    stopExperiment,
    getExperiments,

    calibrateThreshold,

    getRealtimeML
};