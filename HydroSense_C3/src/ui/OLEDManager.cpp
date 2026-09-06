#include "ui/OLEDManager.h"

// ============================================================
// HYDROSENSE ESP32-C3
// FINAL SH1106 OLED MANAGER
//
// Display : SH1106 128x64
// Driver  : Adafruit SH110X
// Address : 0x3C
//
// IMPORTANT:
// This uses the same initialization sequence that was already
// proven to work with this physical OLED.
//
// The raw I2C probe is DIAGNOSTIC ONLY.
// It does NOT prevent display.begin() from being attempted.
// ============================================================

// ============================================================
// CONSTRUCTOR
// ============================================================

OLEDManager::OLEDManager(
    uint8_t sdaPin,
    uint8_t sclPin,
    uint8_t i2cAddress)
    : sda(sdaPin),
      scl(sclPin),
      address(i2cAddress),
      healthy(false),
      display(
          DISPLAY_WIDTH,
          DISPLAY_HEIGHT,
          &Wire,
          -1)
{
}

// ============================================================
// I2C PROBE
// ============================================================

bool OLEDManager::probeDisplay()
{
    Wire.beginTransmission(
        address);

    uint8_t result =
        Wire.endTransmission();

    return result == 0;
}

// ============================================================
// BEGIN
// ============================================================

bool OLEDManager::begin()
{
    healthy = false;

    Serial.println();
    Serial.println(
        "[ OLED MANAGER ]");

    Serial.print(
        "SDA GPIO             : ");

    Serial.println(
        sda);

    Serial.print(
        "SCL GPIO             : ");

    Serial.println(
        scl);

    Serial.print(
        "OLED Address         : 0x");

    Serial.println(
        address,
        HEX);

    // ========================================================
    // 1. START I2C
    //
    // This reproduces the known-working configuration.
    // ========================================================

    Serial.println(
        "Starting I2C...");

    bool wireOK =
        Wire.begin(
            sda,
            scl);

    Serial.print(
        "Wire.begin()         : ");

    Serial.println(
        wireOK
            ? "SUCCESS"
            : "FAILED");

    if (!wireOK)
    {
        Serial.println(
            "OLED RESULT          : I2C START FAILED");

        return false;
    }

    Wire.setClock(
        100000UL);

    // Give OLED controller and bus time to settle.
    delay(
        300);

    // ========================================================
    // 2. RAW ACK TEST
    //
    // IMPORTANT:
    // This is diagnostic only.
    //
    // Even if this says NO ACK, we STILL call display.begin().
    // ========================================================

    bool rawAck =
        probeDisplay();

    Serial.print(
        "Raw I2C ACK          : ");

    Serial.println(
        rawAck
            ? "YES"
            : "NO");

    // ========================================================
    // 3. ADAFRUIT SH1106 INITIALIZATION
    //
    // This is the authoritative test.
    // ========================================================

    Serial.println(
        "Starting SH1106...");

    bool displayOK =
        display.begin(
            address,
            true);

    Serial.print(
        "display.begin()      : ");

    Serial.println(
        displayOK
            ? "SUCCESS"
            : "FAILED");

    if (!displayOK)
    {
        Serial.println(
            "OLED RESULT          : SH1106 INIT FAILED");

        healthy =
            false;

        return false;
    }

    // ========================================================
    // 4. DISPLAY CONFIGURATION
    // ========================================================

    healthy =
        true;

    display.clearDisplay();

    display.setTextWrap(
        false);

    display.setTextColor(
        SH110X_WHITE);

    display.setTextSize(
        1);

    // ========================================================
    // 5. FORCED VISIBLE TEST SCREEN
    //
    // This appears BEFORE AnimationEngine or
    // C3DisplayController gets involved.
    // ========================================================

    display.clearDisplay();

    display.drawRect(
        0,
        0,
        128,
        64,
        SH110X_WHITE);

    display.setTextSize(
        1);

    display.setCursor(
        25,
        10);

    display.print(
        "HYDROSENSE C3");

    display.setCursor(
        28,
        27);

    display.print(
        "OLED ONLINE");

    display.setCursor(
        26,
        44);

    display.print(
        "SH1106 0x3C");

    display.fillCircle(
        64,
        57,
        2,
        SH110X_WHITE);

    display.display();

    Serial.println(
        "Test frame           : SENT");

    Serial.println(
        "OLED RESULT          : ONLINE");

    // Keep the diagnostic screen visible long enough
    // to verify visually.
    delay(
        1500);

    return true;
}

// ============================================================
// HEALTH
// ============================================================

bool OLEDManager::isHealthy() const
{
    return healthy;
}

// ============================================================
// CLEAR BUFFER
// ============================================================

void OLEDManager::clear()
{
    if (!healthy)
    {
        return;
    }

    display.clearDisplay();
}

// ============================================================
// SEND FRAMEBUFFER
// ============================================================

void OLEDManager::present()
{
    if (!healthy)
    {
        return;
    }

    display.display();
}

// ============================================================
// TEXT
// ============================================================

void OLEDManager::drawText(
    int16_t x,
    int16_t y,
    const char *text,
    uint8_t size)
{
    if (
        !healthy ||
        text == nullptr)
    {
        return;
    }

    display.setTextSize(
        size);

    display.setTextColor(
        SH110X_WHITE);

    display.setCursor(
        x,
        y);

    display.print(
        text);
}

// ============================================================
// CENTERED TEXT
// ============================================================

void OLEDManager::drawTextCentered(
    int16_t y,
    const char *text,
    uint8_t size)
{
    if (
        !healthy ||
        text == nullptr)
    {
        return;
    }

    int16_t x1 =
        0;

    int16_t y1 =
        0;

    uint16_t textWidthValue =
        0;

    uint16_t textHeightValue =
        0;

    display.setTextSize(
        size);

    display.getTextBounds(
        text,
        0,
        y,
        &x1,
        &y1,
        &textWidthValue,
        &textHeightValue);

    int16_t x =
        (static_cast<int16_t>(
             DISPLAY_WIDTH) -
         static_cast<int16_t>(
             textWidthValue)) /
        2;

    if (x < 0)
    {
        x = 0;
    }

    drawText(
        x,
        y,
        text,
        size);
}

// ============================================================
// TEXT WIDTH
// ============================================================

int16_t OLEDManager::textWidth(
    const char *text,
    uint8_t size)
{
    if (
        !healthy ||
        text == nullptr)
    {
        return 0;
    }

    int16_t x1 =
        0;

    int16_t y1 =
        0;

    uint16_t textWidthValue =
        0;

    uint16_t textHeightValue =
        0;

    display.setTextSize(
        size);

    display.getTextBounds(
        text,
        0,
        0,
        &x1,
        &y1,
        &textWidthValue,
        &textHeightValue);

    return static_cast<int16_t>(
        textWidthValue);
}

// ============================================================
// ICON
// ============================================================

void OLEDManager::drawIcon(
    int16_t x,
    int16_t y,
    uint8_t iconWidth,
    uint8_t iconHeight,
    const uint8_t *bitmap)
{
    if (
        !healthy ||
        bitmap == nullptr)
    {
        return;
    }

    display.drawBitmap(
        x,
        y,
        bitmap,
        iconWidth,
        iconHeight,
        SH110X_WHITE);
}

// ============================================================
// OUTLINE RECTANGLE
// ============================================================

void OLEDManager::drawFrame(
    int16_t x,
    int16_t y,
    int16_t frameWidth,
    int16_t frameHeight)
{
    if (!healthy)
    {
        return;
    }

    display.drawRect(
        x,
        y,
        frameWidth,
        frameHeight,
        SH110X_WHITE);
}

// ============================================================
// FILLED RECTANGLE
// ============================================================

void OLEDManager::drawBox(
    int16_t x,
    int16_t y,
    int16_t boxWidth,
    int16_t boxHeight)
{
    if (!healthy)
    {
        return;
    }

    display.fillRect(
        x,
        y,
        boxWidth,
        boxHeight,
        SH110X_WHITE);
}

// ============================================================
// HORIZONTAL LINE
// ============================================================

void OLEDManager::drawHLine(
    int16_t x,
    int16_t y,
    int16_t lineWidth)
{
    if (!healthy)
    {
        return;
    }

    display.drawFastHLine(
        x,
        y,
        lineWidth,
        SH110X_WHITE);
}

// ============================================================
// VERTICAL LINE
// ============================================================

void OLEDManager::drawVLine(
    int16_t x,
    int16_t y,
    int16_t lineHeight)
{
    if (!healthy)
    {
        return;
    }

    display.drawFastVLine(
        x,
        y,
        lineHeight,
        SH110X_WHITE);
}

// ============================================================
// GENERAL LINE
// ============================================================

void OLEDManager::drawLine(
    int16_t x1,
    int16_t y1,
    int16_t x2,
    int16_t y2)
{
    if (!healthy)
    {
        return;
    }

    display.drawLine(
        x1,
        y1,
        x2,
        y2,
        SH110X_WHITE);
}

// ============================================================
// OUTLINE CIRCLE
// ============================================================

void OLEDManager::drawCircle(
    int16_t x,
    int16_t y,
    int16_t radius)
{
    if (!healthy)
    {
        return;
    }

    display.drawCircle(
        x,
        y,
        radius,
        SH110X_WHITE);
}

// ============================================================
// FILLED CIRCLE
// ============================================================

void OLEDManager::drawDisc(
    int16_t x,
    int16_t y,
    int16_t radius)
{
    if (!healthy)
    {
        return;
    }

    display.fillCircle(
        x,
        y,
        radius,
        SH110X_WHITE);
}

// ============================================================
// DISPLAY WIDTH
// ============================================================

uint16_t OLEDManager::width() const
{
    return DISPLAY_WIDTH;
}

// ============================================================
// DISPLAY HEIGHT
// ============================================================

uint16_t OLEDManager::height() const
{
    return DISPLAY_HEIGHT;
}