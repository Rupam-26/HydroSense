#include "ui/C3DisplayController.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
    constexpr uint8_t RUNTIME_PAGE_COUNT = 3;

    const char *reservoirTypeText(C3ReservoirType type)
    {
        switch (type)
        {
        case C3ReservoirType::RECTANGULAR:
            return "RECT";
        case C3ReservoirType::VERTICAL_CYLINDER:
            return "DRUM";
        default:
            return "NOT SET";
        }
    }

    void safeFloat(char *buffer, size_t length, float value, uint8_t decimals)
    {
        if (!isfinite(value))
        {
            snprintf(buffer, length, "--");
            return;
        }

        if (decimals == 0)
        {
            snprintf(buffer, length, "%.0f", value);
        }
        else if (decimals == 1)
        {
            snprintf(buffer, length, "%.1f", value);
        }
        else
        {
            snprintf(buffer, length, "%.2f", value);
        }
    }

    uint32_t mixHash(uint32_t hash, uint32_t value)
    {
        // FNV-1a over one 32-bit value.
        for (uint8_t i = 0; i < 4; ++i)
        {
            hash ^= static_cast<uint8_t>(
                (value >> (i * 8U)) & 0xFFU);
            hash *= 16777619UL;
        }

        return hash;
    }

    uint32_t quantizedVisualValue(float value, float scale)
    {
        if (!isfinite(value))
        {
            return 0x80000000UL;
        }

        const int32_t quantized =
            static_cast<int32_t>(
                lroundf(value * scale));

        return static_cast<uint32_t>(quantized);
    }
} // namespace

C3DisplayController::C3DisplayController(OLEDManager &oledManager)
    : oled(oledManager),
      activePage(C3DisplayPage::DASHBOARD),
      lastRenderMs(0),
      lastVisualSignature(0),
      haveVisualSignature(false),
      forceRedraw(true)
{
}

void C3DisplayController::begin(uint32_t nowMs)
{
    activePage = C3DisplayPage::DASHBOARD;
    lastRenderMs = nowMs - MIN_FRAME_INTERVAL_MS;
    lastVisualSignature = 0;
    haveVisualSignature = false;
    forceRedraw = true;

    Serial.println("[DISPLAY] Controller initialized");
    Serial.println("[DISPLAY] 3-page HydroSense UI ready");
}

void C3DisplayController::nextPage(uint32_t nowMs)
{
    uint8_t page = static_cast<uint8_t>(activePage);
    page = static_cast<uint8_t>((page + 1U) % RUNTIME_PAGE_COUNT);
    activePage = static_cast<C3DisplayPage>(page);

    // Human page changes must feel immediate. The next update() call
    // bypasses both revision gating and the minimum frame interval.
    forceRedraw = true;
    lastRenderMs = nowMs - MIN_FRAME_INTERVAL_MS;
}

void C3DisplayController::update(
    const C3DisplayData &data,
    uint32_t nowMs)
{
    if (!oled.isHealthy())
    {
        return;
    }

    const uint32_t currentVisualSignature =
        visualSignature(data);

    const bool visibleFrameChanged =
        !haveVisualSignature ||
        currentVisualSignature !=
            lastVisualSignature;

    // A new sensor snapshot does not automatically mean new visible
    // pixels. Compare only the values used by the active page at the
    // same precision that is actually rendered. This prevents the
    // 250-ms sonar path from causing redundant full-frame I2C writes.
    if (!forceRedraw &&
        !visibleFrameChanged)
    {
        return;
    }

    // Coalesce only genuine bursts of snapshot updates. A physical
    // button page change sets forceRedraw and bypasses this guard.
    if (!forceRedraw &&
        (nowMs - lastRenderMs) <
            MIN_FRAME_INTERVAL_MS)
    {
        return;
    }

    renderCurrentPage(
        data,
        nowMs);

    lastRenderMs =
        millis();

    lastVisualSignature =
        currentVisualSignature;

    haveVisualSignature =
        true;

    forceRedraw =
        false;
}

uint32_t C3DisplayController::visualSignature(
    const C3DisplayData &data) const
{
    uint32_t hash = 2166136261UL;

    hash = mixHash(
        hash,
        static_cast<uint32_t>(activePage));

    switch (activePage)
    {
    case C3DisplayPage::DASHBOARD:
        hash = mixHash(hash, quantizedVisualValue(data.analytics.airTemperatureC, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.analytics.humidityPct, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.analytics.waterTemperatureC, 100.0f));
        hash = mixHash(hash, quantizedVisualValue(data.currentStableDistanceCm, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.analytics.waterLevelPct, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.quality.scorePct, 1.0f));
        hash = mixHash(hash, data.health.dht22.valid ? 1U : 0U);
        hash = mixHash(hash, data.health.ds18b20.valid ? 1U : 0U);
        hash = mixHash(hash, data.distanceHeld ? 1U : 0U);
        break;

    case C3DisplayPage::RESERVOIR:
        hash = mixHash(hash, static_cast<uint32_t>(data.reservoirType));
        hash = mixHash(hash, quantizedVisualValue(data.reservoirCapacityL, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.fullDistanceCm, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.halfDistanceCm, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.emptyDistanceCm, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.analytics.waterLevelPct, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.currentVolumeL, 10.0f));
        break;

    case C3DisplayPage::ENVIRONMENT:
        hash = mixHash(hash, quantizedVisualValue(data.analytics.environment.dewPointC, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.analytics.environment.vpdKPa, 100.0f));
        hash = mixHash(hash, quantizedVisualValue(data.analytics.environment.absoluteHumidityGM3, 10.0f));
        hash = mixHash(hash, quantizedVisualValue(data.analytics.environment.airWaterDeltaC, 100.0f));
        hash = mixHash(hash, quantizedVisualValue(data.speedOfSoundMps, 10.0f));
        break;

    default:
        break;
    }

    return hash;
}

C3DisplayPage C3DisplayController::currentPage() const
{
    return activePage;
}

uint8_t C3DisplayController::pageNumber() const
{
    return static_cast<uint8_t>(activePage) + 1U;
}

void C3DisplayController::renderCurrentPage(
    const C3DisplayData &data,
    uint32_t nowMs)
{
    oled.clear();

    switch (activePage)
    {
    case C3DisplayPage::DASHBOARD:
        renderDashboard(data, nowMs);
        break;

    case C3DisplayPage::RESERVOIR:
        renderReservoir(data, nowMs);
        break;

    case C3DisplayPage::ENVIRONMENT:
        renderEnvironment(data);
        break;

    default:
        activePage = C3DisplayPage::DASHBOARD;
        renderDashboard(data, nowMs);
        break;
    }

    drawPageIndicators();
    oled.present();
}

void C3DisplayController::drawHeader(
    const char *title,
    const uint8_t *icon)
{
    (void)icon;

    oled.drawText(1, 0, "HYDROSENSE", 1);
    oled.drawText(61, 0, title, 1);

    char page[8];
    snprintf(page, sizeof(page), "%u/3", pageNumber());
    oled.drawText(108, 0, page, 1);
    oled.drawHLine(0, 10, 128);
}

void C3DisplayController::drawPageIndicators()
{
    const uint8_t selected = static_cast<uint8_t>(activePage);
    constexpr int16_t startX = 109;

    for (uint8_t i = 0; i < RUNTIME_PAGE_COUNT; ++i)
    {
        const int16_t x = startX + static_cast<int16_t>(i * 7);

        if (i == selected)
        {
            oled.drawDisc(x, 62, 1);
        }
        else
        {
            oled.drawCircle(x, 62, 1);
        }
    }
}

void C3DisplayController::renderDashboard(
    const C3DisplayData &data,
    uint32_t nowMs)
{
    (void)nowMs;

    drawHeader("SENS", nullptr);

    char value[16];
    char line[40];

    safeFloat(
        value,
        sizeof(value),
        data.analytics.airTemperatureC,
        1);

    snprintf(
        line,
        sizeof(line),
        "AIR   %s C %s",
        value,
        data.health.dht22.valid ? "OK" : "--");

    oled.drawText(1, 13, line, 1);

    safeFloat(
        value,
        sizeof(value),
        data.analytics.humidityPct,
        1);

    snprintf(
        line,
        sizeof(line),
        "HUM   %s %% %s",
        value,
        data.health.dht22.valid ? "OK" : "--");

    oled.drawText(1, 23, line, 1);

    safeFloat(
        value,
        sizeof(value),
        data.analytics.waterTemperatureC,
        2);

    snprintf(
        line,
        sizeof(line),
        "WATER %s C %s",
        value,
        data.health.ds18b20.valid ? "OK" : "--");

    oled.drawText(1, 33, line, 1);

    safeFloat(
        value,
        sizeof(value),
        data.currentStableDistanceCm,
        1);

    const char *distanceState =
        !isfinite(data.currentStableDistanceCm)
            ? "--"
        : data.distanceHeld
            ? "HLD"
            : "OK";

    snprintf(
        line,
        sizeof(line),
        "DIST  %s cm %s",
        value,
        distanceState);

    oled.drawText(1, 43, line, 1);

    safeFloat(
        value,
        sizeof(value),
        data.analytics.waterLevelPct,
        1);

    char quality[8];

    safeFloat(
        quality,
        sizeof(quality),
        data.quality.scorePct,
        0);

    snprintf(
        line,
        sizeof(line),
        "LEVEL %s %% Q%s",
        value,
        quality);

    oled.drawText(1, 53, line, 1);
}

void C3DisplayController::renderReservoir(
    const C3DisplayData &data,
    uint32_t nowMs)
{
    (void)nowMs;

    drawHeader("TANK", nullptr);

    char a[12];
    char b[12];
    char line[40];

    snprintf(
        line,
        sizeof(line),
        "TYPE  %s",
        reservoirTypeText(data.reservoirType));

    oled.drawText(1, 13, line, 1);

    safeFloat(
        a,
        sizeof(a),
        data.reservoirCapacityL,
        1);

    snprintf(
        line,
        sizeof(line),
        "SIZE  %s L",
        a);

    oled.drawText(1, 23, line, 1);

    safeFloat(
        a,
        sizeof(a),
        data.fullDistanceCm,
        1);

    safeFloat(
        b,
        sizeof(b),
        data.halfDistanceCm,
        1);

    snprintf(
        line,
        sizeof(line),
        "FULL %s H %s",
        a,
        b);

    oled.drawText(1, 33, line, 1);

    safeFloat(
        a,
        sizeof(a),
        data.emptyDistanceCm,
        1);

    snprintf(
        line,
        sizeof(line),
        "EMPTY %s cm",
        a);

    oled.drawText(1, 43, line, 1);

    safeFloat(
        a,
        sizeof(a),
        data.analytics.waterLevelPct,
        1);

    safeFloat(
        b,
        sizeof(b),
        data.currentVolumeL,
        1);

    snprintf(
        line,
        sizeof(line),
        "NOW %s%% %sL",
        a,
        b);

    oled.drawText(1, 53, line, 1);
}

void C3DisplayController::renderEnvironment(
    const C3DisplayData &data)
{
    drawHeader("DERIV", nullptr);

    char value[16];
    char line[40];

    safeFloat(
        value,
        sizeof(value),
        data.analytics.environment.dewPointC,
        1);

    snprintf(
        line,
        sizeof(line),
        "DEW   %s C",
        value);

    oled.drawText(1, 13, line, 1);

    safeFloat(
        value,
        sizeof(value),
        data.analytics.environment.vpdKPa,
        2);

    snprintf(
        line,
        sizeof(line),
        "VPD   %s kPa",
        value);

    oled.drawText(1, 23, line, 1);

    safeFloat(
        value,
        sizeof(value),
        data.analytics.environment.absoluteHumidityGM3,
        1);

    snprintf(
        line,
        sizeof(line),
        "ABS H %s g/m3",
        value);

    oled.drawText(1, 33, line, 1);

    safeFloat(
        value,
        sizeof(value),
        data.analytics.environment.airWaterDeltaC,
        2);

    snprintf(
        line,
        sizeof(line),
        "AIR-W %s C",
        value);

    oled.drawText(1, 43, line, 1);

    safeFloat(
        value,
        sizeof(value),
        data.speedOfSoundMps,
        1);

    snprintf(
        line,
        sizeof(line),
        "SOUND %s m/s",
        value);

    oled.drawText(1, 53, line, 1);
}

// ============================================================
// LEGACY PRIVATE METHODS
//
// The header is intentionally kept compatible with the existing
// HydroSense project. Runtime page switching uses the three
// pages above.
// ============================================================

void C3DisplayController::renderTrends(
    const C3DisplayData &data)
{
    renderEnvironment(data);
}

void C3DisplayController::renderHealth(
    const C3DisplayData &data,
    uint32_t nowMs)
{
    renderDashboard(data, nowMs);
}

void C3DisplayController::renderCalibration(
    const C3DisplayData &data)
{
    renderReservoir(data, 0);
}

void C3DisplayController::drawStatusIcon(
    bool good,
    int16_t x,
    int16_t y,
    uint32_t nowMs)
{
    (void)nowMs;

    if (good)
    {
        oled.drawDisc(x, y, 2);
    }
    else
    {
        oled.drawCircle(x, y, 2);
    }
}

void C3DisplayController::drawTank(
    float levelPct,
    int16_t x,
    int16_t y,
    uint32_t nowMs)
{
    (void)nowMs;

    oled.drawFrame(x, y, 18, 30);

    if (!isfinite(levelPct))
    {
        return;
    }

    if (levelPct < 0.0f)
    {
        levelPct = 0.0f;
    }

    if (levelPct > 100.0f)
    {
        levelPct = 100.0f;
    }

    const int16_t fill =
        static_cast<int16_t>(
            (26.0f * levelPct) / 100.0f);

    if (fill > 0)
    {
        oled.drawBox(
            x + 2,
            y + 28 - fill,
            14,
            fill);
    }
}

void C3DisplayController::formatValue(
    char *buffer,
    size_t length,
    float value,
    uint8_t decimals,
    const char *unit) const
{
    char number[16];

    safeFloat(
        number,
        sizeof(number),
        value,
        decimals);

    snprintf(
        buffer,
        length,
        "%s%s",
        number,
        unit == nullptr ? "" : unit);
}

void C3DisplayController::formatTrend(
    char *buffer,
    size_t length,
    float value,
    const char *unit) const
{
    formatValue(
        buffer,
        length,
        value,
        2,
        unit);
}

const char *C3DisplayController::sensorStateText(
    SensorState state) const
{
    switch (state)
    {
    case SensorState::LIVE:
        return "LIVE";

    case SensorState::STALE:
        return "STALE";

    case SensorState::FAULT:
        return "FAULT";

    case SensorState::NEVER:
    default:
        return "WAIT";
    }
}