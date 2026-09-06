// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <math.h>
#include <stdint.h>

// ============================================================
// HYDROSENSE ESP32-S3
// TDS SOFT-SENSOR RESULT MODEL
//
// Estimated TDS is a TinyML-generated estimate.
// It is not a direct physical TDS measurement.
// ============================================================

enum class S3TDSStatus : uint8_t
{
    NotReady = 0,
    InputUnavailable,
    InputSaturated,
    InvokeFailed,
    InvalidOutput,
    Valid
};

struct S3TDSResult
{
    float estimatedTdsPpm = NAN;
    float normalizedOutput = NAN;

    uint32_t inferenceMicros = 0;
    uint8_t sourceSequence = 0;

    S3TDSStatus status = S3TDSStatus::NotReady;

    bool inputSaturated = false;

    bool valid() const
    {
        return status == S3TDSStatus::Valid &&
               isfinite(estimatedTdsPpm);
    }

    void clear()
    {
        estimatedTdsPpm = NAN;
        normalizedOutput = NAN;

        inferenceMicros = 0;
        sourceSequence = 0;

        status = S3TDSStatus::NotReady;
        inputSaturated = false;
    }
};