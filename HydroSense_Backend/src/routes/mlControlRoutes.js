const express = require("express");

const {
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
} = require("../controllers/mlControlController");


const router = express.Router();


// ============================================================
// COMPLETE ML CONTROL STATE
// ============================================================

router.get(
    "/status",
    getStatus
);


// ============================================================
// CONFIGURATION
// ============================================================

router.patch(
    "/config",
    saveConfiguration
);


// ============================================================
// HEALTHY BASELINE
// ============================================================

router.post(
    "/baseline/start",
    startBaseline
);

router.post(
    "/baseline/stop",
    stopBaseline
);

router.post(
    "/baseline/abort",
    abortBaseline
);

router.get(
    "/baseline/audit",
    getBaselineAudit
);


// ============================================================
// ISOLATION FOREST TRAINING
// ============================================================

router.post(
    "/train",
    trainIsolationForest
);

router.get(
    "/training",
    getTrainingState
);

router.post(
    "/training/cancel",
    cancelTraining
);


// ============================================================
// MODEL REGISTRY
// ============================================================

router.get(
    "/models",
    getModels
);

router.post(
    "/models/:modelId/activate",
    activateModel
);


// ============================================================
// CONTROLLED EXPERIMENTS
// ============================================================

router.post(
    "/experiments/start",
    startExperiment
);

router.post(
    "/experiments/stop",
    stopExperiment
);

router.get(
    "/experiments",
    getExperiments
);


// ============================================================
// LABELLED THRESHOLD CALIBRATION
// ============================================================

router.post(
    "/threshold/calibrate",
    calibrateThreshold
);


// ============================================================
// REALTIME SERVER ML STATE
// ============================================================

router.get(
    "/live",
    getRealtimeML
);


// ============================================================
// EXPORT
// ============================================================

module.exports = router;