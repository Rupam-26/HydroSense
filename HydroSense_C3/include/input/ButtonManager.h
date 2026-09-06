#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE ESP32-C3
// BUTTON MANAGER
//
// Wiring:
// GPIO3 ---- push button ---- GND
//
// Electrical mode:
// INPUT_PULLUP
//
// Released = HIGH
// Pressed  = LOW
//
// Features:
// - software debounce
// - falling-edge press detection
// - exactly one event per physical press
// - no repeated events while held
// - press counter
// - raw electrical diagnostic state
// ============================================================

class ButtonManager
{
public:
    explicit ButtonManager(
        uint8_t pin,
        uint32_t debounceMs = 35UL);

    // --------------------------------------------------------
    // Initialize GPIO
    // --------------------------------------------------------

    void begin();

    // --------------------------------------------------------
    // Must be called continuously from loop()
    // --------------------------------------------------------

    void update(
        uint32_t nowMs);

    // --------------------------------------------------------
    // Returns true ONCE for each valid press.
    //
    // Reading consumes the event.
    // --------------------------------------------------------

    bool consumePress();

    // --------------------------------------------------------
    // Debounced physical state
    // --------------------------------------------------------

    bool isPressed() const;

    // --------------------------------------------------------
    // Total number of accepted presses
    // --------------------------------------------------------

    uint32_t pressCount() const;

    // --------------------------------------------------------
    // Raw GPIO electrical level
    //
    // true  = HIGH
    // false = LOW
    // --------------------------------------------------------

    bool rawLevelHigh() const;

private:
    uint8_t buttonPin;

    uint32_t debounceIntervalMs;

    // --------------------------------------------------------
    // Raw electrical GPIO state
    // --------------------------------------------------------

    bool rawState;

    // --------------------------------------------------------
    // Debounced stable state
    // --------------------------------------------------------

    bool stableState;

    // --------------------------------------------------------
    // Previous debounced state
    // --------------------------------------------------------

    bool previousStableState;

    // --------------------------------------------------------
    // Timestamp of last raw state transition
    // --------------------------------------------------------

    uint32_t lastRawChangeMs;

    // --------------------------------------------------------
    // Pending one-shot press event
    // --------------------------------------------------------

    bool pressEvent;

    // --------------------------------------------------------
    // Accepted physical presses
    // --------------------------------------------------------

    uint32_t totalPresses;
};