#pragma once

#include <Arduino.h>

// ============================================================
// HYDROSENSE
// ULTRASONIC PHYSICS PROCESSING
//
// RESPONSIBILITY:
// - Calculate temperature-dependent speed of sound
// - Convert echo duration to temperature-corrected distance
//
// DOES NOT:
// - Read HC-SR04 hardware
// - Read DHT22
// - Filter measurements
// - Calculate reservoir level
// - Perform calibration
//
// INPUTS:
// - Air temperature (C)
// - Echo duration (microseconds)
//
// OUTPUTS:
// - Speed of sound (m/s)
// - Corrected distance (cm)
// ============================================================

class UltrasonicPhysics
{
public:
    // --------------------------------------------------------
    // Calculate speed of sound in dry air.
    //
    // Approximation:
    //
    // c = 331.3 + 0.606 * T
    //
    // where:
    // c = speed of sound in m/s
    // T = air temperature in Celsius
    // --------------------------------------------------------

    static float speedOfSoundMps(
        float airTemperatureC);

    // --------------------------------------------------------
    // Convert genuine echo duration to corrected distance.
    //
    // HC-SR04 pulse represents round-trip travel:
    //
    // sensor -> target -> sensor
    //
    // therefore distance is divided by 2.
    //
    // Returns NAN when input is invalid.
    // --------------------------------------------------------

    static float correctedDistanceCm(
        uint32_t echoTimeUs,
        float airTemperatureC);

    // --------------------------------------------------------
    // Input validation
    // --------------------------------------------------------

    static bool validAirTemperature(
        float airTemperatureC);
};