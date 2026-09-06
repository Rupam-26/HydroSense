#!/usr/bin/env python3

import argparse
import json
import math
import sys
from pathlib import Path

import joblib
import numpy as np


# ============================================================
# BASIC HELPERS
# ============================================================


def finite_number(value):
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(float(value))
    )


def write_json(payload):
    print(
        json.dumps(
            payload,
            allow_nan=False,
        ),
        flush=True,
    )


# ============================================================
# LOAD MODEL BUNDLE
# ============================================================


def load_bundle(model_path):
    path = Path(model_path)

    if not path.exists():
        raise RuntimeError(f"Model file does not exist: {path}")

    bundle = joblib.load(path)

    if not isinstance(bundle, dict):
        raise RuntimeError("Model artifact is not a HydroSense model bundle")

    if bundle.get("modelType") != "ISOLATION_FOREST":
        raise RuntimeError("Unsupported model type")

    model = bundle.get("model")
    features = bundle.get("features")

    if model is None:
        raise RuntimeError("Isolation Forest model object is missing")

    if not isinstance(features, list) or not features:
        raise RuntimeError("Model feature list is missing")

    return bundle


# ============================================================
# SCORE ONE SAMPLE
# ============================================================


def score_sample(bundle, request):
    features = bundle["features"]

    values = request.get("values")

    if not isinstance(values, dict):
        return {
            "success": False,
            "scorable": False,
            "reason": "values must be an object",
        }

    vector = []

    missing = []

    for feature in features:
        value = values.get(feature)

        if not finite_number(value):
            missing.append(feature)
            continue

        vector.append(float(value))

    if missing:
        return {
            "success": True,
            "scorable": False,
            "missingFeatures": missing,
            "reason": ("One or more required model features are missing or non-finite"),
        }

    array = np.asarray(
        [vector],
        dtype=np.float64,
    )

    model = bundle["model"]

    decision_function = float(model.decision_function(array)[0])

    score_samples = float(model.score_samples(array)[0])

    return {
        "success": True,
        "scorable": True,
        "deviceId": bundle.get("deviceId"),
        "versionLabel": bundle.get("versionLabel"),
        "features": features,
        "decisionFunction": decision_function,
        "scoreSamples": score_samples,
        "interpretation": (
            "Higher decision_function values are more "
            "consistent with the learned healthy baseline. "
            "This score is not a probability."
        ),
    }


# ============================================================
# HANDLE REQUEST
# ============================================================


def handle_request(bundle, request):
    if not isinstance(request, dict):
        return {
            "success": False,
            "error": "Request must be a JSON object",
        }

    command = request.get("command")

    if command == "ping":
        return {
            "success": True,
            "status": "READY",
            "deviceId": bundle.get("deviceId"),
            "versionLabel": bundle.get("versionLabel"),
            "features": bundle.get("features"),
        }

    if command == "score":
        return score_sample(
            bundle,
            request,
        )

    if command == "shutdown":
        return {
            "success": True,
            "shutdown": True,
        }

    return {
        "success": False,
        "error": ("Unsupported command. Expected ping, score, or shutdown."),
    }


# ============================================================
# COMMAND LINE
# ============================================================


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=("HydroSense realtime Isolation Forest worker")
    )

    parser.add_argument(
        "--model",
        required=True,
        help="Path to HydroSense joblib model bundle",
    )

    return parser.parse_args()


# ============================================================
# MAIN LOOP
# ============================================================


def main():
    args = parse_arguments()

    bundle = load_bundle(args.model)

    write_json(
        {
            "type": "ready",
            "success": True,
            "deviceId": bundle.get("deviceId"),
            "versionLabel": bundle.get("versionLabel"),
            "features": bundle.get("features"),
        }
    )

    for raw_line in sys.stdin:
        line = raw_line.strip()

        if not line:
            continue

        try:
            request = json.loads(line)

            result = handle_request(
                bundle,
                request,
            )

            write_json(result)

            if result.get("shutdown") is True:
                break

        except Exception as exc:
            write_json(
                {
                    "success": False,
                    "error": str(exc),
                }
            )


# ============================================================
# ENTRY POINT
# ============================================================

if __name__ == "__main__":
    try:
        main()

    except Exception as exc:
        write_json(
            {
                "type": "fatal",
                "success": False,
                "error": str(exc),
            }
        )

        sys.exit(1)
