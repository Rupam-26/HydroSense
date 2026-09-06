// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>

#include "models/S3SensorSnapshot.h"
#include "models/S3TDSResult.h"

// ============================================================
// HYDROSENSE ESP32-S3
// FP32 TDS SOFT-SENSOR INFERENCE INTERFACE
//
// Model input order:
// 0. Air temperature
// 1. Air humidity
// 2. Water temperature
//
// Estimated TDS is not a direct physical TDS measurement.
// ============================================================

class S3TinyMLTDS
{
public:
    bool begin();

    bool ready() const;

    S3TDSResult predict(
        const S3SensorSnapshot &snapshot);

    static const char *statusText(
        S3TDSStatus status);

private:
    bool initialized_ = false;
};