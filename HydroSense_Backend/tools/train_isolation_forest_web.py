#!/usr/bin/env python3

import argparse
import json
import math
import sys
from pathlib import Path

import joblib
import numpy as np
from sklearn.ensemble import IsolationForest


# ============================================================
# PROGRESS OUTPUT
# Node.js reads these JSON lines in real time.
# ============================================================


def emit_progress(progress_pct, stage, message):
    payload = {
        "type": "progress",
        "progressPct": float(progress_pct),
        "stage": str(stage),
        "message": str(message),
    }

    print(
        json.dumps(payload),
        flush=True,
    )


# ============================================================
# BASIC VALIDATION
# ============================================================


def finite_number(value):
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(float(value))
    )


def require_number(value, name):
    if not finite_number(value):
        raise RuntimeError(f"{name} must be a finite number")

    return float(value)


def require_integer(value, name):
    if not isinstance(value, int) or isinstance(value, bool):
        raise RuntimeError(f"{name} must be an integer")

    return int(value)


# ============================================================
# COMMAND LINE
# ============================================================


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=("HydroSense website-controlled Isolation Forest trainer")
    )

    parser.add_argument(
        "--input",
        required=True,
        help="Training input JSON file",
    )

    parser.add_argument(
        "--output",
        required=True,
        help="Training result JSON file",
    )

    parser.add_argument(
        "--model",
        required=True,
        help="Output joblib model file",
    )

    return parser.parse_args()


# ============================================================
# LOAD INPUT
# ============================================================


def load_training_input(path):
    input_path = Path(path)

    if not input_path.exists():
        raise RuntimeError(f"Training input does not exist: {input_path}")

    try:
        with input_path.open(
            "r",
            encoding="utf-8",
        ) as file:
            payload = json.load(file)

    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Training input is not valid JSON: {exc}") from exc

    if not isinstance(payload, dict):
        raise RuntimeError("Training input root must be an object")

    return payload


# ============================================================
# BUILD REAL FEATURE MATRIX
# ============================================================


def build_matrix(payload):
    features = payload.get("features")
    samples = payload.get("samples")

    if not isinstance(features, list) or len(features) == 0:
        raise RuntimeError("No ML features were supplied")

    if not isinstance(samples, list) or len(samples) < 3:
        raise RuntimeError("At least 3 complete baseline samples are required")

    clean_feature_names = []

    for feature in features:
        if not isinstance(feature, str) or not feature.strip():
            raise RuntimeError("Every feature name must be a non-empty string")

        clean_feature_names.append(feature.strip())

    if len(set(clean_feature_names)) != len(clean_feature_names):
        raise RuntimeError("Duplicate feature names are not allowed")

    matrix = []
    timestamps = []
    sequences = []

    previous_timestamp = None

    for index, sample in enumerate(samples):
        if not isinstance(sample, dict):
            raise RuntimeError(f"Sample {index} is not an object")

        values = sample.get("values")

        if not isinstance(values, list) or len(values) != len(clean_feature_names):
            raise RuntimeError(
                f"Sample {index} feature count does not match model features"
            )

        vector = []

        for feature_index, value in enumerate(values):
            if not finite_number(value):
                raise RuntimeError(
                    (
                        f"Sample {index}, feature "
                        f"{clean_feature_names[feature_index]} "
                        "is missing or non-finite"
                    )
                )

            vector.append(float(value))

        timestamp_text = sample.get("timestamp")

        if not isinstance(timestamp_text, str) or not timestamp_text:
            raise RuntimeError(f"Sample {index} has no timestamp")

        # ISO timestamps sort correctly because Node supplies UTC ISO values.
        if previous_timestamp is not None and timestamp_text < previous_timestamp:
            raise RuntimeError("Baseline samples are not in chronological order")

        previous_timestamp = timestamp_text

        matrix.append(vector)
        timestamps.append(timestamp_text)
        sequences.append(sample.get("sequence"))

    array = np.asarray(
        matrix,
        dtype=np.float64,
    )

    if array.ndim != 2:
        raise RuntimeError("Training matrix must be two-dimensional")

    if array.shape[0] != len(samples):
        raise RuntimeError("Training matrix row count mismatch")

    if array.shape[1] != len(clean_feature_names):
        raise RuntimeError("Training matrix feature count mismatch")

    if not np.all(np.isfinite(array)):
        raise RuntimeError("Training matrix contains NaN or infinite values")

    return (
        clean_feature_names,
        array,
        timestamps,
        sequences,
    )


# ============================================================
# MODEL SETTINGS
# ============================================================


def read_model_settings(payload):
    settings = payload.get("isolationForest")

    if not isinstance(settings, dict):
        raise RuntimeError("Isolation Forest configuration is missing")

    n_estimators = require_integer(
        settings.get("nEstimators"),
        "nEstimators",
    )

    random_state = require_integer(
        settings.get("randomState"),
        "randomState",
    )

    train_fraction = require_number(
        settings.get("trainFraction"),
        "trainFraction",
    )

    if n_estimators < 1:
        raise RuntimeError("nEstimators must be at least 1")

    if not (0.0 < train_fraction < 1.0):
        raise RuntimeError("trainFraction must be between 0 and 1")

    return {
        "nEstimators": n_estimators,
        "trainFraction": train_fraction,
        "randomState": random_state,
    }


# ============================================================
# CHRONOLOGICAL SPLIT
# ============================================================


def chronological_split(
    matrix,
    train_fraction,
):
    total_samples = matrix.shape[0]

    if total_samples < 3:
        raise RuntimeError("At least 3 samples are required")

    split_index = int(math.floor(total_samples * train_fraction))

    # Need at least 2 training samples.
    split_index = max(
        2,
        split_index,
    )

    # Always preserve at least one validation sample.
    split_index = min(
        total_samples - 1,
        split_index,
    )

    train = matrix[:split_index]

    validation = matrix[split_index:]

    if train.shape[0] < 2:
        raise RuntimeError("Chronological training partition is too small")

    if validation.shape[0] < 1:
        raise RuntimeError("Chronological validation partition is empty")

    return (
        train,
        validation,
        split_index,
    )


# ============================================================
# TRAIN
# ============================================================


def train_model(
    x_train,
    settings,
):
    model = IsolationForest(
        n_estimators=settings["nEstimators"],
        max_samples="auto",
        # Used internally by sklearn.
        # HydroSense does NOT use sklearn's automatic
        # prediction threshold as the production alarm threshold.
        contamination="auto",
        random_state=settings["randomState"],
        n_jobs=-1,
    )

    model.fit(x_train)

    return model


# ============================================================
# MODEL BUNDLE
# ============================================================


def save_model(
    model_path,
    model,
    features,
    payload,
    settings,
    sample_count,
    train_count,
    validation_count,
):
    output_path = Path(model_path)

    output_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    bundle = {
        "schemaVersion": 1,
        "system": "HydroSense",
        "modelType": "ISOLATION_FOREST",
        "deviceId": payload.get("deviceId"),
        "versionLabel": payload.get("versionLabel"),
        "features": list(features),
        "model": model,
        "trainingConfiguration": {
            "nEstimators": settings["nEstimators"],
            "trainFraction": settings["trainFraction"],
            "randomState": settings["randomState"],
            "maxSamples": "auto",
            "contamination": "auto",
            "chronologicalSplit": True,
        },
        "sampleCounts": {
            "total": int(sample_count),
            "training": int(train_count),
            "validation": int(validation_count),
        },
        "productionThreshold": {
            "defined": False,
            "value": None,
            "reason": (
                "Production threshold must be calibrated "
                "from labelled healthy and fault experiments."
            ),
        },
        "scoreInterpretation": (
            "Higher decision_function values indicate greater "
            "consistency with the learned healthy baseline. "
            "The value is not a probability."
        ),
    }

    joblib.dump(
        bundle,
        output_path,
    )


# ============================================================
# WRITE RESULT
# ============================================================


def write_result(
    output_path,
    payload,
):
    path = Path(output_path)

    path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    temporary_path = path.parent / f"{path.name}.tmp"

    with temporary_path.open(
        "w",
        encoding="utf-8",
    ) as file:
        json.dump(
            payload,
            file,
            indent=2,
            allow_nan=False,
        )

    temporary_path.replace(path)


# ============================================================
# MAIN
# ============================================================


def main():
    args = parse_arguments()

    emit_progress(
        2,
        "LOADING_INPUT",
        "Loading baseline training job",
    )

    payload = load_training_input(args.input)

    emit_progress(
        8,
        "VALIDATING_INPUT",
        "Validating real baseline samples",
    )

    (
        features,
        matrix,
        timestamps,
        sequences,
    ) = build_matrix(payload)

    settings = read_model_settings(payload)

    emit_progress(
        15,
        "PREPARING_DATA",
        (
            f"Prepared {matrix.shape[0]} complete samples "
            f"with {matrix.shape[1]} features"
        ),
    )

    (
        x_train,
        x_validation,
        split_index,
    ) = chronological_split(
        matrix,
        settings["trainFraction"],
    )

    emit_progress(
        25,
        "SPLITTING_DATA",
        (
            f"Chronological split: "
            f"{x_train.shape[0]} train, "
            f"{x_validation.shape[0]} validation"
        ),
    )

    emit_progress(
        35,
        "TRAINING",
        "Training Isolation Forest",
    )

    model = train_model(
        x_train,
        settings,
    )

    emit_progress(
        70,
        "SCORING_TRAIN",
        "Scoring healthy training partition",
    )

    training_decision_scores = model.decision_function(x_train).astype(float).tolist()

    emit_progress(
        80,
        "SCORING_VALIDATION",
        "Scoring held-out healthy validation partition",
    )

    validation_decision_scores = (
        model.decision_function(x_validation).astype(float).tolist()
    )

    training_raw_scores = model.score_samples(x_train).astype(float).tolist()

    validation_raw_scores = model.score_samples(x_validation).astype(float).tolist()

    emit_progress(
        90,
        "SAVING_MODEL",
        "Saving Isolation Forest model",
    )

    save_model(
        args.model,
        model,
        features,
        payload,
        settings,
        matrix.shape[0],
        x_train.shape[0],
        x_validation.shape[0],
    )

    result = {
        "success": True,
        "schemaVersion": 1,
        "deviceId": payload.get("deviceId"),
        "versionLabel": payload.get("versionLabel"),
        "modelType": "ISOLATION_FOREST",
        "features": features,
        "sampleCounts": {
            "total": int(matrix.shape[0]),
            "training": int(x_train.shape[0]),
            "validation": int(x_validation.shape[0]),
        },
        "chronologicalSplit": {
            "enabled": True,
            "splitIndex": int(split_index),
        },
        "baselineFrom": timestamps[0] if timestamps else None,
        "baselineTo": timestamps[-1] if timestamps else None,
        "trainingScores": training_decision_scores,
        "validationScores": validation_decision_scores,
        "scoreSamples": {
            "training": training_raw_scores,
            "validation": validation_raw_scores,
        },
        "productionThreshold": {
            "defined": False,
            "value": None,
            "reason": (
                "No production threshold is generated from healthy baseline data alone."
            ),
        },
        "important": [
            "No missing values were fabricated.",
            "No interpolation was performed.",
            "No baseline outlier was automatically clipped.",
            "Training and validation were split chronologically.",
            "Isolation Forest decision score is not a probability.",
            "Production threshold remains uncalibrated.",
        ],
    }

    write_result(
        args.output,
        result,
    )

    emit_progress(
        100,
        "READY",
        (
            "Isolation Forest training complete. "
            "Production threshold remains uncalibrated."
        ),
    )


# ============================================================
# ENTRY POINT
# ============================================================

if __name__ == "__main__":
    try:
        main()

    except Exception as exc:
        print(
            (f"HydroSense Isolation Forest training failed: {exc}"),
            file=sys.stderr,
            flush=True,
        )

        sys.exit(1)
