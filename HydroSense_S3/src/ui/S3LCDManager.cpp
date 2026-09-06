#include "ui/S3LCDManager.h"

#include <Arduino_GFX_Library.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

// ============================================================
// HYDROSENSE ESP32-S3 LCD
// MINIMAL CLASSIC FERRARI F1 THEME
//
// DISPLAY:
// Waveshare ESP32-S3-LCD-1.9
// ST7789
// 320 x 170 landscape
//
// DESIGN RULES:
// 1. Carbon-black chassis with restrained Ferrari-red accents.
// 2. White data cards keep EVERY genuine numeric value BLACK and obvious.
// 3. Positive states are GREEN.
// 4. Negative / fault states are RED.
// 5. WAIT / transitional states are AMBER.
// 6. No purple, gradients, shadows, or decorative clutter.
// 7. Static UI is redrawn only on page changes.
// 8. Ordinary refreshes touch only small dynamic regions.
//
// NO SENSOR / BLE / WIFI / TINYML / CLOUD LOGIC IS HERE.
// ============================================================

namespace
{
    // ========================================================
    // HARDWARE
    // ========================================================

    constexpr int LCD_DC = 11;
    constexpr int LCD_CS = 12;
    constexpr int LCD_SCK = 10;
    constexpr int LCD_MOSI = 13;
    constexpr int LCD_RST = 9;
    constexpr int LCD_BL = 14;

    // Official BOOT0 key on the Waveshare board.
    constexpr int PAGE_BUTTON_PIN = 0;

    // ========================================================
    // DISPLAY DRIVER
    // ========================================================

    Arduino_DataBus *lcdBus =
        new Arduino_HWSPI(
            LCD_DC,
            LCD_CS,
            LCD_SCK,
            LCD_MOSI);

    Arduino_GFX *display =
        new Arduino_ST7789(
            lcdBus,
            LCD_RST,
            0,
            false,
            170,
            320,
            35,
            0,
            35,
            0);

    // ========================================================
    // SCREEN GEOMETRY
    // ========================================================

    constexpr int16_t SCREEN_W = 320;
    constexpr int16_t SCREEN_H = 170;

    constexpr int16_t HEADER_H = 26;

    constexpr int16_t FOOTER_H = 15;
    constexpr int16_t FOOTER_Y = SCREEN_H - FOOTER_H;

    constexpr int16_t CARD_Y = 31;
    constexpr int16_t CARD_H = 119;
    constexpr int16_t CARD_W = 100;

    constexpr int16_t CARD_X1 = 5;
    constexpr int16_t CARD_X2 = 110;
    constexpr int16_t CARD_X3 = 215;

    constexpr int16_t ROW1_Y = 57;
    constexpr int16_t ROW2_Y = 89;
    constexpr int16_t ROW3_Y = 121;

    // ========================================================
    // PROFESSIONAL CLASSIC FERRARI F1 COLOR SYSTEM - RGB565
    //
    // Design rule:
    // - Carbon-black chassis/background = Ferrari cockpit identity.
    // - White data cards = maximum readability.
    // - EVERY genuine numeric value = pure black on white.
    // - Positive state = green.
    // - Negative/fault state = red.
    // - Waiting/learning state = amber.
    // - Ferrari red is structural/branding, never used as a data value.
    // ========================================================

    // Carbon-black chassis surrounding the white data cards.
    constexpr uint16_t C_BG = 0x0000;

    // Data surfaces are always bright so black values can never disappear.
    constexpr uint16_t C_PANEL = 0xFFFF;
    constexpr uint16_t C_FOOTER = 0xFFFF;

    // Text hierarchy.
    constexpr uint16_t C_BLACK = 0x0000;
    constexpr uint16_t C_VALUE = 0x0000; // ALL genuine numeric values
    constexpr uint16_t C_LABEL = 0x2104; // dark graphite labels
    constexpr uint16_t C_WHITE = 0xFFFF;

    // Classic Ferrari identity.
    constexpr uint16_t C_FERRARI_RED = 0xD800; // deep Rosso-style red
    constexpr uint16_t C_FERRARI_BRIGHT = 0xF800;
    constexpr uint16_t C_CARBON = 0x0000; // pure carbon black

    // Neutral structure.
    constexpr uint16_t C_BORDER = 0x8410;  // medium neutral grey
    constexpr uint16_t C_DIVIDER = 0xC618; // light neutral grey divider
    constexpr uint16_t C_SHADOW = 0x0000;  // retained compatibility; not drawn

    // Semantic colors. Kept distinct from decorative Ferrari red.
    constexpr uint16_t C_GREEN = 0x04E0; // strong positive / valid / online
    constexpr uint16_t C_RED = 0xF800;   // negative / fault / offline
    constexpr uint16_t C_AMBER = 0xD560; // waiting / transitional

    // Compatibility names used by existing call sites.
    constexpr uint16_t C_TEXT = C_BLACK;
    constexpr uint16_t C_MUTED = C_LABEL;
    constexpr uint16_t C_CYAN = C_FERRARI_RED;
    constexpr uint16_t C_TEAL = C_CARBON;

    // ========================================================
    // TIMING
    // ========================================================

    constexpr uint8_t PAGE_COUNT = 3;
    constexpr uint32_t BUTTON_LOCKOUT_MS = 180UL;
    constexpr uint32_t VALUE_REFRESH_MS = 100UL; // legacy; packet-driven renderer does not use it
    constexpr uint32_t FOOTER_REFRESH_MS = 500UL;
    constexpr uint32_t TRANSITION_DURATION_MS = 420UL;

    // ========================================================
    // SMALL COLOR / TEXT HELPERS
    // ========================================================

    bool textEquals(
        const char *a,
        const char *b)
    {
        return a != nullptr &&
               b != nullptr &&
               strcmp(a, b) == 0;
    }

    bool isPositiveState(
        const char *state)
    {
        return textEquals(state, "VALID") ||
               textEquals(state, "READY") ||
               textEquals(state, "ON") ||
               textEquals(state, "CONNECTED") ||
               textEquals(state, "ONLINE") ||
               textEquals(state, "CLEAR") ||
               textEquals(state, "OK");
    }

    bool isNegativeState(
        const char *state)
    {
        return textEquals(state, "FAULT") ||
               textEquals(state, "FAIL") ||
               textEquals(state, "ERROR") ||
               textEquals(state, "INVALID") ||
               textEquals(state, "OFF") ||
               textEquals(state, "DISCONNECTED") ||
               textEquals(state, "YES");
    }

    bool isWaitingState(
        const char *state)
    {
        return textEquals(state, "WAIT") ||
               textEquals(state, "LEARNING");
    }

    uint16_t semanticColor(
        const char *state,
        uint16_t requestedColor)
    {
        if (isPositiveState(state))
        {
            return C_GREEN;
        }

        if (isNegativeState(state))
        {
            return C_RED;
        }

        if (isWaitingState(state))
        {
            return C_AMBER;
        }

        // Non-semantic informational strings, e.g. "32-16-8",
        // are ordinary displayed values and therefore BLACK.
        (void)requestedColor;
        return C_VALUE;
    }

    uint16_t cloudStateColor(
        S3CloudChannelState state)
    {
        switch (state)
        {
        case S3CloudChannelState::Online:
            return C_GREEN;

        case S3CloudChannelState::WaitingForWiFi:
        case S3CloudChannelState::WaitingForData:
            return C_AMBER;

        case S3CloudChannelState::RetryPending:
        case S3CloudChannelState::NotConfigured:
        default:
            return C_RED;
        }
    }

    // Centered text helper for namespace-level boot animation.
    // Member drawCenteredText() cannot be called before an S3LCDManager
    // instance is involved, so the one-time boot animation uses this local
    // equivalent.
    void drawCenteredTextRaw(
        int16_t x,
        int16_t width,
        int16_t y,
        const char *textValue,
        uint8_t textSize,
        uint16_t color)
    {
        if (textValue == nullptr)
        {
            return;
        }

        const int16_t estimatedWidth =
            static_cast<int16_t>(
                strlen(textValue) *
                6 *
                textSize);

        int16_t cursorX =
            x +
            (width - estimatedWidth) / 2;

        if (cursorX < x + 2)
        {
            cursorX =
                x + 2;
        }

        display->setTextSize(
            textSize);

        display->setTextColor(
            color);

        display->setCursor(
            cursorX,
            y);

        display->print(
            textValue);
    }

    // ========================================================
    // FERRARI TRANSITION RAIL
    //
    // The header itself is red, so the moving rail deliberately uses
    // carbon-black + white + a small red nose. This guarantees that the
    // animation remains visible instead of red disappearing into red.
    // ========================================================

    void drawFerrariRail(
        uint8_t page,
        int16_t width,
        bool movingHead)
    {
        (void)page;

        if (width < 0)
        {
            width = 0;
        }

        if (width > SCREEN_W)
        {
            width = SCREEN_W;
        }

        // A single 2 px Ferrari-red sweep on black.
        display->fillRect(
            0,
            HEADER_H - 2,
            SCREEN_W,
            2,
            C_CARBON);

        if (width > 0)
        {
            display->fillRect(
                0,
                HEADER_H - 2,
                width,
                2,
                C_FERRARI_RED);
        }

        if (movingHead && width > 0)
        {
            const int16_t headX =
                width > 6
                    ? width - 6
                    : 0;

            display->fillRect(
                headX,
                HEADER_H - 2,
                width - headX,
                2,
                C_WHITE);
        }
    }

    // ========================================================
    // ONE-TIME F1 START-LIGHT BOOT ANIMATION
    //
    // Strong classic Ferrari presentation:
    // carbon-black canvas, Rosso Corsa accents and five start lights.
    // ========================================================

    void drawBootAnimation()
    {
        display->fillScreen(
            C_BG);

        // Minimal Ferrari identity.
        display->fillRect(
            0,
            0,
            SCREEN_W,
            4,
            C_FERRARI_RED);

        drawCenteredTextRaw(
            0,
            SCREEN_W,
            23,
            "HYDROSENSE",
            2,
            C_WHITE);

        drawCenteredTextRaw(
            0,
            SCREEN_W,
            45,
            "EDGE TELEMETRY",
            1,
            C_DIVIDER);

        // Five compact F1 start lights.
        constexpr int16_t LIGHT_Y = 84;
        constexpr int16_t FIRST_X = 80;
        constexpr int16_t LIGHT_GAP = 40;
        constexpr int16_t LIGHT_R = 8;

        for (uint8_t i = 0; i < 5; ++i)
        {
            const int16_t x =
                FIRST_X +
                static_cast<int16_t>(i) *
                    LIGHT_GAP;

            display->drawCircle(
                x,
                LIGHT_Y,
                LIGHT_R + 1,
                C_DIVIDER);

            display->fillCircle(
                x,
                LIGHT_Y,
                LIGHT_R - 1,
                C_BORDER);
        }

        for (uint8_t i = 0; i < 5; ++i)
        {
            const int16_t x =
                FIRST_X +
                static_cast<int16_t>(i) *
                    LIGHT_GAP;

            display->fillCircle(
                x,
                LIGHT_Y,
                LIGHT_R - 1,
                C_FERRARI_BRIGHT);

            delay(
                120);
        }

        delay(
            250);

        // Lights out.
        for (uint8_t i = 0; i < 5; ++i)
        {
            const int16_t x =
                FIRST_X +
                static_cast<int16_t>(i) *
                    LIGHT_GAP;

            display->fillCircle(
                x,
                LIGHT_Y,
                LIGHT_R - 1,
                C_BLACK);
        }

        display->drawFastHLine(
            48,
            111,
            224,
            C_DIVIDER);

        for (int16_t width = 0;
             width <= 224;
             width += 28)
        {
            display->fillRect(
                48,
                110,
                224,
                3,
                C_CARBON);

            if (width > 0)
            {
                display->fillRect(
                    48,
                    110,
                    width,
                    3,
                    C_FERRARI_RED);
            }

            delay(
                10);
        }

        drawCenteredTextRaw(
            0,
            SCREEN_W,
            139,
            "SYSTEM READY",
            1,
            C_GREEN);

        delay(
            160);
    }
}

// ============================================================
// CENTERED TEXT
// ============================================================

void S3LCDManager::drawCenteredText(
    int16_t x,
    int16_t width,
    int16_t y,
    const char *text,
    uint8_t textSize,
    uint16_t color)
{
    if (text == nullptr)
    {
        return;
    }

    const int16_t estimatedWidth =
        static_cast<int16_t>(
            strlen(text) *
            6 *
            textSize);

    int16_t cursorX =
        x +
        (width - estimatedWidth) / 2;

    if (cursorX < x + 2)
    {
        cursorX = x + 2;
    }

    display->setTextSize(
        textSize);

    display->setTextColor(
        color);

    display->setCursor(
        cursorX,
        y);

    display->print(
        text);
}

// ============================================================
// INITIALIZATION
// ============================================================

bool S3LCDManager::begin()
{
    // Keep the board's existing backlight polarity and sequencing.
    pinMode(
        LCD_BL,
        OUTPUT);

    digitalWrite(
        LCD_BL,
        HIGH);

    pinMode(
        PAGE_BUTTON_PIN,
        INPUT_PULLUP);

    delay(
        5);

    lastButtonLevel_ =
        digitalRead(
            PAGE_BUTTON_PIN);

    Serial.println();
    Serial.println(
        "============================================");
    Serial.println(
        "[LCD UI] HydroSense Ferrari display initializing");
    Serial.print(
        "[LCD UI] BOOT GPIO0 idle state : ");
    Serial.println(
        lastButtonLevel_
            ? "HIGH"
            : "LOW");
    Serial.println(
        "============================================");

    if (!display->begin())
    {
        initialized_ =
            false;

        Serial.println(
            "[LCD UI] LCD initialization FAILED");

        return false;
    }

    display->setRotation(
        1);

    display->setTextWrap(
        false);

    // Existing board uses active-low backlight.
    digitalWrite(
        LCD_BL,
        LOW);

    initialized_ =
        true;

    currentPage_ =
        0;

    pageDirty_ =
        true;

    // Reset packet-driven render state.
    renderedSourceStateValid_ =
        false;

    renderedHeaderStateValid_ =
        false;

    renderedTinyMLStateValid_ =
        false;

    renderedFooterStateValid_ =
        false;

    lastFooterRefreshMs_ =
        0;

    lastValueRefreshMs_ =
        0;

    transitionActive_ =
        false;

    previousTransitionWidth_ =
        0;

    drawBootAnimation();

    Serial.println(
        "[LCD UI] LCD READY");
    Serial.println(
        "[LCD UI] BOOT single-click changes page");

    return true;
}

// ============================================================
// READY
// ============================================================

bool S3LCDManager::ready() const
{
    return initialized_;
}

// ============================================================
// BOOT BUTTON
// ============================================================

void S3LCDManager::updateControls(
    uint32_t nowMs)
{
    if (!initialized_)
    {
        return;
    }

    const bool currentButtonLevel =
        digitalRead(
            PAGE_BUTTON_PIN);

    // One HIGH -> LOW transition = one accepted page change.
    if (lastButtonLevel_ == HIGH &&
        currentButtonLevel == LOW)
    {
        if (lastAcceptedPressMs_ == 0 ||
            nowMs - lastAcceptedPressMs_ >=
                BUTTON_LOCKOUT_MS)
        {
            lastAcceptedPressMs_ =
                nowMs;

            ++currentPage_;

            if (currentPage_ >= PAGE_COUNT)
            {
                currentPage_ =
                    0;
            }

            pageDirty_ =
                true;

            transitionActive_ =
                true;

            transitionStartMs_ =
                nowMs;

            previousTransitionWidth_ =
                0;

            Serial.print(
                "[LCD UI] BOOT -> PAGE ");
            Serial.print(
                currentPage_ + 1);
            Serial.print(
                "/");
            Serial.println(
                PAGE_COUNT);
        }
    }

    lastButtonLevel_ =
        currentButtonLevel;
}

// ============================================================
// HEADER STATIC
// ============================================================

void S3LCDManager::drawHeaderStatic(
    const char *pageTitle)
{
    display->fillRect(
        0,
        0,
        SCREEN_W,
        HEADER_H,
        C_CARBON);

    // Thin Ferrari identity line.
    display->fillRect(
        0,
        0,
        SCREEN_W,
        3,
        C_FERRARI_RED);

    display->setTextSize(
        1);

    display->setTextColor(
        C_WHITE);

    display->setCursor(
        7,
        9);

    display->print(
        "HYDRO");

    display->setTextColor(
        C_FERRARI_BRIGHT);

    display->print(
        "SENSE");

    drawCenteredText(
        80,
        96,
        9,
        pageTitle,
        1,
        C_WHITE);

    // Minimal page dots.
    constexpr int16_t FIRST_DOT_X = 184;

    for (uint8_t i = 0;
         i < PAGE_COUNT;
         ++i)
    {
        const int16_t x =
            FIRST_DOT_X +
            i * 9;

        display->fillCircle(
            x,
            12,
            i == currentPage_
                ? 3
                : 2,
            i == currentPage_
                ? C_FERRARI_RED
                : C_DIVIDER);
    }

    display->setTextColor(
        C_DIVIDER);

    display->setCursor(
        208,
        9);
    display->print(
        "B");

    display->setCursor(
        234,
        9);
    display->print(
        "W");

    display->setCursor(
        260,
        9);
    display->print(
        "C");

    display->setCursor(
        286,
        9);
    display->print(
        "M");

    drawFerrariRail(
        currentPage_,
        18,
        false);
}

// ============================================================
// HEADER DYNAMIC STATUS
// ============================================================

void S3LCDManager::updateHeaderStatus(
    bool bleConnected,
    bool wifiConnected)
{
    S3CloudRuntimeStatus cloudStatus;

    updateHeaderStatus(
        bleConnected,
        wifiConnected,
        cloudStatus);
}

void S3LCDManager::updateHeaderStatus(
    bool bleConnected,
    bool wifiConnected,
    const S3CloudRuntimeStatus &cloudStatus)
{
    // B = BLE.
    display->fillCircle(
        221,
        12,
        3,
        bleConnected
            ? C_GREEN
            : C_RED);

    // W = Wi-Fi.
    display->fillCircle(
        247,
        12,
        3,
        wifiConnected
            ? C_GREEN
            : C_RED);

    // C = realtime cloud API.
    display->fillCircle(
        273,
        12,
        3,
        cloudStateColor(
            cloudStatus.realtime));

    // M = historical MongoDB path.
    display->fillCircle(
        305,
        12,
        3,
        cloudStateColor(
            cloudStatus.historical));
}

// ============================================================
// FOOTER STATIC
// ============================================================

void S3LCDManager::drawFooterStatic()
{
    display->fillRect(
        0,
        FOOTER_Y,
        SCREEN_W,
        FOOTER_H,
        C_FOOTER);

    display->drawFastHLine(
        0,
        FOOTER_Y,
        SCREEN_W,
        C_FERRARI_RED);

    display->setTextSize(
        1);

    display->setTextColor(
        C_LABEL);

    display->setCursor(
        5,
        FOOTER_Y + 4);
    display->print(
        "Q");

    display->setCursor(
        58,
        FOOTER_Y + 4);
    display->print(
        "P");

    display->setCursor(
        116,
        FOOTER_Y + 4);
    display->print(
        "CRC");

    display->setCursor(
        173,
        FOOTER_Y + 4);
    display->print(
        "AGE");

    display->setCursor(
        273,
        FOOTER_Y + 4);
    display->print(
        "SEQ");
}

// ============================================================
// FOOTER DYNAMIC
//
// EVERY NUMBER IN THIS FOOTER IS BLACK.
// ============================================================

void S3LCDManager::updateFooter(
    const S3SensorSnapshot &snapshot,
    uint32_t validPacketCount,
    uint32_t crcFailures,
    uint32_t packetAgeMs)
{
    display->setTextSize(
        1);

    // QUALITY
    display->fillRect(
        17,
        FOOTER_Y + 2,
        36,
        11,
        C_FOOTER);

    display->setCursor(
        18,
        FOOTER_Y + 4);

    display->setTextColor(
        C_VALUE);

    display->print(
        snapshot.qualityScorePct,
        0);

    display->print(
        "%");

    // PACKETS
    display->fillRect(
        69,
        FOOTER_Y + 2,
        43,
        11,
        C_FOOTER);

    display->setCursor(
        70,
        FOOTER_Y + 4);

    display->setTextColor(
        C_VALUE);

    display->print(
        validPacketCount);

    // CRC
    display->fillRect(
        137,
        FOOTER_Y + 2,
        31,
        11,
        C_FOOTER);

    display->setCursor(
        138,
        FOOTER_Y + 4);

    display->setTextColor(
        C_VALUE);

    display->print(
        crcFailures);

    // AGE
    display->fillRect(
        197,
        FOOTER_Y + 2,
        72,
        11,
        C_FOOTER);

    display->setCursor(
        198,
        FOOTER_Y + 4);

    display->setTextColor(
        C_VALUE);

    display->print(
        packetAgeMs);

    display->print(
        "ms");

    // SEQUENCE
    display->fillRect(
        297,
        FOOTER_Y + 2,
        22,
        11,
        C_FOOTER);

    display->setCursor(
        298,
        FOOTER_Y + 4);

    display->setTextColor(
        C_VALUE);

    display->print(
        snapshot.sequence);
}

// ============================================================
// CARD
// ============================================================

void S3LCDManager::drawCard(
    int16_t x,
    const char *title,
    uint16_t accentColor)
{
    (void)accentColor;

    display->fillRoundRect(
        x,
        CARD_Y,
        CARD_W,
        CARD_H,
        6,
        C_PANEL);

    display->drawRoundRect(
        x,
        CARD_Y,
        CARD_W,
        CARD_H,
        6,
        C_BORDER);

    // One restrained Ferrari-red line only.
    display->fillRoundRect(
        x + 8,
        CARD_Y + 5,
        CARD_W - 16,
        3,
        1,
        C_FERRARI_RED);

    drawCenteredText(
        x + 4,
        CARD_W - 8,
        CARD_Y + 13,
        title,
        1,
        C_BLACK);

    display->drawFastHLine(
        x + 8,
        CARD_Y + 26,
        CARD_W - 16,
        C_DIVIDER);
}

// ============================================================
// METRIC LABEL
// ============================================================

void S3LCDManager::drawMetricLabel(
    int16_t cardX,
    int16_t labelY,
    const char *label)
{
    drawCenteredText(
        cardX + 3,
        CARD_W - 6,
        labelY,
        label,
        1,
        C_LABEL);
}

// ============================================================
// CLEAR DYNAMIC VALUE AREA
// ============================================================

void S3LCDManager::clearValueArea(
    int16_t cardX,
    int16_t labelY)
{
    display->fillRect(
        cardX + 5,
        labelY + 10,
        CARD_W - 10,
        18,
        C_PANEL);
}

// ============================================================
// FLOAT VALUE
//
// HARD RULE:
// - genuine value = BLACK
// - unavailable / invalid = RED "--"
// ============================================================

void S3LCDManager::drawFloatValue(
    int16_t cardX,
    int16_t labelY,
    float value,
    bool valid,
    uint16_t valueColor,
    uint8_t decimals)
{
    clearValueArea(
        cardX,
        labelY);

    char valueBuffer[20];

    uint16_t finalColor =
        C_VALUE;

    if (!valid ||
        !isfinite(value))
    {
        snprintf(
            valueBuffer,
            sizeof(valueBuffer),
            "--");

        finalColor =
            C_RED;
    }
    else
    {
        snprintf(
            valueBuffer,
            sizeof(valueBuffer),
            "%.*f",
            decimals,
            value);

        // Explicitly ignore caller color. User requirement:
        // ALL numeric values are BLACK.
        finalColor =
            C_VALUE;
    }

    (void)valueColor;

    const uint8_t valueTextSize =
        strlen(valueBuffer) <= 7
            ? 2
            : 1;

    drawCenteredText(
        cardX + 3,
        CARD_W - 6,
        labelY + 11,
        valueBuffer,
        valueTextSize,
        finalColor);
}

// ============================================================
// INTEGER VALUE
//
// HARD RULE:
// EVERY INTEGER VALUE IS BLACK.
// ============================================================

void S3LCDManager::drawIntegerValue(
    int16_t cardX,
    int16_t labelY,
    uint32_t value,
    uint16_t valueColor)
{
    clearValueArea(
        cardX,
        labelY);

    char valueBuffer[18];

    snprintf(
        valueBuffer,
        sizeof(valueBuffer),
        "%lu",
        static_cast<unsigned long>(value));

    (void)valueColor;

    const uint8_t valueTextSize =
        strlen(valueBuffer) <= 7
            ? 2
            : 1;

    drawCenteredText(
        cardX + 3,
        CARD_W - 6,
        labelY + 11,
        valueBuffer,
        valueTextSize,
        C_VALUE);
}

// ============================================================
// STATE VALUE
//
// GREEN = positive
// RED   = negative
// AMBER = waiting
// BLACK = informational non-status text
//
// Semantic states receive a visible outlined badge so they cannot blend
// into either the white card or the Ferrari-red/black chassis.
// ============================================================

void S3LCDManager::drawStateValue(
    int16_t cardX,
    int16_t labelY,
    const char *state,
    uint16_t stateColor)
{
    clearValueArea(
        cardX,
        labelY);

    if (state == nullptr)
    {
        state =
            "--";
    }

    uint16_t finalColor;

    if (textEquals(state, "--"))
    {
        finalColor =
            C_RED;
    }
    else
    {
        finalColor =
            semanticColor(
                state,
                stateColor);
    }

    const bool semantic =
        finalColor == C_GREEN ||
        finalColor == C_RED ||
        finalColor == C_AMBER;

    if (semantic)
    {
        display->fillCircle(
            cardX + 14,
            labelY + 19,
            3,
            finalColor);
    }

    const uint8_t stateTextSize =
        strlen(state) <= 7
            ? 2
            : 1;

    // Leave a little extra room for the status dot.
    drawCenteredText(
        cardX + (semantic ? 12 : 3),
        CARD_W - (semantic ? 18 : 6),
        labelY + 11,
        state,
        stateTextSize,
        finalColor);
}

// ============================================================
// GENERIC STATIC PAGE
// ============================================================

void S3LCDManager::drawPageStatic(
    const char *pageTitle,
    uint16_t pageAccent)
{
    (void)pageAccent;

    display->fillScreen(
        C_BG);

    drawHeaderStatic(
        pageTitle);

    drawFooterStatic();

    drawFerrariRail(
        currentPage_,
        18,
        false);
}

// ============================================================
// PAGE 1 STATIC - REAL SENSORS
// ============================================================

void S3LCDManager::drawRealSensorStatic()
{
    drawPageStatic(
        "REAL SENSORS",
        C_FERRARI_RED);

    drawCard(
        CARD_X1,
        "AIR",
        C_FERRARI_RED);

    drawMetricLabel(
        CARD_X1,
        ROW1_Y,
        "TEMP  C");

    drawMetricLabel(
        CARD_X1,
        ROW2_Y,
        "HUMIDITY  %");

    drawMetricLabel(
        CARD_X1,
        ROW3_Y,
        "DHT22");

    drawCard(
        CARD_X2,
        "WATER",
        C_CARBON);

    drawMetricLabel(
        CARD_X2,
        ROW1_Y,
        "TEMP  C");

    drawMetricLabel(
        CARD_X2,
        ROW2_Y,
        "LEVEL  %");

    drawMetricLabel(
        CARD_X2,
        ROW3_Y,
        "DS18B20");

    drawCard(
        CARD_X3,
        "RESERVOIR",
        C_FERRARI_RED);

    drawMetricLabel(
        CARD_X3,
        ROW1_Y,
        "DISTANCE  cm");

    drawMetricLabel(
        CARD_X3,
        ROW2_Y,
        "ULTRASONIC");

    drawMetricLabel(
        CARD_X3,
        ROW3_Y,
        "RESERVOIR");
}

// ============================================================
// PAGE 2 STATIC - EDGE AI
// ============================================================

void S3LCDManager::drawAIStatic()
{
    drawPageStatic(
        "EDGE AI",
        C_FERRARI_RED);

    drawCard(
        CARD_X1,
        "PREDICTION",
        C_FERRARI_RED);

    drawMetricLabel(
        CARD_X1,
        ROW1_Y,
        "EST TDS  ppm");

    drawMetricLabel(
        CARD_X1,
        ROW2_Y,
        "NORMALIZED");

    drawMetricLabel(
        CARD_X1,
        ROW3_Y,
        "AI STATUS");

    drawCard(
        CARD_X2,
        "MODEL",
        C_CARBON);

    drawMetricLabel(
        CARD_X2,
        ROW1_Y,
        "INFERENCE  us");

    drawMetricLabel(
        CARD_X2,
        ROW2_Y,
        "ENGINE");

    drawMetricLabel(
        CARD_X2,
        ROW3_Y,
        "NETWORK");

    drawCard(
        CARD_X3,
        "INPUT TRUST",
        C_FERRARI_RED);

    drawMetricLabel(
        CARD_X3,
        ROW1_Y,
        "INPUTS");

    drawMetricLabel(
        CARD_X3,
        ROW2_Y,
        "SATURATION");

    drawMetricLabel(
        CARD_X3,
        ROW3_Y,
        "SOURCE SEQ");
}

// ============================================================
// PAGE 3 STATIC - DERIVED PHYSICS
// ============================================================

void S3LCDManager::drawDerivedStatic()
{
    drawPageStatic(
        "DERIVED",
        C_FERRARI_RED);

    drawCard(
        CARD_X1,
        "ATMOSPHERE",
        C_FERRARI_RED);

    drawMetricLabel(
        CARD_X1,
        ROW1_Y,
        "DEW POINT  C");

    drawMetricLabel(
        CARD_X1,
        ROW2_Y,
        "VPD  kPa");

    drawMetricLabel(
        CARD_X1,
        ROW3_Y,
        "ABS HUM  g/m3");

    drawCard(
        CARD_X2,
        "VAPOR",
        C_CARBON);

    drawMetricLabel(
        CARD_X2,
        ROW1_Y,
        "SAT VP  kPa");

    drawMetricLabel(
        CARD_X2,
        ROW2_Y,
        "ACT VP  kPa");

    drawMetricLabel(
        CARD_X2,
        ROW3_Y,
        "ENV STATUS");

    drawCard(
        CARD_X3,
        "THERMAL",
        C_FERRARI_RED);

    drawMetricLabel(
        CARD_X3,
        ROW1_Y,
        "AIR-WATER  C");

    drawMetricLabel(
        CARD_X3,
        ROW2_Y,
        "ABS DELTA  C");

    drawMetricLabel(
        CARD_X3,
        ROW3_Y,
        "THERMAL");
}

// ============================================================
// PAGE 1 DYNAMIC
// ============================================================

void S3LCDManager::updateRealSensorValues(
    const S3SensorSnapshot &snapshot,
    bool haveSensorData)
{
    drawFloatValue(
        CARD_X1,
        ROW1_Y,
        snapshot.airTemperatureC,
        haveSensorData &&
            snapshot.airEnvironmentAvailable(),
        C_VALUE,
        2);

    drawFloatValue(
        CARD_X1,
        ROW2_Y,
        snapshot.humidityPct,
        haveSensorData &&
            snapshot.airEnvironmentAvailable(),
        C_VALUE,
        2);

    drawStateValue(
        CARD_X1,
        ROW3_Y,
        snapshot.dht22Valid
            ? "VALID"
            : "FAULT",
        snapshot.dht22Valid
            ? C_GREEN
            : C_RED);

    drawFloatValue(
        CARD_X2,
        ROW1_Y,
        snapshot.waterTemperatureC,
        haveSensorData &&
            snapshot.waterTemperatureAvailable(),
        C_VALUE,
        2);

    drawFloatValue(
        CARD_X2,
        ROW2_Y,
        snapshot.waterLevelPct,
        haveSensorData &&
            snapshot.waterLevelAvailable(),
        C_VALUE,
        2);

    drawStateValue(
        CARD_X2,
        ROW3_Y,
        snapshot.ds18b20Valid
            ? "VALID"
            : "FAULT",
        snapshot.ds18b20Valid
            ? C_GREEN
            : C_RED);

    drawFloatValue(
        CARD_X3,
        ROW1_Y,
        snapshot.stableDistanceCm,
        haveSensorData &&
            snapshot.distanceAvailable(),
        C_VALUE,
        1);

    drawStateValue(
        CARD_X3,
        ROW2_Y,
        snapshot.ultrasonicValid
            ? "VALID"
            : "FAULT",
        snapshot.ultrasonicValid
            ? C_GREEN
            : C_RED);

    drawStateValue(
        CARD_X3,
        ROW3_Y,
        snapshot.reservoirValid
            ? "VALID"
            : "WAIT",
        snapshot.reservoirValid
            ? C_GREEN
            : C_AMBER);
}

// ============================================================
// PAGE 2 DYNAMIC
// ============================================================

void S3LCDManager::updateAIValues(
    const S3SensorSnapshot &snapshot,
    const S3TDSResult &tdsResult,
    bool tinyMLReady)
{
    drawFloatValue(
        CARD_X1,
        ROW1_Y,
        tdsResult.estimatedTdsPpm,
        tdsResult.valid(),
        C_VALUE,
        1);

    drawFloatValue(
        CARD_X1,
        ROW2_Y,
        tdsResult.normalizedOutput,
        tdsResult.valid(),
        C_VALUE,
        3);

    if (!tinyMLReady)
    {
        drawStateValue(
            CARD_X1,
            ROW3_Y,
            "FAIL",
            C_RED);
    }
    else if (tdsResult.valid())
    {
        drawStateValue(
            CARD_X1,
            ROW3_Y,
            "VALID",
            C_GREEN);
    }
    else
    {
        drawStateValue(
            CARD_X1,
            ROW3_Y,
            "WAIT",
            C_AMBER);
    }

    drawIntegerValue(
        CARD_X2,
        ROW1_Y,
        tdsResult.inferenceMicros,
        C_VALUE);

    drawStateValue(
        CARD_X2,
        ROW2_Y,
        tinyMLReady
            ? "READY"
            : "FAIL",
        tinyMLReady
            ? C_GREEN
            : C_RED);

    // Informational model architecture -> BLACK.
    drawStateValue(
        CARD_X2,
        ROW3_Y,
        "32-16-8",
        C_VALUE);

    const bool modelInputsAvailable =
        snapshot.airEnvironmentAvailable() &&
        snapshot.waterTemperatureAvailable();

    drawStateValue(
        CARD_X3,
        ROW1_Y,
        modelInputsAvailable
            ? "VALID"
            : "WAIT",
        modelInputsAvailable
            ? C_GREEN
            : C_AMBER);

    drawStateValue(
        CARD_X3,
        ROW2_Y,
        tdsResult.inputSaturated
            ? "YES"
            : "CLEAR",
        tdsResult.inputSaturated
            ? C_RED
            : C_GREEN);

    drawIntegerValue(
        CARD_X3,
        ROW3_Y,
        tdsResult.sourceSequence,
        C_VALUE);
}

// ============================================================
// PAGE 3 DYNAMIC
// ============================================================

void S3LCDManager::updateDerivedValues(
    const S3FeatureData &features)
{
    drawFloatValue(
        CARD_X1,
        ROW1_Y,
        features.dewPointC,
        features.environmentalFeaturesValid,
        C_VALUE,
        1);

    drawFloatValue(
        CARD_X1,
        ROW2_Y,
        features.vpdKPa,
        features.environmentalFeaturesValid,
        C_VALUE,
        2);

    drawFloatValue(
        CARD_X1,
        ROW3_Y,
        features.absoluteHumidityGm3,
        features.environmentalFeaturesValid,
        C_VALUE,
        1);

    drawFloatValue(
        CARD_X2,
        ROW1_Y,
        features.saturationVaporPressureKPa,
        features.environmentalFeaturesValid,
        C_VALUE,
        2);

    drawFloatValue(
        CARD_X2,
        ROW2_Y,
        features.actualVaporPressureKPa,
        features.environmentalFeaturesValid,
        C_VALUE,
        2);

    drawStateValue(
        CARD_X2,
        ROW3_Y,
        features.environmentalFeaturesValid
            ? "VALID"
            : "WAIT",
        features.environmentalFeaturesValid
            ? C_GREEN
            : C_AMBER);

    drawFloatValue(
        CARD_X3,
        ROW1_Y,
        features.airWaterDeltaC,
        features.thermalFeaturesValid,
        C_VALUE,
        1);

    drawFloatValue(
        CARD_X3,
        ROW2_Y,
        features.absoluteAirWaterDeltaC,
        features.thermalFeaturesValid,
        C_VALUE,
        1);

    drawStateValue(
        CARD_X3,
        ROW3_Y,
        features.thermalFeaturesValid
            ? "VALID"
            : "WAIT",
        features.thermalFeaturesValid
            ? C_GREEN
            : C_AMBER);
}

// ============================================================
// PACKET / STATE CHANGE DETECTION
//
// The LCD must react to genuine source changes, not to an
// arbitrary 100-ms redraw timer.
// ============================================================

bool S3LCDManager::sourceFrameChanged(
    const S3SensorSnapshot &snapshot,
    bool haveSensorData) const
{
    if (!renderedSourceStateValid_)
    {
        return true;
    }

    if (haveSensorData !=
        lastRenderedHaveSensorData_)
    {
        return true;
    }

    if (!haveSensorData)
    {
        return false;
    }

    return snapshot.sequence !=
               lastRenderedSequence_ ||
           snapshot.receivedAtMs !=
               lastRenderedReceivedAtMs_;
}

bool S3LCDManager::headerStateChanged(
    bool bleConnected,
    bool wifiConnected) const
{
    return !renderedHeaderStateValid_ ||
           bleConnected !=
               lastRenderedBleConnected_ ||
           wifiConnected !=
               lastRenderedWifiConnected_;
}

bool S3LCDManager::headerStateChanged(
    bool bleConnected,
    bool wifiConnected,
    const S3CloudRuntimeStatus &cloudStatus) const
{
    return !renderedHeaderStateValid_ ||
           bleConnected !=
               lastRenderedBleConnected_ ||
           wifiConnected !=
               lastRenderedWifiConnected_ ||
           cloudStatus.realtime !=
               lastRenderedRealtimeState_ ||
           cloudStatus.historical !=
               lastRenderedHistoricalState_;
}

bool S3LCDManager::tinyMLStateChanged(
    bool tinyMLReady) const
{
    return !renderedTinyMLStateValid_ ||
           tinyMLReady !=
               lastRenderedTinyMLReady_;
}

bool S3LCDManager::footerStateChanged(
    const S3SensorSnapshot &snapshot,
    uint32_t validPacketCount,
    uint32_t crcFailures,
    uint32_t packetAgeMs,
    uint32_t nowMs) const
{
    if (!renderedFooterStateValid_)
    {
        return true;
    }

    // Packet/counter/quality changes should appear immediately.
    if (validPacketCount !=
            lastRenderedValidPacketCount_ ||
        crcFailures !=
            lastRenderedCrcFailures_ ||
        snapshot.sequence !=
            lastRenderedFooterSequence_ ||
        fabsf(snapshot.qualityScorePct -
              lastRenderedQualityPct_) >
            0.001f)
    {
        return true;
    }

    // AGE is the only intentionally time-driven footer value.
    // Updating just the small footer every 500 ms cannot cause
    // the sensor cards themselves to flicker.
    if (nowMs -
            lastFooterRefreshMs_ >=
        FOOTER_REFRESH_MS)
    {
        return true;
    }

    // If age moves backwards (new source time / reconnect),
    // reflect that immediately even before the next footer tick.
    if (packetAgeMs <
        lastRenderedPacketAgeMs_)
    {
        return true;
    }

    return false;
}

void S3LCDManager::rememberSourceFrame(
    const S3SensorSnapshot &snapshot,
    bool haveSensorData)
{
    renderedSourceStateValid_ =
        true;

    lastRenderedHaveSensorData_ =
        haveSensorData;

    lastRenderedSequence_ =
        snapshot.sequence;

    lastRenderedReceivedAtMs_ =
        snapshot.receivedAtMs;
}

void S3LCDManager::rememberHeaderState(
    bool bleConnected,
    bool wifiConnected)
{
    renderedHeaderStateValid_ =
        true;

    lastRenderedBleConnected_ =
        bleConnected;

    lastRenderedWifiConnected_ =
        wifiConnected;
}

void S3LCDManager::rememberHeaderState(
    bool bleConnected,
    bool wifiConnected,
    const S3CloudRuntimeStatus &cloudStatus)
{
    rememberHeaderState(
        bleConnected,
        wifiConnected);

    lastRenderedRealtimeState_ =
        cloudStatus.realtime;

    lastRenderedHistoricalState_ =
        cloudStatus.historical;
}

void S3LCDManager::rememberTinyMLState(
    bool tinyMLReady)
{
    renderedTinyMLStateValid_ =
        true;

    lastRenderedTinyMLReady_ =
        tinyMLReady;
}

void S3LCDManager::rememberFooterState(
    const S3SensorSnapshot &snapshot,
    uint32_t validPacketCount,
    uint32_t crcFailures,
    uint32_t packetAgeMs,
    uint32_t nowMs)
{
    renderedFooterStateValid_ =
        true;

    lastFooterRefreshMs_ =
        nowMs;

    lastRenderedValidPacketCount_ =
        validPacketCount;

    lastRenderedCrcFailures_ =
        crcFailures;

    lastRenderedPacketAgeMs_ =
        packetAgeMs;

    lastRenderedFooterSequence_ =
        snapshot.sequence;

    lastRenderedQualityPct_ =
        snapshot.qualityScorePct;
}

// ============================================================
// PAGE TRANSITION
//
// Only the 3-pixel header rail animates.
// ============================================================

void S3LCDManager::updateTransition(
    uint32_t nowMs)
{
    if (!transitionActive_)
    {
        return;
    }

    const uint32_t elapsed =
        nowMs -
        transitionStartMs_;

    if (elapsed >= TRANSITION_DURATION_MS)
    {
        drawFerrariRail(
            currentPage_,
            SCREEN_W,
            false);

        transitionActive_ =
            false;

        previousTransitionWidth_ =
            SCREEN_W;

        return;
    }

    const int16_t newWidth =
        static_cast<int16_t>(
            (static_cast<uint32_t>(SCREEN_W) *
             elapsed) /
            TRANSITION_DURATION_MS);

    if (newWidth > previousTransitionWidth_)
    {
        drawFerrariRail(
            currentPage_,
            newWidth,
            true);

        previousTransitionWidth_ =
            newWidth;
    }
}

// ============================================================
// MAIN RENDER
// ============================================================

void S3LCDManager::render(
    const S3SensorSnapshot &snapshot,
    const S3FeatureData &features,
    const S3TDSResult &tdsResult,
    bool haveSensorData,
    bool bleConnected,
    bool wifiConnected,
    bool tinyMLReady,
    uint32_t validPacketCount,
    uint32_t crcFailures,
    uint32_t packetAgeMs)
{
    S3CloudRuntimeStatus cloudStatus;

    render(
        snapshot,
        features,
        tdsResult,
        haveSensorData,
        bleConnected,
        wifiConnected,
        cloudStatus,
        tinyMLReady,
        validPacketCount,
        crcFailures,
        packetAgeMs);
}

void S3LCDManager::render(
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
    uint32_t packetAgeMs)
{
    if (!initialized_)
    {
        return;
    }

    const uint32_t nowMs =
        millis();

    // --------------------------------------------------------
    // PAGE CHANGE
    //
    // Redraw the static page once, then paint the current
    // genuine dynamic state once.  All change trackers are
    // synchronized afterwards.
    // --------------------------------------------------------

    if (pageDirty_)
    {
        switch (currentPage_)
        {
        case 0:
            drawRealSensorStatic();
            break;

        case 1:
            drawAIStatic();
            break;

        case 2:
            drawDerivedStatic();
            break;

        default:
            currentPage_ =
                0;

            drawRealSensorStatic();
            break;
        }

        switch (currentPage_)
        {
        case 0:
            updateRealSensorValues(
                snapshot,
                haveSensorData);
            break;

        case 1:
            updateAIValues(
                snapshot,
                tdsResult,
                tinyMLReady);
            break;

        case 2:
            updateDerivedValues(
                features);
            break;
        }

        updateHeaderStatus(
            bleConnected,
            wifiConnected,
            cloudStatus);

        updateFooter(
            snapshot,
            validPacketCount,
            crcFailures,
            packetAgeMs);

        rememberSourceFrame(
            snapshot,
            haveSensorData);

        rememberHeaderState(
            bleConnected,
            wifiConnected,
            cloudStatus);

        rememberTinyMLState(
            tinyMLReady);

        rememberFooterState(
            snapshot,
            validPacketCount,
            crcFailures,
            packetAgeMs,
            nowMs);

        pageDirty_ =
            false;

        transitionActive_ =
            true;

        transitionStartMs_ =
            nowMs;

        previousTransitionWidth_ =
            0;
    }
    else
    {
        const bool sourceChanged =
            sourceFrameChanged(
                snapshot,
                haveSensorData);

        const bool headerChanged =
            headerStateChanged(
                bleConnected,
                wifiConnected,
                cloudStatus);

        const bool tinyChanged =
            tinyMLStateChanged(
                tinyMLReady);

        const bool footerChanged =
            footerStateChanged(
                snapshot,
                validPacketCount,
                crcFailures,
                packetAgeMs,
                nowMs);

        // ----------------------------------------------------
        // MAIN CARDS
        //
        // Redraw ONLY for a new genuine C3 source frame,
        // availability transition, or Page-2 TinyML readiness
        // transition.  There is no blind 100-ms repaint.
        // ----------------------------------------------------

        if (sourceChanged ||
            (currentPage_ == 1 &&
             tinyChanged))
        {
            switch (currentPage_)
            {
            case 0:
                updateRealSensorValues(
                    snapshot,
                    haveSensorData);
                break;

            case 1:
                updateAIValues(
                    snapshot,
                    tdsResult,
                    tinyMLReady);
                break;

            case 2:
                updateDerivedValues(
                    features);
                break;
            }
        }

        if (sourceChanged)
        {
            rememberSourceFrame(
                snapshot,
                haveSensorData);
        }

        if (tinyChanged)
        {
            rememberTinyMLState(
                tinyMLReady);
        }

        // ----------------------------------------------------
        // HEADER
        // ----------------------------------------------------

        if (headerChanged)
        {
            updateHeaderStatus(
                bleConnected,
                wifiConnected,
                cloudStatus);

            rememberHeaderState(
                bleConnected,
                wifiConnected,
                cloudStatus);
        }

        // ----------------------------------------------------
        // FOOTER
        //
        // AGE may refresh at a slow independent cadence, but
        // this never clears or repaints the sensor cards.
        // ----------------------------------------------------

        if (footerChanged)
        {
            updateFooter(
                snapshot,
                validPacketCount,
                crcFailures,
                packetAgeMs);

            rememberFooterState(
                snapshot,
                validPacketCount,
                crcFailures,
                packetAgeMs,
                nowMs);
        }
    }

    // Preserve the original subtle page-transition rail.
    updateTransition(
        nowMs);
}
