#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ============================================================
// HYDROSENSE
// FINAL SH1106 OLED MANAGER
//
// Display : SH1106 128x64
// Bus     : I2C
// Backend : Adafruit SH110X
//
// No sensor logic.
// No page logic.
// No button logic.
// ============================================================

class OLEDManager
{
public:
    OLEDManager(
        uint8_t sdaPin,
        uint8_t sclPin,
        uint8_t i2cAddress = 0x3C);

    bool begin();

    bool isHealthy() const;

    void clear();
    void present();

    void drawText(
        int16_t x,
        int16_t y,
        const char *text,
        uint8_t size = 1);

    void drawTextCentered(
        int16_t y,
        const char *text,
        uint8_t size = 1);

    void drawIcon(
        int16_t x,
        int16_t y,
        uint8_t width,
        uint8_t height,
        const uint8_t *bitmap);

    void drawFrame(
        int16_t x,
        int16_t y,
        int16_t width,
        int16_t height);

    void drawBox(
        int16_t x,
        int16_t y,
        int16_t width,
        int16_t height);

    void drawHLine(
        int16_t x,
        int16_t y,
        int16_t width);

    void drawVLine(
        int16_t x,
        int16_t y,
        int16_t height);

    void drawLine(
        int16_t x1,
        int16_t y1,
        int16_t x2,
        int16_t y2);

    void drawCircle(
        int16_t x,
        int16_t y,
        int16_t radius);

    void drawDisc(
        int16_t x,
        int16_t y,
        int16_t radius);

    int16_t textWidth(
        const char *text,
        uint8_t size = 1);

    uint16_t width() const;
    uint16_t height() const;

private:
    static constexpr uint16_t DISPLAY_WIDTH = 128;
    static constexpr uint16_t DISPLAY_HEIGHT = 64;

    uint8_t sda;
    uint8_t scl;
    uint8_t address;

    bool healthy;

    Adafruit_SH1106G display;

    bool probeDisplay();
};