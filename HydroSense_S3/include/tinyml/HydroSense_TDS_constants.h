#pragma once

/*
 * SPDX-FileCopyrightText: 2026 Rupam Mondal
 * SPDX-License-Identifier: MIT
 */

// ============================================================
// ACTIVE FP32 MODEL PREPROCESSING
//
// Required input order:
//   0 = DHT temperature
//   1 = DHT humidity
//   2 = Water temperature
// ============================================================

constexpr float HS_X_MEAN[3] =
    {
        24.300770078f,
        71.575707048f,
        21.524219635f};

constexpr float HS_X_SCALE[3] =
    {
        1.056920341f,
        7.147115801f,
        2.019317271f};

constexpr float HS_TDS_MEAN =
    1154.108338698f;

constexpr float HS_TDS_SCALE =
    246.474719751f;

// ============================================================
// HISTORICAL INT8 REFERENCE ONLY
//
// These values are retained for model provenance.
// The active FP32 firmware does not use them.
// ============================================================

constexpr float HS_INPUT_QUANT_SCALE =
    0.031909208745f;

constexpr int HS_INPUT_ZERO_POINT =
    0;

constexpr float HS_OUTPUT_QUANT_SCALE =
    0.015512428246f;

constexpr int HS_OUTPUT_ZERO_POINT =
    11;