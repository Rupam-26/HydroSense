#include "input/ButtonManager.h"

// ============================================================
// HYDROSENSE ESP32-C3
// BUTTON MANAGER IMPLEMENTATION
//
// Wiring:
//
// GPIO3 ---- PUSH BUTTON ---- GND
//
// INPUT_PULLUP:
//
// Released = HIGH
// Pressed  = LOW
//
// Features:
// - debounce
// - falling-edge press detection
// - one event per press
// - no repetition while held
// - press counter
// - raw electrical diagnostic
// ============================================================

// ============================================================
// CONSTRUCTOR
// ============================================================

ButtonManager::ButtonManager(
    uint8_t pin,
    uint32_t debounceMs)
    : buttonPin(pin),
      debounceIntervalMs(debounceMs),
      rawState(HIGH),
      stableState(HIGH),
      previousStableState(HIGH),
      lastRawChangeMs(0),
      pressEvent(false),
      totalPresses(0)
{
}

// ============================================================
// BEGIN
// ============================================================

void ButtonManager::begin()
{
    // Internal pull-up:
    //
    // Released -> HIGH
    // Pressed  -> LOW

    pinMode(
        buttonPin,
        INPUT_PULLUP);

    delay(
        10);

    // Read actual initial state.

    rawState =
        digitalRead(
            buttonPin);

    stableState =
        rawState;

    previousStableState =
        stableState;

    lastRawChangeMs =
        millis();

    pressEvent =
        false;

    totalPresses =
        0;
}

// ============================================================
// UPDATE
//
// Call continuously from loop().
// ============================================================

void ButtonManager::update(
    uint32_t nowMs)
{
    const bool currentRawState =
        digitalRead(
            buttonPin);

    // ========================================================
    // RAW GPIO TRANSITION
    // ========================================================

    if (
        currentRawState !=
        rawState)
    {
        rawState =
            currentRawState;

        lastRawChangeMs =
            nowMs;
    }

    // ========================================================
    // DEBOUNCE
    //
    // Wait until raw signal remains unchanged for the required
    // debounce period.
    // ========================================================

    if (
        (
            nowMs -
            lastRawChangeMs) <
        debounceIntervalMs)
    {
        return;
    }

    // ========================================================
    // NEW STABLE STATE
    // ========================================================

    if (
        stableState !=
        rawState)
    {
        previousStableState =
            stableState;

        stableState =
            rawState;

        // ====================================================
        // PRESS EVENT
        //
        // HIGH -> LOW
        //
        // Falling edge means physical button press.
        // ====================================================

        if (
            previousStableState == HIGH &&
            stableState == LOW)
        {
            pressEvent =
                true;

            totalPresses++;
        }
    }
}

// ============================================================
// CONSUME PRESS EVENT
//
// Returns true exactly once for every accepted press.
// ============================================================

bool ButtonManager::consumePress()
{
    if (!pressEvent)
    {
        return false;
    }

    pressEvent =
        false;

    return true;
}

// ============================================================
// DEBOUNCED BUTTON STATE
// ============================================================

bool ButtonManager::isPressed() const
{
    return stableState == LOW;
}

// ============================================================
// PRESS COUNTER
// ============================================================

uint32_t ButtonManager::pressCount() const
{
    return totalPresses;
}

// ============================================================
// RAW GPIO LEVEL
//
// true  -> HIGH
// false -> LOW
// ============================================================

bool ButtonManager::rawLevelHigh() const
{
    return rawState == HIGH;
}