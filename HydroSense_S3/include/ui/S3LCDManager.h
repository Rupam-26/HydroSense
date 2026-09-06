#pragma once

#include <Arduino.h>
#include <math.h>

// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#include "models/S3SensorSnapshot.h"
#include "models/S3FeatureData.h"
#include "models/S3TDSResult.h"
#include "models/S3CloudStatus.h"

// ============================================================
// HYDROSENSE ESP32-S3
// PACKET-DRIVEN / FLICKER-REDUCED PROFESSIONAL LCD UI
//
// PAGE 1 : REAL SENSOR DATA
// PAGE 2 : EDGE AI DATA
// PAGE 3 : DERIVED PHYSICS DATA
//
// BOOT / GPIO0:
// single press -> next page
//
// IMPORTANT:
// - Static UI is drawn only when the page changes.
// - Genuine C3 sensor/AI/derived values redraw only when a new
//   CRC-valid C3 snapshot arrives or availability changes.
// - B/W/C/M status redraws only when its state changes.
//   B = BLE, W = Wi-Fi, C = realtime cloud, M = MongoDB.
// - Footer age/status may refresh independently.
// - No smoothing, interpolation or fabricated values.
// ============================================================

class S3LCDManager
{
public:
    bool begin();

    bool ready() const;

    void updateControls(
        uint32_t nowMs);

    // Backward-compatible renderer.
    void render(
        const S3SensorSnapshot &snapshot,
        const S3FeatureData &features,
        const S3TDSResult &tdsResult,
        bool haveSensorData,
        bool bleConnected,
        bool wifiConnected,
        bool tinyMLReady,
        uint32_t validPacketCount,
        uint32_t crcFailures,
        uint32_t packetAgeMs);

    // Final cloud-aware renderer.
    void render(
        const S3SensorSnapshot &snapshot,
        const S3FeatureData &features,
        const S3TDSResult &tdsResult,
        bool haveSensorData,
        bool bleConnected,
        bool wifiConnected,
        const S3CloudRuntimeStatus &cloudStatus,
        bool tinyMLReady,
        uint32_t validPacketCount,
        uint32_t crcFailures,
        uint32_t packetAgeMs);

private:
    // ========================================================
    // CORE STATE
    // ========================================================

    bool initialized_ =
        false;

    uint8_t currentPage_ =
        0;

    bool pageDirty_ =
        true;

    // ========================================================
    // BOOT BUTTON
    // ========================================================

    bool lastButtonLevel_ =
        HIGH;

    uint32_t lastAcceptedPressMs_ =
        0;

    // ========================================================
    // LEGACY REFRESH FIELD
    // ========================================================

    uint32_t lastValueRefreshMs_ =
        0;

    // ========================================================
    // C3 SOURCE FRAME TRACKING
    // ========================================================

    bool renderedSourceStateValid_ =
        false;

    uint8_t lastRenderedSequence_ =
        0;

    uint32_t lastRenderedReceivedAtMs_ =
        0;

    bool lastRenderedHaveSensorData_ =
        false;

    // ========================================================
    // HEADER STATUS TRACKING
    // ========================================================

    bool renderedHeaderStateValid_ =
        false;

    bool lastRenderedBleConnected_ =
        false;

    bool lastRenderedWifiConnected_ =
        false;

    S3CloudChannelState lastRenderedRealtimeState_ =
        S3CloudChannelState::NotConfigured;

    S3CloudChannelState lastRenderedHistoricalState_ =
        S3CloudChannelState::NotConfigured;

    // ========================================================
    // TINYML STATUS TRACKING
    // ========================================================

    bool renderedTinyMLStateValid_ =
        false;

    bool lastRenderedTinyMLReady_ =
        false;

    // ========================================================
    // FOOTER TRACKING
    // ========================================================

    bool renderedFooterStateValid_ =
        false;

    uint32_t lastFooterRefreshMs_ =
        0;

    uint32_t lastRenderedValidPacketCount_ =
        0;

    uint32_t lastRenderedCrcFailures_ =
        0;

    uint32_t lastRenderedPacketAgeMs_ =
        0;

    uint8_t lastRenderedFooterSequence_ =
        0;

    float lastRenderedQualityPct_ =
        NAN;

    // ========================================================
    // PAGE TRANSITION
    // ========================================================

    bool transitionActive_ =
        false;

    uint32_t transitionStartMs_ =
        0;

    int16_t previousTransitionWidth_ =
        0;

    // ========================================================
    // PACKET / STATE CHANGE HELPERS
    // ========================================================

    bool sourceFrameChanged(
        const S3SensorSnapshot &snapshot,
        bool haveSensorData) const;

    bool headerStateChanged(
        bool bleConnected,
        bool wifiConnected) const;

    bool headerStateChanged(
        bool bleConnected,
        bool wifiConnected,
        const S3CloudRuntimeStatus &cloudStatus) const;

    bool tinyMLStateChanged(
        bool tinyMLReady) const;

    bool footerStateChanged(
        const S3SensorSnapshot &snapshot,
        uint32_t validPacketCount,
        uint32_t crcFailures,
        uint32_t packetAgeMs,
        uint32_t nowMs) const;

    void rememberSourceFrame(
        const S3SensorSnapshot &snapshot,
        bool haveSensorData);

    void rememberHeaderState(
        bool bleConnected,
        bool wifiConnected);

    void rememberHeaderState(
        bool bleConnected,
        bool wifiConnected,
        const S3CloudRuntimeStatus &cloudStatus);

    void rememberTinyMLState(
        bool tinyMLReady);

    void rememberFooterState(
        const S3SensorSnapshot &snapshot,
        uint32_t validPacketCount,
        uint32_t crcFailures,
        uint32_t packetAgeMs,
        uint32_t nowMs);

    // ========================================================
    // STATIC PAGE DRAWING
    // ========================================================

    void drawPageStatic(
        const char *pageTitle,
        uint16_t pageAccent);

    void drawRealSensorStatic();

    void drawAIStatic();

    void drawDerivedStatic();

    // ========================================================
    // DYNAMIC PAGE DRAWING
    // ========================================================

    void updateRealSensorValues(
        const S3SensorSnapshot &snapshot,
        bool haveSensorData);

    void updateAIValues(
        const S3SensorSnapshot &snapshot,
        const S3TDSResult &tdsResult,
        bool tinyMLReady);

    void updateDerivedValues(
        const S3FeatureData &features);

    // ========================================================
    // HEADER + FOOTER
    // ========================================================

    void drawHeaderStatic(
        const char *pageTitle);

    void updateHeaderStatus(
        bool bleConnected,
        bool wifiConnected);

    void updateHeaderStatus(
        bool bleConnected,
        bool wifiConnected,
        const S3CloudRuntimeStatus &cloudStatus);

    void drawFooterStatic();

    void updateFooter(
        const S3SensorSnapshot &snapshot,
        uint32_t validPacketCount,
        uint32_t crcFailures,
        uint32_t packetAgeMs);

    // ========================================================
    // CARD HELPERS
    // ========================================================

    void drawCard(
        int16_t x,
        const char *title,
        uint16_t accentColor);

    void drawMetricLabel(
        int16_t cardX,
        int16_t labelY,
        const char *label);

    void drawFloatValue(
        int16_t cardX,
        int16_t labelY,
        float value,
        bool valid,
        uint16_t valueColor,
        uint8_t decimals);

    void drawIntegerValue(
        int16_t cardX,
        int16_t labelY,
        uint32_t value,
        uint16_t valueColor);

    void drawStateValue(
        int16_t cardX,
        int16_t labelY,
        const char *state,
        uint16_t stateColor);

    void clearValueArea(
        int16_t cardX,
        int16_t labelY);

    void drawCenteredText(
        int16_t x,
        int16_t width,
        int16_t y,
        const char *text,
        uint8_t textSize,
        uint16_t color);

    // ========================================================
    // SUBTLE ANIMATION
    // ========================================================

    void updateTransition(
        uint32_t nowMs);
};