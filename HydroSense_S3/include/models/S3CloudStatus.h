#pragma once

#include <Arduino.h>

// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

enum class S3CloudChannelState : uint8_t
{
    NotConfigured = 0,
    WaitingForWiFi,
    WaitingForData,
    Online,
    RetryPending
};

struct S3CloudRuntimeStatus
{
    S3CloudChannelState realtime =
        S3CloudChannelState::NotConfigured;

    S3CloudChannelState historical =
        S3CloudChannelState::NotConfigured;

    bool realtimeFramePending =
        false;

    uint32_t realtimeAttempts =
        0;

    uint32_t realtimeSuccesses =
        0;

    uint32_t realtimeFailures =
        0;

    uint32_t historicalAttempts =
        0;

    uint32_t historicalSuccesses =
        0;

    uint32_t historicalFailures =
        0;

    uint32_t lastRealtimeAttemptMs =
        0;

    uint32_t lastRealtimeSuccessMs =
        0;

    uint32_t lastHistoricalAttemptMs =
        0;

    uint32_t lastHistoricalSuccessMs =
        0;

    void clear()
    {
        realtime =
            S3CloudChannelState::NotConfigured;

        historical =
            S3CloudChannelState::NotConfigured;

        realtimeFramePending =
            false;

        realtimeAttempts =
            0;

        realtimeSuccesses =
            0;

        realtimeFailures =
            0;

        historicalAttempts =
            0;

        historicalSuccesses =
            0;

        historicalFailures =
            0;

        lastRealtimeAttemptMs =
            0;

        lastRealtimeSuccessMs =
            0;

        lastHistoricalAttemptMs =
            0;

        lastHistoricalSuccessMs =
            0;
    }
};