// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>

#include "ui/OLEDManager.h"
#include "models/C3DisplayData.h"

// ============================================================
// HYDROSENSE ESP32-C3
// C3 DISPLAY CONTROLLER
//
// ACTIVE RUNTIME PAGES:
//
// 1. Dashboard / Sensors
// 2. Reservoir / Tank
// 3. Derived Environment
//
// One valid button press immediately advances to the next page.
// After page 3, the display returns to page 1.
//
// Page switching is immediate and deterministic.
// No slide transition is used.
//
// This controller does not read physical sensors directly.
// It renders an already-prepared C3DisplayData snapshot.
// ============================================================

enum class C3DisplayPage : uint8_t
{
    DASHBOARD = 0,
    RESERVOIR,
    ENVIRONMENT,

    COUNT
};

class C3DisplayController
{
public:
    explicit C3DisplayController(
        OLEDManager &oledManager);

    // Initialize the display controller on page 1.
    void begin(
        uint32_t nowMs);

    // Advance immediately to the next of the three runtime pages.
    //
    // Page sequence:
    // DASHBOARD -> RESERVOIR -> ENVIRONMENT -> DASHBOARD
    void nextPage(
        uint32_t nowMs);

    // Render the active page when its visible content changes.
    void update(
        const C3DisplayData &data,
        uint32_t nowMs);

    C3DisplayPage currentPage() const;

    // Human-readable page number in the range 1 to 3.
    uint8_t pageNumber() const;

private:
    OLEDManager &oled;

    C3DisplayPage activePage;

    uint32_t lastRenderMs;

    // Signature of the values and states that are actually visible on
    // the active page.
    //
    // Sensor snapshots can update faster than the displayed precision.
    // Identical visible frames are therefore not transmitted repeatedly
    // over I2C.
    uint32_t lastVisualSignature;

    bool haveVisualSignature;

    // A physical page change sets this flag so the next update bypasses
    // ordinary frame-rate and content-change suppression.
    bool forceRedraw;

    // This short guard coalesces bursts such as DHT22 and DS18B20
    // updates completing close together.
    //
    // A physical page change bypasses the guard immediately.
    static constexpr uint32_t MIN_FRAME_INTERVAL_MS =
        75UL;

    // ========================================================
    // MAIN RENDERING
    // ========================================================

    uint32_t visualSignature(
        const C3DisplayData &data) const;

    void renderCurrentPage(
        const C3DisplayData &data,
        uint32_t nowMs);

    // ========================================================
    // THREE ACTIVE RUNTIME PAGES
    // ========================================================

    void renderDashboard(
        const C3DisplayData &data,
        uint32_t nowMs);

    void renderReservoir(
        const C3DisplayData &data,
        uint32_t nowMs);

    void renderEnvironment(
        const C3DisplayData &data);

    // ========================================================
    // LEGACY INTERNAL COMPATIBILITY FUNCTIONS
    //
    // These functions remain declared because matching definitions
    // still exist in C3DisplayController.cpp. They are not selectable
    // runtime pages and do not increase the page count beyond three.
    // ========================================================

    void renderTrends(
        const C3DisplayData &data);

    void renderHealth(
        const C3DisplayData &data,
        uint32_t nowMs);

    void renderCalibration(
        const C3DisplayData &data);

    // ========================================================
    // COMMON UI
    // ========================================================

    void drawHeader(
        const char *title,
        const uint8_t *icon);

    void drawPageIndicators();

    void drawStatusIcon(
        bool good,
        int16_t x,
        int16_t y,
        uint32_t nowMs);

    void drawTank(
        float levelPct,
        int16_t x,
        int16_t y,
        uint32_t nowMs);

    // ========================================================
    // FORMATTING
    // ========================================================

    void formatValue(
        char *buffer,
        size_t length,
        float value,
        uint8_t decimals,
        const char *unit) const;

    void formatTrend(
        char *buffer,
        size_t length,
        float value,
        const char *unit) const;

    const char *sensorStateText(
        SensorState state) const;
};