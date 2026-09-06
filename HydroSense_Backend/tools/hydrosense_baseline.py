import argparse
import json
import math
import os
from datetime import datetime, timezone
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from dotenv import load_dotenv
from pymongo import MongoClient
from sklearn.ensemble import IsolationForest


# ============================================================
# HYDROSENSE HEALTHY BASELINE + ISOLATION FOREST
#
# MongoDB Atlas
#      ↓
# validated historical telemetry
#      ↓
# healthy baseline CSV
#      ↓
# chronological train/validation split
#      ↓
# Isolation Forest
#
# IMPORTANT:
# - No fake values
# - No interpolation
# - No hard-coded anomaly threshold
# - Isolation Forest output is NOT a probability
# ============================================================


RAW_FEATURES = [
    "airTemperatureC",
    "humidityPct",
    "waterTemperatureC",
    "stableDistanceCm",
    "waterLevelPct",
]


DERIVED_FEATURES = [
    "dewPointC",
    "saturationVaporPressureKPa",
    "actualVaporPressureKPa",
    "vpdKPa",
    "absoluteHumidityGm3",
    "airWaterDeltaC",
    "absoluteAirWaterDeltaC",
]


TIMESTAMP_CANDIDATES = [
    "serverTimestamp",
    "timestamp",
    "sourceTimestamp",
    "createdAt",
    "receivedAt",
    "updatedAt",
]


# ============================================================
# ARGUMENTS
# ============================================================


def get_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("--device-id", default="HydroSense_S3_01")

    parser.add_argument("--from-time", default=None)

    parser.add_argument("--to-time", default=None)

    parser.add_argument("--accept-all", action="store_true")

    parser.add_argument("--train", action="store_true")

    parser.add_argument("--db", default=None)

    parser.add_argument("--collection", default=None)

    parser.add_argument("--output", default="hydrosense_ml_output")

    return parser.parse_args()


# ============================================================
# HELPERS
# ============================================================


def is_finite(value):
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(float(value))
    )


def parse_time(value):
    if value is None:
        return None

    value = value.strip()

    if value.endswith("Z"):
        value = value[:-1] + "+00:00"

    dt = datetime.fromisoformat(value)

    if dt.tzinfo is None:
        raise ValueError("Timestamp must contain timezone, for example +05:30")

    return dt.astimezone(timezone.utc)


def find_mongo_uri():
    load_dotenv()

    possible_names = [
        "MONGODB_URI",
        "MONGO_URI",
        "MONGO_URL",
        "DATABASE_URL",
    ]

    for name in possible_names:
        value = os.getenv(name)

        if value:
            return value

    raise RuntimeError(
        "MongoDB URI not found in .env.\nExpected MONGODB_URI or MONGO_URI."
    )


def looks_like_hydrosense_telemetry(document):
    return (
        isinstance(document, dict)
        and isinstance(document.get("deviceId"), str)
        and isinstance(document.get("measured"), dict)
        and isinstance(document.get("derived"), dict)
        and isinstance(document.get("estimated"), dict)
        and isinstance(document.get("quality"), dict)
    )


# ============================================================
# DATABASE DISCOVERY
# ============================================================


def select_database(client, requested_db):

    if requested_db:
        return client[requested_db]

    try:
        default_db = client.get_default_database()

        if default_db is not None:
            return default_db

    except Exception:
        pass

    names = [
        name
        for name in client.list_database_names()
        if name not in ["admin", "config", "local"]
    ]

    if len(names) == 1:
        return client[names[0]]

    print("\nAvailable databases:")

    for name in names:
        print(" -", name)

    raise RuntimeError(
        "\nCould not automatically select database.\n"
        "Run again using:\n"
        "--db YOUR_DATABASE_NAME"
    )


def select_collection(database, device_id, requested_collection):

    if requested_collection:
        return database[requested_collection]

    matches = []

    for collection_name in database.list_collection_names():
        collection = database[collection_name]

        sample = collection.find_one({"deviceId": device_id})

        if sample is not None and looks_like_hydrosense_telemetry(sample):
            matches.append(collection_name)

    if len(matches) == 1:
        return database[matches[0]]

    if len(matches) == 0:
        print("\nCollections:")

        for name in database.list_collection_names():
            print(" -", name)

        raise RuntimeError(
            "\nCould not automatically locate HydroSense telemetry collection."
        )

    print("\nPossible telemetry collections:")

    for name in matches:
        print(" -", name)

    raise RuntimeError(
        "\nMore than one telemetry collection found.\n"
        "Run again using:\n"
        "--collection COLLECTION_NAME"
    )


# ============================================================
# TIMESTAMP
# ============================================================


def detect_timestamp_field(document):

    for field in TIMESTAMP_CANDIDATES:
        value = document.get(field)

        if isinstance(value, datetime):
            return field

        if isinstance(value, str):
            try:
                parse_time(value)
                return field

            except Exception:
                pass

    object_id = document.get("_id")

    if hasattr(object_id, "generation_time"):
        return "_id"

    raise RuntimeError("No usable timestamp field found.")


def document_time(document, timestamp_field):

    if timestamp_field == "_id":
        return document["_id"].generation_time.astimezone(timezone.utc)

    value = document.get(timestamp_field)

    if isinstance(value, datetime):
        if value.tzinfo is None:
            return value.replace(tzinfo=timezone.utc)

        return value.astimezone(timezone.utc)

    return parse_time(value)


# ============================================================
# FLATTEN MONGODB DOCUMENT
# ============================================================


def flatten_record(document, timestamp):

    measured = document.get("measured", {})

    derived = document.get("derived", {})

    estimated = document.get("estimated", {})

    quality = document.get("quality", {})

    return {
        "timestampUtc": timestamp.isoformat(),
        "deviceId": document.get("deviceId"),
        "sequence": document.get("sequence"),
        # ----------------------------------------------------
        # REAL SENSOR VALUES
        # ----------------------------------------------------
        "airTemperatureC": measured.get("airTemperatureC"),
        "humidityPct": measured.get("humidityPct"),
        "waterTemperatureC": measured.get("waterTemperatureC"),
        "stableDistanceCm": measured.get("stableDistanceCm"),
        "waterLevelPct": measured.get("waterLevelPct"),
        # ----------------------------------------------------
        # DERIVED PHYSICS
        # ----------------------------------------------------
        "dewPointC": derived.get("dewPointC"),
        "saturationVaporPressureKPa": derived.get("saturationVaporPressureKPa"),
        "actualVaporPressureKPa": derived.get("actualVaporPressureKPa"),
        "vpdKPa": derived.get("vpdKPa"),
        "absoluteHumidityGm3": derived.get("absoluteHumidityGm3"),
        "airWaterDeltaC": derived.get("airWaterDeltaC"),
        "absoluteAirWaterDeltaC": derived.get("absoluteAirWaterDeltaC"),
        # ----------------------------------------------------
        # TINYML
        # ----------------------------------------------------
        "tdsPpm": estimated.get("tdsPpm"),
        "tdsModel": estimated.get("model"),
        "tdsStatus": estimated.get("status"),
        "inferenceMicros": estimated.get("inferenceMicros"),
        # ----------------------------------------------------
        # DATA QUALITY
        # ----------------------------------------------------
        "qualityScorePct": quality.get("scorePct"),
        "qualityFlags": quality.get("flags"),
        "crcFailures": quality.get("crcFailures"),
        "wrongLengths": quality.get("wrongLengths"),
        "badHeaders": quality.get("badHeaders"),
        "badVersions": quality.get("badVersions"),
        "queueDrops": quality.get("queueDrops"),
        "packetAgeMs": document.get("packetAgeMs"),
        "bleConnected": document.get("bleConnected"),
        "wifiConnected": document.get("wifiConnected"),
    }


# ============================================================
# CLEAN BASELINE
# ============================================================


def create_clean_baseline(dataframe):

    required = (
        RAW_FEATURES
        + DERIVED_FEATURES
        + [
            "tdsPpm",
            "qualityScorePct",
            "packetAgeMs",
        ]
    )

    for column in required:
        dataframe[column] = pd.to_numeric(dataframe[column], errors="coerce")

    valid_mask = np.ones(len(dataframe), dtype=bool)

    for column in required:
        valid_mask &= (
            dataframe[column]
            .map(lambda value: pd.notna(value) and math.isfinite(float(value)))
            .to_numpy()
        )

    clean = dataframe[valid_mask].copy()

    # --------------------------------------------------------
    # Only connected historical samples
    # --------------------------------------------------------

    if "bleConnected" in clean.columns:
        clean = clean[clean["bleConnected"] == True]

    if "wifiConnected" in clean.columns:
        clean = clean[clean["wifiConnected"] == True]

    clean = clean.sort_values("timestampUtc")

    clean.reset_index(drop=True, inplace=True)

    return clean


# ============================================================
# AUDIT
# ============================================================


def create_audit(dataframe, device_id, database_name, collection_name):

    times = pd.to_datetime(dataframe["timestampUtc"], utc=True)

    deltas = times.diff().dt.total_seconds().dropna()

    audit = {
        "deviceId": device_id,
        "database": database_name,
        "collection": collection_name,
        "records": int(len(dataframe)),
        "startUtc": (times.min().isoformat() if len(times) else None),
        "endUtc": (times.max().isoformat() if len(times) else None),
        "durationHours": (
            float((times.max() - times.min()).total_seconds() / 3600.0)
            if len(times) > 1
            else 0.0
        ),
        "samplingIntervalSeconds": {
            "median": (float(deltas.median()) if len(deltas) else None),
            "minimum": (float(deltas.min()) if len(deltas) else None),
            "maximum": (float(deltas.max()) if len(deltas) else None),
        },
        "features": {},
    }

    for feature in RAW_FEATURES + DERIVED_FEATURES + ["tdsPpm", "qualityScorePct"]:
        values = pd.to_numeric(dataframe[feature], errors="coerce")

        audit["features"][feature] = {
            "minimum": float(values.min()),
            "maximum": float(values.max()),
            "mean": float(values.mean()),
            "standardDeviation": (float(values.std()) if len(values) > 1 else 0.0),
        }

    return audit


# ============================================================
# ISOLATION FOREST
# ============================================================


def train_isolation_forest(dataframe, output_directory):

    print()
    print("============================================")
    print("TRAINING ISOLATION FOREST")
    print("============================================")

    # --------------------------------------------------------
    # Use direct physical measurements first.
    #
    # Derived features are deterministic transformations of
    # these sensors and would duplicate information.
    # --------------------------------------------------------

    features = RAW_FEATURES

    X = dataframe[features].astype(float)

    if len(X) < 10:
        print("[WARNING] Very little healthy data available.")

        print("[WARNING] Model will be created, but do not treat it as research-ready.")

    # --------------------------------------------------------
    # Chronological split
    # --------------------------------------------------------

    split_index = int(len(X) * 0.80)

    split_index = max(1, min(split_index, len(X) - 1))

    X_train = X.iloc[:split_index]

    X_validation = X.iloc[split_index:]

    # --------------------------------------------------------
    # Model
    #
    # contamination="auto"
    #
    # IMPORTANT:
    # This does NOT create our final HydroSense threshold.
    # --------------------------------------------------------

    model = IsolationForest(
        n_estimators=300,
        max_samples="auto",
        contamination="auto",
        random_state=42,
        n_jobs=-1,
    )

    model.fit(X_train)

    train_scores = model.decision_function(X_train)

    validation_scores = model.decision_function(X_validation)

    model_file = output_directory / "hydrosense_isolation_forest.joblib"

    joblib.dump(
        {
            "model": model,
            "features": features,
            "interpretation": (
                "Higher score = more consistent "
                "with learned healthy baseline. "
                "Lower score = more isolated. "
                "NOT a probability."
            ),
        },
        model_file,
    )

    score_dataframe = pd.DataFrame(
        {
            "timestampUtc": dataframe["timestampUtc"],
            "partition": (
                ["train"] * len(X_train) + ["validation"] * len(X_validation)
            ),
            "isolationScore": np.concatenate([train_scores, validation_scores]),
        }
    )

    score_dataframe.to_csv(
        output_directory / "healthy_isolation_scores.csv", index=False
    )

    metadata = {
        "model": "IsolationForest",
        "features": features,
        "trainingSamples": int(len(X_train)),
        "validationSamples": int(len(X_validation)),
        "configuration": {
            "nEstimators": 300,
            "contamination": "auto",
            "randomState": 42,
        },
        "healthyScoreDistribution": {
            "training": {
                "minimum": float(np.min(train_scores)),
                "maximum": float(np.max(train_scores)),
                "mean": float(np.mean(train_scores)),
                "median": float(np.median(train_scores)),
            },
            "validation": {
                "minimum": float(np.min(validation_scores)),
                "maximum": float(np.max(validation_scores)),
                "mean": float(np.mean(validation_scores)),
                "median": float(np.median(validation_scores)),
            },
        },
        "productionThresholdDefined": False,
        "important": (
            "Isolation Forest score is not a probability. "
            "A final fault threshold must be determined "
            "using controlled labelled fault experiments."
        ),
    }

    with open(
        output_directory / "isolation_forest_metadata.json", "w", encoding="utf-8"
    ) as file:
        json.dump(metadata, file, indent=4)

    print("[OK] Isolation Forest trained")

    print("[OK] Model:", model_file)

    print("[IMPORTANT] No final anomaly threshold has been created.")


# ============================================================
# MAIN
# ============================================================


def main():

    args = get_args()

    output_directory = Path(args.output)

    output_directory.mkdir(parents=True, exist_ok=True)

    # --------------------------------------------------------
    # HEALTHY INTERVAL
    # --------------------------------------------------------

    if not args.accept_all and (args.from_time is None or args.to_time is None):
        raise RuntimeError(
            "\nYou must either:\n\n"
            "1. specify a healthy interval:\n"
            "--from-time ... --to-time ...\n\n"
            "OR\n\n"
            "2. explicitly say all existing records "
            "are healthy:\n"
            "--accept-all"
        )

    start_time = parse_time(args.from_time)

    end_time = parse_time(args.to_time)

    # --------------------------------------------------------
    # CONNECT MONGODB
    # --------------------------------------------------------

    uri = find_mongo_uri()

    print()
    print("Connecting to MongoDB Atlas...")

    client = MongoClient(uri, serverSelectionTimeoutMS=10000)

    client.admin.command("ping")

    print("[OK] MongoDB connected")

    database = select_database(client, args.db)

    collection = select_collection(database, args.device_id, args.collection)

    print("[OK] Database:", database.name)

    print("[OK] Collection:", collection.name)

    # --------------------------------------------------------
    # LOAD RECORDS
    # --------------------------------------------------------

    documents = list(collection.find({"deviceId": args.device_id}))

    if not documents:
        raise RuntimeError("No telemetry found for device.")

    timestamp_field = detect_timestamp_field(documents[0])

    print("[OK] Timestamp field:", timestamp_field)

    selected_records = []

    for document in documents:
        try:
            timestamp = document_time(document, timestamp_field)

        except Exception:
            continue

        if start_time is not None and timestamp < start_time:
            continue

        if end_time is not None and timestamp > end_time:
            continue

        selected_records.append((timestamp, document))

    selected_records.sort(key=lambda item: item[0])

    print("[OK] Selected records:", len(selected_records))

    # --------------------------------------------------------
    # DATAFRAME
    # --------------------------------------------------------

    rows = [
        flatten_record(document, timestamp)
        for (timestamp, document) in selected_records
    ]

    dataframe = pd.DataFrame(rows)

    clean = create_clean_baseline(dataframe)

    if clean.empty:
        raise RuntimeError("No complete healthy baseline rows remain.")

    print("[OK] Clean healthy records:", len(clean))

    # --------------------------------------------------------
    # SAVE FULL DATASET
    # --------------------------------------------------------

    full_csv = output_directory / "healthy_baseline_full.csv"

    clean.to_csv(full_csv, index=False)

    # --------------------------------------------------------
    # SAVE RAW SENSOR DATASET
    # --------------------------------------------------------

    clean[["timestampUtc", "deviceId"] + RAW_FEATURES].to_csv(
        output_directory / "healthy_baseline_raw5.csv", index=False
    )

    # --------------------------------------------------------
    # AUDIT
    # --------------------------------------------------------

    audit = create_audit(clean, args.device_id, database.name, collection.name)

    audit["healthySelection"] = {
        "acceptAll": args.accept_all,
        "fromUtc": (start_time.isoformat() if start_time else None),
        "toUtc": (end_time.isoformat() if end_time else None),
    }

    audit["important"] = [
        "No missing values were fabricated.",
        "No interpolation was performed.",
        "No outlier clipping was performed.",
        "Healthy label came from operator selection.",
        "Isolation Forest score is not a probability.",
    ]

    audit_file = output_directory / "baseline_audit.json"

    with open(audit_file, "w", encoding="utf-8") as file:
        json.dump(audit, file, indent=4)

    print()
    print("============================================")

    print("HEALTHY BASELINE READY")

    print("============================================")

    print("Records:", len(clean))

    print("CSV:", full_csv)

    print("Audit:", audit_file)

    # --------------------------------------------------------
    # OPTIONAL TRAINING
    # --------------------------------------------------------

    if args.train:
        train_isolation_forest(clean, output_directory)

    else:
        print()
        print("Training not started.")

        print("Check baseline_audit.json first.")

        print("Then run again with --train.")


if __name__ == "__main__":
    main()
