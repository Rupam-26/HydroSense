/*
 * SPDX-FileCopyrightText: 2026 Rupam Mondal
 * SPDX-License-Identifier: MIT
 */

#include "cloud/S3CloudUploader.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <cmath>

// ============================================================
// HYDROSENSE S3 CLOUD UPLOADER
//
// HISTORICAL:
//     S3
//      -> /api/v1/telemetry
//      -> validated complete sample
//      -> MongoDB Atlas
//
// REAL-TIME:
//     S3
//      -> /api/v1/realtime
//      -> Socket.IO
//      -> Website
//
// IMPORTANT:
//
// Historical storage rejects incomplete/invalid samples.
//
// Real-time telemetry NEVER fabricates a value.
// An unavailable value is transmitted as JSON null.
//
// Example:
//
//     "waterTemperatureC": null,
//     "waterTemperatureValid": false
//
// ============================================================

// ============================================================
// JSON HELPERS
// ============================================================

// ------------------------------------------------------------
// Append normal floating-point number.
//
// If the number itself is NaN/Infinity, use null.
// ------------------------------------------------------------

static void appendJsonNumber(
    String &json,
    const char *name,
    float value,
    uint8_t decimals,
    bool comma = true)
{
    json += "\"";
    json += name;
    json += "\":";

    if (
        std::isfinite(
            value))
    {
        json +=
            String(
                value,
                static_cast<unsigned int>(
                    decimals));
    }
    else
    {
        json +=
            "null";
    }

    if (
        comma)
    {
        json += ",";
    }
}

// ------------------------------------------------------------
// Append sensor/derived value according to VALIDITY.
//
// This is critical for the real-time website.
//
// A stale finite value MUST NOT be displayed as valid merely
// because it is still numerically present in memory.
//
// valid == true + finite:
//
//     "temperature":31.42
//
// valid == false:
//
//     "temperature":null
//
// ------------------------------------------------------------

static void appendJsonNullableNumber(
    String &json,
    const char *name,
    float value,
    bool valid,
    uint8_t decimals,
    bool comma = true)
{
    json += "\"";
    json += name;
    json += "\":";

    if (
        valid &&
        std::isfinite(
            value))
    {
        json +=
            String(
                value,
                static_cast<unsigned int>(
                    decimals));
    }
    else
    {
        json +=
            "null";
    }

    if (
        comma)
    {
        json += ",";
    }
}

// ------------------------------------------------------------
// Append JSON boolean.
// ------------------------------------------------------------

static void appendJsonBool(
    String &json,
    const char *name,
    bool value,
    bool comma = true)
{
    json += "\"";
    json += name;
    json += "\":";

    json +=
        value
            ? "true"
            : "false";

    if (
        comma)
    {
        json += ",";
    }
}

// ------------------------------------------------------------
// Append a JSON string with basic escaping.
// ------------------------------------------------------------
static void appendJsonString(
    String &json,
    const char *name,
    const char *value,
    bool comma = true)
{
    json += "\"";
    json += name;
    json += "\":\"";

    const char *cursor =
        value != nullptr
            ? value
            : "";

    while (*cursor != '\0')
    {
        const char character =
            *cursor++;

        switch (character)
        {
        case '\\':
            json += "\\\\";
            break;

        case '"':
            json += "\\\"";
            break;

        case '\n':
            json += "\\n";
            break;

        case '\r':
            json += "\\r";
            break;

        case '\t':
            json += "\\t";
            break;

        default:
            json += character;
            break;
        }
    }

    json += "\"";

    if (comma)
    {
        json += ",";
    }
}

static const char *trustStateText(
    S3SensorTrustState state)
{
    switch (state)
    {
    case S3SensorTrustState::MeasurementUnavailable:
        return "MEASUREMENT UNAVAILABLE";

    case S3SensorTrustState::LearningNormalBehaviour:
        return "LEARNING NORMAL SENSOR BEHAVIOUR";

    case S3SensorTrustState::Trusted:
        return "MEASUREMENT CURRENTLY TRUSTED";

    case S3SensorTrustState::Monitor:
        return "TRUST REDUCED - MONITOR SENSOR";

    case S3SensorTrustState::LowTrust:
        return "LOW SENSOR TRUST - INSPECTION RECOMMENDED";

    case S3SensorTrustState::Unreliable:
        return "MEASUREMENT NOT RELIABLE FOR OPERATIONAL DECISIONS";

    case S3SensorTrustState::PartialSensorCoverage:
        return "PARTIAL SENSOR COVERAGE - ONE OR MORE MEASUREMENTS UNAVAILABLE";

    default:
        return "UNKNOWN SENSOR TRUST STATE";
    }
}

static const char *trustReasonText(
    S3SensorTrustReason reason)
{
    switch (reason)
    {
    case S3SensorTrustReason::None:
        return "No significant reliability problem is currently supported by the available evidence.";

    case S3SensorTrustReason::MeasurementUnavailable:
        return "The sensor did not provide a currently usable measurement.";

    case S3SensorTrustReason::LearningNormalBehaviour:
        return "HydroSense is still learning this sensor's normal behaviour, so the trust assessment is provisional.";

    case S3SensorTrustReason::LowDataQuality:
        return "The upstream data-quality assessment is low, so confidence in this measurement is reduced.";

    case S3SensorTrustReason::CommunicationInstability:
        return "Recent packet-integrity, sequence, queue, or reconnect evidence indicates an unstable C3-to-S3 measurement path.";

    case S3SensorTrustReason::PossibleStuckOutput:
        return "The sensor output remained unusually unchanged while a meaningful external change indicated that a response should have occurred.";

    case S3SensorTrustReason::ExcessiveMeasurementNoise:
        return "Short-term measurement variability is persistently much higher than the learned healthy behaviour.";

    case S3SensorTrustReason::ChangedResponseDynamics:
        return "The sensor is responding differently from its learned healthy dynamic behaviour.";

    case S3SensorTrustReason::SuddenMeasurementChange:
        return "A sudden measurement change was observed; one transient alone is not treated as sensor failure.";

    case S3SensorTrustReason::PossibleResponseDegradation:
        return "The submerged water-temperature probe shows possible response degradation, but the evidence is not yet strong enough for a higher-severity conclusion.";

    case S3SensorTrustReason::FoulingCompatibleResponseDegradation:
        return "Repeated comparable thermal events show response degradation compatible with probe coating, biological fouling, contamination, or ageing; direct algae identification is not claimed.";

    default:
        return "No human-readable reason is available.";
    }
}

static const char *sensorConditionText(
    S3SensorCondition condition)
{
    switch (condition)
    {
    case S3SensorCondition::MeasurementUnavailable:
        return "MEASUREMENT UNAVAILABLE";

    case S3SensorCondition::LearningBaseline:
        return "LEARNING NORMAL SENSOR BEHAVIOUR";

    case S3SensorCondition::Normal:
        return "NORMAL SENSOR BEHAVIOUR";

    case S3SensorCondition::Monitor:
        return "UNUSUAL SENSOR BEHAVIOUR - MONITOR";

    case S3SensorCondition::Warning:
        return "PERSISTENT SENSOR RELIABILITY WARNING";

    default:
        return "UNKNOWN SENSOR CONDITION";
    }
}

static const char *surfaceFoulingStateText(
    S3SurfaceFoulingState state)
{
    switch (state)
    {
    case S3SurfaceFoulingState::LearningCleanResponse:
        return "LEARNING CLEAN WATER-PROBE RESPONSE";

    case S3SurfaceFoulingState::AssessmentNotPossible:
        return "FOULING ASSESSMENT PAUSED - SENSOR SIGNAL UNRELIABLE";

    case S3SurfaceFoulingState::InsufficientThermalEvidence:
        return "NOT ENOUGH COMPARABLE THERMAL EVENTS YET";

    case S3SurfaceFoulingState::LowEvidence:
        return "NO SIGNIFICANT FOULING-COMPATIBLE DEGRADATION";

    case S3SurfaceFoulingState::PossibleResponseDegradation:
        return "POSSIBLE WATER-PROBE RESPONSE DEGRADATION - MONITOR";

    case S3SurfaceFoulingState::FoulingCompatibleDegradation:
        return "FOULING-COMPATIBLE RESPONSE DEGRADATION - INSPECT PROBE";

    default:
        return "UNKNOWN SURFACE-FOULING ASSESSMENT";
    }
}

static void appendTrustComponent(
    String &json,
    const char *name,
    const S3TrustComponent &component,
    bool comma = true)
{
    appendJsonNullableNumber(
        json,
        name,
        component.scorePct,
        component.available,
        1,
        comma);
}

static void appendSensorTrustAnalysis(
    String &json,
    const char *name,
    const S3SensorTrustAnalysis &analysis,
    bool comma = true)
{
    json += "\"";
    json += name;
    json += "\":{";

    appendJsonBool(
        json,
        "measurementAvailable",
        analysis.measurementAvailable);

    appendJsonBool(
        json,
        "baselineReady",
        analysis.baselineReady);

    appendJsonNullableNumber(
        json,
        "trustScorePct",
        analysis.trustScorePct,
        analysis.measurementAvailable,
        1);

    appendJsonNullableNumber(
        json,
        "evidenceCoveragePct",
        analysis.evidenceCoveragePct,
        analysis.measurementAvailable,
        1);

    appendJsonString(
        json,
        "state",
        trustStateText(
            analysis.state));

    appendJsonString(
        json,
        "primaryReason",
        trustReasonText(
            analysis.primaryReason));

    json += "\"components\":{";

    appendTrustComponent(
        json,
        "measurementIntegrityPct",
        analysis.measurementIntegrity);

    appendTrustComponent(
        json,
        "dataQualityPct",
        analysis.dataQuality);

    appendTrustComponent(
        json,
        "temporalReliabilityPct",
        analysis.temporalReliability);

    appendTrustComponent(
        json,
        "learnedBehaviourConsistencyPct",
        analysis.learnedBehaviourConsistency);

    appendTrustComponent(
        json,
        "communicationIntegrityPct",
        analysis.communicationIntegrity);

    appendTrustComponent(
        json,
        "domainSpecificDegradationIntegrityPct",
        analysis.domainSpecificDegradation,
        false);

    json += "},";

    appendJsonBool(
        json,
        "domainSpecificEvidenceApplied",
        analysis.domainSpecificEvidenceApplied,
        false);

    json += "}";

    if (comma)
    {
        json += ",";
    }
}

static void appendSensorAnomalyAnalysis(
    String &json,
    const char *name,
    const S3SensorAnomalyAnalysis &analysis,
    bool comma = true)
{
    json += "\"";
    json += name;
    json += "\":{";

    appendJsonBool(
        json,
        "measurementAvailable",
        analysis.available);

    appendJsonBool(
        json,
        "baselineReady",
        analysis.baselineReady);

    appendJsonNullableNumber(
        json,
        "anomalyEvidenceIndexPct",
        analysis.anomalyScorePct,
        analysis.available && analysis.baselineReady,
        1);

    appendJsonString(
        json,
        "condition",
        sensorConditionText(
            analysis.condition));

    json += "\"evidence\":{";

    appendJsonBool(
        json,
        "possibleStuckOutput",
        analysis.stuckOutputEvidence);

    appendJsonBool(
        json,
        "excessiveMeasurementNoise",
        analysis.excessiveNoiseEvidence);

    appendJsonBool(
        json,
        "suddenMeasurementChange",
        analysis.rapidChangeEvidence);

    appendJsonBool(
        json,
        "changedResponseDynamics",
        analysis.changedDynamicsEvidence);

    appendJsonBool(
        json,
        "suppressedResponseDynamics",
        analysis.suppressedDynamicsEvidence);

    appendJsonBool(
        json,
        "amplifiedResponseDynamics",
        analysis.amplifiedDynamicsEvidence,
        false);

    json += "}";
    json += "}";

    if (comma)
    {
        json += ",";
    }
}

// ============================================================
// CONSTRUCTOR
// ============================================================

S3CloudUploader::S3CloudUploader()
    : serverUrl_(
          nullptr),

      realtimeUrl_(
          nullptr),

      deviceId_(
          nullptr),

      initialized_(
          false),

      lastHttpCode_(
          0),

      successCount_(
          0),

      failureCount_(
          0)
{
}

// ============================================================
// HISTORICAL CLOUD INITIALIZATION
// ============================================================

bool S3CloudUploader::begin(
    const char *serverUrl,
    const char *deviceId)
{
    if (
        serverUrl == nullptr ||
        deviceId == nullptr)
    {
        initialized_ =
            false;

        return false;
    }

    serverUrl_ =
        serverUrl;

    deviceId_ =
        deviceId;

    initialized_ =
        true;

    Serial.println();

    Serial.println(
        "[CLOUD] Uploader initialized");

    Serial.print(
        "[CLOUD] Historical endpoint : ");

    Serial.println(
        serverUrl_);

    Serial.print(
        "[CLOUD] Device              : ");

    Serial.println(
        deviceId_);

    return true;
}

// ============================================================
// REAL-TIME CLOUD INITIALIZATION
// ============================================================

bool S3CloudUploader::beginRealtime(
    const char *realtimeUrl)
{
    if (
        realtimeUrl == nullptr)
    {
        return false;
    }

    realtimeUrl_ =
        realtimeUrl;

    Serial.print(
        "[REALTIME] Endpoint          : ");

    Serial.println(
        realtimeUrl_);

    return true;
}

// ============================================================
// STATUS
// ============================================================

bool S3CloudUploader::ready() const
{
    return initialized_;
}

int S3CloudUploader::lastHttpCode() const
{
    return lastHttpCode_;
}

uint32_t S3CloudUploader::successCount() const
{
    return successCount_;
}

uint32_t S3CloudUploader::failureCount() const
{
    return failureCount_;
}

// ============================================================
// HISTORICAL / MONGODB UPLOAD
//
// POLICY:
//
// Store only a complete scientifically valid observation.
//
// If ANY required sensor is invalid:
//     DO NOT STORE.
//
// If ANY required derived feature is invalid:
//     DO NOT STORE.
//
// If TDS inference is invalid:
//     DO NOT STORE.
//
// No fake zeroes.
// No NaN.
// No Infinity.
// ============================================================

bool S3CloudUploader::upload(
    const S3SensorSnapshot &snapshot,
    const S3FeatureData &features,
    const S3TDSResult &tdsResult,
    uint32_t packetAgeMs,
    bool bleConnected,
    bool wifiConnected,
    uint32_t crcFailures,
    uint32_t wrongLengths,
    uint32_t badHeaders,
    uint32_t badVersions,
    uint32_t queueDrops)
{
    // ========================================================
    // INITIALIZATION
    // ========================================================

    if (
        !initialized_ ||
        serverUrl_ == nullptr ||
        deviceId_ == nullptr)
    {
        Serial.println(
            "[CLOUD] Historical uploader not initialized");

        failureCount_++;

        return false;
    }

    // ========================================================
    // WIFI
    // ========================================================

    if (
        WiFi.status() !=
        WL_CONNECTED)
    {
        Serial.println(
            "[CLOUD] Historical upload unavailable: WiFi offline");

        failureCount_++;

        return false;
    }

    // ========================================================
    // PACKET VALIDITY
    // ========================================================

    if (
        !snapshot.packetValid)
    {
        Serial.println(
            "[CLOUD] Historical sample skipped: packet invalid");

        return false;
    }

    // ========================================================
    // INDIVIDUAL SENSOR VALIDITY
    //
    // A single failed sensor prevents MongoDB storage.
    // ========================================================

    if (
        !snapshot.airEnvironmentAvailable())
    {
        Serial.println(
            "[CLOUD] Historical sample skipped: DHT22 data unavailable");

        return false;
    }

    if (
        !snapshot.waterTemperatureAvailable())
    {
        Serial.println(
            "[CLOUD] Historical sample skipped: DS18B20 data unavailable");

        return false;
    }

    if (
        !snapshot.distanceAvailable())
    {
        Serial.println(
            "[CLOUD] Historical sample skipped: ultrasonic data unavailable");

        return false;
    }

    if (
        !snapshot.waterLevelAvailable())
    {
        Serial.println(
            "[CLOUD] Historical sample skipped: reservoir data unavailable");

        return false;
    }

    // ========================================================
    // FEATURE VALIDITY
    // ========================================================

    if (
        !features.valid ||
        !features.environmentalFeaturesValid ||
        !features.thermalFeaturesValid)
    {
        Serial.println(
            "[CLOUD] Historical sample skipped: derived features invalid");

        return false;
    }

    // ========================================================
    // TDS VALIDITY
    // ========================================================

    if (
        !tdsResult.valid())
    {
        Serial.println(
            "[CLOUD] Historical sample skipped: TDS invalid");

        return false;
    }

    // ========================================================
    // NUMERIC VALIDATION
    // ========================================================

    if (
        !std::isfinite(
            snapshot.airTemperatureC) ||

        !std::isfinite(
            snapshot.humidityPct) ||

        !std::isfinite(
            snapshot.waterTemperatureC) ||

        !std::isfinite(
            snapshot.stableDistanceCm) ||

        !std::isfinite(
            snapshot.waterLevelPct) ||

        !std::isfinite(
            features.dewPointC) ||

        !std::isfinite(
            features.saturationVaporPressureKPa) ||

        !std::isfinite(
            features.actualVaporPressureKPa) ||

        !std::isfinite(
            features.vpdKPa) ||

        !std::isfinite(
            features.absoluteHumidityGm3) ||

        !std::isfinite(
            features.airWaterDeltaC) ||

        !std::isfinite(
            features.absoluteAirWaterDeltaC) ||

        !std::isfinite(
            tdsResult.estimatedTdsPpm))
    {
        Serial.println(
            "[CLOUD] Historical sample skipped: non-finite value");

        return false;
    }

    // ========================================================
    // BUILD HISTORICAL JSON
    // ========================================================

    String payload;

    payload.reserve(
        1500);

    payload +=
        "{";

    // ========================================================
    // DEVICE ID
    // ========================================================

    appendJsonString(
        payload,
        "deviceId",
        deviceId_);

    // ========================================================
    // SEQUENCE
    // ========================================================

    payload +=
        "\"sequence\":";

    payload +=
        String(
            snapshot.sequence);

    payload +=
        ",";

    // ========================================================
    // MEASURED SENSOR DATA
    // ========================================================

    payload +=
        "\"measured\":{";

    appendJsonNumber(
        payload,
        "airTemperatureC",
        snapshot.airTemperatureC,
        2);

    appendJsonNumber(
        payload,
        "humidityPct",
        snapshot.humidityPct,
        2);

    appendJsonNumber(
        payload,
        "waterTemperatureC",
        snapshot.waterTemperatureC,
        2);

    appendJsonNumber(
        payload,
        "stableDistanceCm",
        snapshot.stableDistanceCm,
        2);

    appendJsonNumber(
        payload,
        "waterLevelPct",
        snapshot.waterLevelPct,
        2,
        false);

    payload +=
        "},";

    // ========================================================
    // DERIVED FEATURES
    // ========================================================

    payload +=
        "\"derived\":{";

    appendJsonNumber(
        payload,
        "dewPointC",
        features.dewPointC,
        3);

    appendJsonNumber(
        payload,
        "saturationVaporPressureKPa",
        features.saturationVaporPressureKPa,
        4);

    appendJsonNumber(
        payload,
        "actualVaporPressureKPa",
        features.actualVaporPressureKPa,
        4);

    appendJsonNumber(
        payload,
        "vpdKPa",
        features.vpdKPa,
        4);

    appendJsonNumber(
        payload,
        "absoluteHumidityGm3",
        features.absoluteHumidityGm3,
        3);

    appendJsonNumber(
        payload,
        "airWaterDeltaC",
        features.airWaterDeltaC,
        3);

    appendJsonNumber(
        payload,
        "absoluteAirWaterDeltaC",
        features.absoluteAirWaterDeltaC,
        3,
        false);

    payload +=
        "},";

    // ========================================================
    // AI / ESTIMATED DATA
    // ========================================================

    payload +=
        "\"estimated\":{";

    appendJsonNumber(
        payload,
        "tdsPpm",
        tdsResult.estimatedTdsPpm,
        2);

    payload +=
        "\"model\":\"Tiny_32_16_8_FP32\",";

    payload +=
        "\"status\":\"VALID\",";

    payload +=
        "\"inferenceMicros\":";

    payload +=
        String(
            tdsResult.inferenceMicros);

    payload +=
        "},";

    // ========================================================
    // QUALITY
    // ========================================================

    payload +=
        "\"quality\":{";

    appendJsonNumber(
        payload,
        "scorePct",
        snapshot.qualityScorePct,
        0);

    payload +=
        "\"flags\":";

    payload +=
        String(
            snapshot.qualityFlags);

    payload +=
        ",";

    payload +=
        "\"crcFailures\":";

    payload +=
        String(
            crcFailures);

    payload +=
        ",";

    payload +=
        "\"wrongLengths\":";

    payload +=
        String(
            wrongLengths);

    payload +=
        ",";

    payload +=
        "\"badHeaders\":";

    payload +=
        String(
            badHeaders);

    payload +=
        ",";

    payload +=
        "\"badVersions\":";

    payload +=
        String(
            badVersions);

    payload +=
        ",";

    payload +=
        "\"queueDrops\":";

    payload +=
        String(
            queueDrops);

    payload +=
        "},";

    // ========================================================
    // COMMUNICATION STATE
    // ========================================================

    payload +=
        "\"packetAgeMs\":";

    payload +=
        String(
            packetAgeMs);

    payload +=
        ",";

    appendJsonBool(
        payload,
        "bleConnected",
        bleConnected);

    appendJsonBool(
        payload,
        "wifiConnected",
        wifiConnected,
        false);

    payload +=
        "}";

    // ========================================================
    // HTTP POST
    // ========================================================

    WiFiClient client;

    HTTPClient http;

    if (
        !http.begin(
            client,
            serverUrl_))
    {
        Serial.println(
            "[CLOUD] Historical HTTP initialization failed");

        failureCount_++;

        return false;
    }

    http.setTimeout(
        1500);

    http.addHeader(
        "Content-Type",
        "application/json");

    Serial.println();

    Serial.println(
        "[CLOUD] Sending historical telemetry...");

    lastHttpCode_ =
        http.POST(
            payload);

    // ========================================================
    // SUCCESS
    // ========================================================

    if (
        lastHttpCode_ >= 200 &&
        lastHttpCode_ < 300)
    {
        successCount_++;

        Serial.print(
            "[CLOUD] SUCCESS | HTTP ");

        Serial.println(
            lastHttpCode_);

        http.end();

        return true;
    }

    // ========================================================
    // FAILURE
    // ========================================================

    failureCount_++;

    Serial.print(
        "[CLOUD] FAILED | HTTP ");

    Serial.println(
        lastHttpCode_);

    if (
        lastHttpCode_ > 0)
    {
        const String response =
            http.getString();

        Serial.print(
            "[CLOUD] Server response: ");

        Serial.println(
            response);
    }

    http.end();

    return false;
}

// ============================================================
// REAL-TIME WEBSITE UPLOAD
//
// IMPORTANT:
//
// Every sensor is handled independently.
//
// Sensor invalid:
//     value -> null
//     sensorValid -> false
//
// Other healthy sensors continue transmitting normally.
//
// This endpoint does NOT write to MongoDB.
// ============================================================

bool S3CloudUploader::uploadRealtime(
    const S3SensorSnapshot &snapshot,
    const S3FeatureData &features,
    const S3TDSResult &tdsResult,
    const S3AnomalyResult &anomalyResult,
    const S3SensorTrustResult &sensorTrustResult,
    uint32_t packetAgeMs,
    bool bleConnected,
    bool wifiConnected,
    uint32_t crcFailures,
    uint32_t wrongLengths,
    uint32_t badHeaders,
    uint32_t badVersions,
    uint32_t queueDrops)
{
    // ========================================================
    // REAL-TIME ENDPOINT
    // ========================================================

    if (
        realtimeUrl_ == nullptr)
    {
        return false;
    }

    // ========================================================
    // WIFI
    //
    // If S3 itself has no WiFi connection it obviously cannot
    // reach the backend until WiFi reconnects.
    // ========================================================

    if (
        WiFi.status() !=
        WL_CONNECTED)
    {
        return false;
    }

    // ========================================================
    // BASE PACKET STATE
    // ========================================================

    const bool packetValid =
        snapshot.packetValid;

    // ========================================================
    // DHT22
    //
    // DHT22 provides:
    //   - Air temperature
    //   - Relative humidity
    // ========================================================

    const bool dht22Valid =
        snapshot.airEnvironmentAvailable();

    const bool airTemperatureValid =
        dht22Valid;

    const bool humidityValid =
        dht22Valid;

    // ========================================================
    // DS18B20
    // ========================================================

    const bool ds18b20Valid =
        snapshot.waterTemperatureAvailable();

    const bool waterTemperatureValid =
        ds18b20Valid;

    // ========================================================
    // HC-SR04
    // ========================================================

    const bool ultrasonicValid =
        snapshot.distanceAvailable();

    const bool stableDistanceValid =
        ultrasonicValid;

    // ========================================================
    // RESERVOIR STATE
    // ========================================================

    const bool calibrationValid =
        packetValid &&
        snapshot.calibrationValid;

    const bool reservoirValid =
        snapshot.waterLevelAvailable();

    const bool waterLevelValid =
        reservoirValid;

    // ========================================================
    // ENVIRONMENTAL DERIVED FEATURES
    // ========================================================

    const bool environmentalFeaturesValid =
        features.valid &&
        features.environmentalFeaturesValid &&

        std::isfinite(
            features.dewPointC) &&

        std::isfinite(
            features.saturationVaporPressureKPa) &&

        std::isfinite(
            features.actualVaporPressureKPa) &&

        std::isfinite(
            features.vpdKPa) &&

        std::isfinite(
            features.absoluteHumidityGm3);

    // ========================================================
    // THERMAL DERIVED FEATURES
    // ========================================================

    const bool thermalFeaturesValid =
        features.valid &&
        features.thermalFeaturesValid &&

        std::isfinite(
            features.airWaterDeltaC) &&

        std::isfinite(
            features.absoluteAirWaterDeltaC);

    // ========================================================
    // COMPLETE DERIVED STATE
    // ========================================================

    const bool derivedValid =
        environmentalFeaturesValid &&
        thermalFeaturesValid;

    // ========================================================
    // TDS AI
    // ========================================================

    const bool tdsValid =
        tdsResult.valid() &&
        std::isfinite(
            tdsResult.estimatedTdsPpm);

    // ========================================================
    // OVERALL LIVE SYSTEM STATUS
    //
    // ANY critical failure makes overall status INVALID.
    //
    // This DOES NOT hide healthy individual sensor values.
    // ========================================================

    const bool overallValid =
        packetValid &&
        bleConnected &&
        wifiConnected &&
        dht22Valid &&
        ds18b20Valid &&
        ultrasonicValid &&
        reservoirValid &&
        calibrationValid &&
        derivedValid &&
        tdsValid;

    // ========================================================
    // BUILD REAL-TIME JSON
    // ========================================================

    String payload;

    // The realtime frame now also carries edge anomaly and cross-layer
    // Sensor Trust evidence. Reserve once to avoid repeated heap growth.
    payload.reserve(
        7200);

    payload +=
        "{";

    // ========================================================
    // DEVICE ID
    // ========================================================

    appendJsonString(
        payload,
        "deviceId",
        deviceId_ != nullptr
            ? deviceId_
            : "HydroSense_S3");

    // ========================================================
    // SEQUENCE
    // ========================================================

    payload +=
        "\"sequence\":";

    payload +=
        String(
            snapshot.sequence);

    payload +=
        ",";

    // ========================================================
    // OVERALL STATUS
    // ========================================================

    payload +=
        "\"status\":\"";

    payload +=
        overallValid
            ? "VALID"
            : "INVALID";

    payload +=
        "\",";

    // ========================================================
    // SENSOR / SYSTEM HEALTH
    //
    // WEBSITE CAN READ THESE DIRECTLY.
    // ========================================================

    payload +=
        "\"health\":{";

    appendJsonBool(
        payload,
        "packetValid",
        packetValid);

    appendJsonBool(
        payload,
        "dht22Valid",
        dht22Valid);

    appendJsonBool(
        payload,
        "ds18b20Valid",
        ds18b20Valid);

    appendJsonBool(
        payload,
        "ultrasonicValid",
        ultrasonicValid);

    appendJsonBool(
        payload,
        "reservoirValid",
        reservoirValid);

    appendJsonBool(
        payload,
        "calibrationValid",
        calibrationValid);

    appendJsonBool(
        payload,
        "environmentalFeaturesValid",
        environmentalFeaturesValid);

    appendJsonBool(
        payload,
        "thermalFeaturesValid",
        thermalFeaturesValid);

    appendJsonBool(
        payload,
        "tdsValid",
        tdsValid);

    appendJsonBool(
        payload,
        "bleConnected",
        bleConnected);

    appendJsonBool(
        payload,
        "wifiConnected",
        wifiConnected,
        false);

    payload +=
        "},";

    // ========================================================
    // DIRECT SENSOR DATA
    // ========================================================

    payload +=
        "\"measured\":{";

    // --------------------------------------------------------
    // DHT22 AIR TEMPERATURE
    // --------------------------------------------------------

    appendJsonNullableNumber(
        payload,
        "airTemperatureC",
        snapshot.airTemperatureC,
        airTemperatureValid,
        2);

    appendJsonBool(
        payload,
        "airTemperatureValid",
        airTemperatureValid);

    // --------------------------------------------------------
    // DHT22 HUMIDITY
    // --------------------------------------------------------

    appendJsonNullableNumber(
        payload,
        "humidityPct",
        snapshot.humidityPct,
        humidityValid,
        2);

    appendJsonBool(
        payload,
        "humidityValid",
        humidityValid);

    // --------------------------------------------------------
    // DS18B20 WATER TEMPERATURE
    // --------------------------------------------------------

    appendJsonNullableNumber(
        payload,
        "waterTemperatureC",
        snapshot.waterTemperatureC,
        waterTemperatureValid,
        2);

    appendJsonBool(
        payload,
        "waterTemperatureValid",
        waterTemperatureValid);

    // --------------------------------------------------------
    // HC-SR04 STABLE DISTANCE
    // --------------------------------------------------------

    appendJsonNullableNumber(
        payload,
        "stableDistanceCm",
        snapshot.stableDistanceCm,
        stableDistanceValid,
        2);

    appendJsonBool(
        payload,
        "stableDistanceValid",
        stableDistanceValid);

    // --------------------------------------------------------
    // RESERVOIR WATER LEVEL
    // --------------------------------------------------------

    appendJsonNullableNumber(
        payload,
        "waterLevelPct",
        snapshot.waterLevelPct,
        waterLevelValid,
        2);

    appendJsonBool(
        payload,
        "waterLevelValid",
        waterLevelValid,
        false);

    payload +=
        "},";

    // ========================================================
    // DERIVED FEATURES
    // ========================================================

    payload +=
        "\"derived\":{";

    // --------------------------------------------------------
    // ENVIRONMENTAL FEATURES
    // --------------------------------------------------------

    appendJsonNullableNumber(
        payload,
        "dewPointC",
        features.dewPointC,
        environmentalFeaturesValid,
        3);

    appendJsonNullableNumber(
        payload,
        "saturationVaporPressureKPa",
        features.saturationVaporPressureKPa,
        environmentalFeaturesValid,
        4);

    appendJsonNullableNumber(
        payload,
        "actualVaporPressureKPa",
        features.actualVaporPressureKPa,
        environmentalFeaturesValid,
        4);

    appendJsonNullableNumber(
        payload,
        "vpdKPa",
        features.vpdKPa,
        environmentalFeaturesValid,
        4);

    appendJsonNullableNumber(
        payload,
        "absoluteHumidityGm3",
        features.absoluteHumidityGm3,
        environmentalFeaturesValid,
        3);

    // --------------------------------------------------------
    // THERMAL FEATURES
    // --------------------------------------------------------

    appendJsonNullableNumber(
        payload,
        "airWaterDeltaC",
        features.airWaterDeltaC,
        thermalFeaturesValid,
        3);

    appendJsonNullableNumber(
        payload,
        "absoluteAirWaterDeltaC",
        features.absoluteAirWaterDeltaC,
        thermalFeaturesValid,
        3);

    appendJsonBool(
        payload,
        "environmentalValid",
        environmentalFeaturesValid);

    appendJsonBool(
        payload,
        "thermalValid",
        thermalFeaturesValid);

    appendJsonBool(
        payload,
        "valid",
        derivedValid,
        false);

    payload +=
        "},";

    // ========================================================
    // AI / TDS
    // ========================================================

    payload +=
        "\"estimated\":{";

    appendJsonNullableNumber(
        payload,
        "tdsPpm",
        tdsResult.estimatedTdsPpm,
        tdsValid,
        2);

    payload +=
        "\"model\":\"Tiny_32_16_8_FP32\",";

    payload +=
        "\"status\":\"";

    payload +=
        tdsValid
            ? "VALID"
            : "INVALID";

    payload +=
        "\",";

    appendJsonBool(
        payload,
        "valid",
        tdsValid);

    payload +=
        "\"inferenceMicros\":";

    payload +=
        String(
            tdsResult.inferenceMicros);

    payload +=
        "},";

    // ========================================================
    // QUALITY / TRANSPORT
    // ========================================================

    payload +=
        "\"quality\":{";

    appendJsonNumber(
        payload,
        "scorePct",
        snapshot.qualityScorePct,
        0);

    payload +=
        "\"flags\":";

    payload +=
        String(
            snapshot.qualityFlags);

    payload +=
        ",";

    payload +=
        "\"crcFailures\":";

    payload +=
        String(
            crcFailures);

    payload +=
        ",";

    payload +=
        "\"wrongLengths\":";

    payload +=
        String(
            wrongLengths);

    payload +=
        ",";

    payload +=
        "\"badHeaders\":";

    payload +=
        String(
            badHeaders);

    payload +=
        ",";

    payload +=
        "\"badVersions\":";

    payload +=
        String(
            badVersions);

    payload +=
        ",";

    payload +=
        "\"queueDrops\":";

    payload +=
        String(
            queueDrops);

    payload +=
        "},";

    // ========================================================
    // CROSS-LAYER SENSOR TRUST
    //
    // This is a reliability assessment, not a probability of
    // correctness. Missing/inapplicable evidence remains null.
    // ========================================================

    payload +=
        "\"sensorTrust\":{";

    appendJsonBool(
        payload,
        "baselineReady",
        sensorTrustResult.baselineReady);

    appendJsonBool(
        payload,
        "allRequiredMeasurementsAvailable",
        sensorTrustResult.allRequiredMeasurementsAvailable);

    appendJsonNullableNumber(
        payload,
        "overallTrustScorePct",
        sensorTrustResult.overallTrustScorePct,
        packetValid,
        1);

    appendJsonNullableNumber(
        payload,
        "overallEvidenceCoveragePct",
        sensorTrustResult.overallEvidenceCoveragePct,
        packetValid,
        1);

    appendJsonString(
        payload,
        "overallState",
        packetValid
            ? trustStateText(
                  sensorTrustResult.overallState)
            : "MEASUREMENT UNAVAILABLE");

    appendJsonNullableNumber(
        payload,
        "communicationIntegrityPct",
        sensorTrustResult.communicationIntegrityPct,
        packetValid,
        1);

    payload +=
        "\"transportDiagnostics\":{";

    payload +=
        "\"transportFaultEventsObserved\":";
    payload +=
        String(sensorTrustResult.transportFaultEventsObserved);
    payload +=
        ",";

    payload +=
        "\"sequenceGapEventsObserved\":";
    payload +=
        String(sensorTrustResult.sequenceGapEventsObserved);
    payload +=
        ",";

    payload +=
        "\"reconnectEventsObserved\":";
    payload +=
        String(sensorTrustResult.reconnectEventsObserved);
    payload +=
        "},";

    appendSensorTrustAnalysis(
        payload,
        "airTemperature",
        sensorTrustResult.airTemperature);

    appendSensorTrustAnalysis(
        payload,
        "humidity",
        sensorTrustResult.humidity);

    appendSensorTrustAnalysis(
        payload,
        "waterTemperature",
        sensorTrustResult.waterTemperature);

    appendSensorTrustAnalysis(
        payload,
        "reservoirDistance",
        sensorTrustResult.reservoirDistance,
        false);

    payload +=
        "},";

    // ========================================================
    // EXPLAINABLE EDGE ANOMALY EVIDENCE
    // ========================================================

    payload +=
        "\"anomaly\":{";

    appendJsonBool(
        payload,
        "baselineReady",
        anomalyResult.generalBaselineReady);

    appendJsonNullableNumber(
        payload,
        "overallAnomalyEvidenceIndexPct",
        anomalyResult.overallSensorAnomalyScorePct,
        packetValid && anomalyResult.generalBaselineReady,
        1);

    appendJsonString(
        payload,
        "overallCondition",
        packetValid
            ? sensorConditionText(
                  anomalyResult.overallCondition)
            : "MEASUREMENT UNAVAILABLE");

    appendSensorAnomalyAnalysis(
        payload,
        "airTemperature",
        anomalyResult.airTemperature);

    appendSensorAnomalyAnalysis(
        payload,
        "humidity",
        anomalyResult.humidity);

    appendSensorAnomalyAnalysis(
        payload,
        "waterTemperature",
        anomalyResult.waterTemperature);

    appendSensorAnomalyAnalysis(
        payload,
        "reservoirDistance",
        anomalyResult.reservoirDistance);

    payload +=
        "\"surfaceFouling\":{";

    appendJsonBool(
        payload,
        "assessmentAvailable",
        anomalyResult.surfaceFoulingAssessmentAvailable);

    appendJsonString(
        payload,
        "state",
        surfaceFoulingStateText(
            anomalyResult.surfaceFoulingState));

    appendJsonNullableNumber(
        payload,
        "evidenceIndexPct",
        anomalyResult.surfaceFoulingRiskIndexPct,
        anomalyResult.surfaceFoulingAssessmentAvailable,
        1);

    appendJsonNullableNumber(
        payload,
        "assessmentConfidencePct",
        anomalyResult.surfaceFoulingAssessmentConfidencePct,
        anomalyResult.surfaceFoulingAssessmentAvailable,
        1);

    payload +=
        "\"cleanResponseEvents\":";
    payload +=
        String(anomalyResult.thermalResponseBaselineEvents);
    payload +=
        ",";

    payload +=
        "\"requiredCleanResponseEvents\":";
    payload +=
        String(anomalyResult.requiredThermalResponseBaselineEvents);
    payload +=
        ",";

    payload +=
        "\"comparableTestEvents\":";
    payload +=
        String(anomalyResult.thermalResponseAssessmentEvents);
    payload +=
        ",";

    payload +=
        "\"rejectedEvents\":";
    payload +=
        String(anomalyResult.rejectedThermalEvents);
    payload +=
        ",";

    appendJsonNullableNumber(
        payload,
        "cleanThermalResponseRatio",
        anomalyResult.cleanThermalResponseRatio,
        std::isfinite(anomalyResult.cleanThermalResponseRatio),
        3);

    appendJsonNullableNumber(
        payload,
        "currentThermalResponseRatio",
        anomalyResult.currentThermalResponseRatio,
        std::isfinite(anomalyResult.currentThermalResponseRatio),
        3);

    appendJsonNullableNumber(
        payload,
        "responseRelativeToCleanPct",
        anomalyResult.responseRatioRelativeToCleanPct,
        std::isfinite(anomalyResult.responseRatioRelativeToCleanPct),
        1);

    appendJsonBool(
        payload,
        "repeatedResponseAttenuation",
        anomalyResult.responseAttenuationEvidence);

    appendJsonBool(
        payload,
        "waterResponseSuppression",
        anomalyResult.waterResponseSuppressionEvidence);

    appendJsonBool(
        payload,
        "thermalRelationshipShift",
        anomalyResult.thermalRelationshipShiftEvidence);

    appendJsonBool(
        payload,
        "reservoirStableForAssessment",
        anomalyResult.reservoirThermallyStable);

    appendJsonBool(
        payload,
        "thermalExcitationStrongEnough",
        anomalyResult.thermalExcitationStrongEnough);

    appendJsonBool(
        payload,
        "currentEventComparableToBaseline",
        anomalyResult.thermalEventComparableToBaseline,
        false);

    payload +=
        "}";
    payload +=
        "},";

    // ========================================================
    // PACKET AGE
    // ========================================================

    payload +=
        "\"packetAgeMs\":";

    payload +=
        String(
            packetAgeMs);

    payload +=
        ",";

    // ========================================================
    // BLE STATE
    // ========================================================

    appendJsonBool(
        payload,
        "bleConnected",
        bleConnected);

    // ========================================================
    // WIFI STATE
    // ========================================================

    appendJsonBool(
        payload,
        "wifiConnected",
        wifiConnected,
        false);

    payload +=
        "}";

    // ========================================================
    // HTTP POST TO REAL-TIME BACKEND
    // ========================================================

    WiFiClient client;

    HTTPClient http;

    if (
        !http.begin(
            client,
            realtimeUrl_))
    {
        Serial.println(
            "[REALTIME] HTTP initialization failed");

        return false;
    }

    // Keep this shorter than historical upload so a temporary
    // backend issue does not block the S3 interface for long.

    http.setTimeout(
        1000);

    http.addHeader(
        "Content-Type",
        "application/json");

    const int realtimeHttpCode =
        http.POST(
            payload);

    const bool realtimeSuccess =
        realtimeHttpCode >= 200 &&
        realtimeHttpCode < 300;

    if (
        !realtimeSuccess)
    {
        Serial.print(
            "[REALTIME] HTTP failure: ");

        Serial.println(
            realtimeHttpCode);

        if (
            realtimeHttpCode > 0)
        {
            const String response =
                http.getString();

            Serial.print(
                "[REALTIME] Response: ");

            Serial.println(
                response);
        }
    }

    http.end();

    return realtimeSuccess;
}
