#pragma once

/*
 * SPDX-FileCopyrightText: 2026 Rupam Mondal
 * SPDX-License-Identifier: MIT
 */

#include <Arduino.h>

#include "models/S3SensorSnapshot.h"
#include "models/S3FeatureData.h"
#include "models/S3TDSResult.h"
#include "models/S3AnomalyResult.h"
#include "models/S3SensorTrustResult.h"

// ============================================================
// HYDROSENSE ESP32-S3 CLOUD UPLOADER
//
// Historical path:
//   S3 -> /api/v1/telemetry -> MongoDB
//
// Realtime path:
//   S3 -> /api/v1/realtime -> Socket.IO -> Website
//
// Historical storage accepts only complete, semantically valid
// samples.
//
// Realtime telemetry reports unavailable numerical values as
// JSON null and never fabricates replacement measurements.
// ============================================================

class S3CloudUploader
{
public:
    S3CloudUploader();

    // Configure the historical endpoint and device identity.
    // The supplied strings must remain valid for the lifetime
    // of this object.
    bool begin(
        const char *serverUrl,
        const char *deviceId);

    // Configure the separate realtime endpoint.
    bool beginRealtime(
        const char *realtimeUrl);

    // Upload one complete validated historical observation.
    bool upload(
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
        uint32_t queueDrops);

    // Upload the current realtime state.
    // Invalid or unavailable values are transmitted as JSON null.
    // This endpoint does not write historical MongoDB data.
    bool uploadRealtime(
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
        uint32_t queueDrops);

    // Historical uploader configuration status.
    bool ready() const;

    // Results from the latest historical HTTP attempt.
    int lastHttpCode() const;

    uint32_t successCount() const;

    uint32_t failureCount() const;

private:
    const char *serverUrl_;

    const char *realtimeUrl_;

    const char *deviceId_;

    bool initialized_;

    int lastHttpCode_;

    uint32_t successCount_;

    uint32_t failureCount_;
};