#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================
// HYDROSENSE
// DS18B20 WATER-TEMPERATURE DRIVER
//
// Final Block-3 design:
// - exactly one physical DS18B20 water probe is expected;
// - the probe is identified by its 64-bit OneWire ROM address;
// - 12-bit resolution is retained;
// - conversion is asynchronous/non-blocking;
// - conversion timing is anchored to the Dallas library's actual
//   START-CONVERSION timestamp, not to an earlier scheduler time;
// - a disconnected probe is invalidated and genuinely rediscovered
//   before another conversion is accepted;
// - the last genuine temperature is retained across transient faults,
//   while SensorHealthManager owns freshness/usability policy.
// ============================================================

class DS18B20Sensor
{
public:
    explicit DS18B20Sensor(uint8_t pin);

    void begin();

    // Start one genuine physical conversion. If the probe identity is
    // currently unknown, this performs a real OneWire discovery first.
    // Runtime callers must launch this only from a sonar-safe window.
    bool requestConversion();

    // True only after the full physical conversion time has elapsed from
    // the actual DallasTemperature START-CONVERSION timestamp.
    bool conversionReady(uint32_t nowMs) const;

    // Read a completed conversion. Returns true only for a genuine,
    // validated DS18B20 temperature. The last genuine stored temperature
    // is not erased by a failed acquisition.
    bool readConversion(uint32_t nowMs);

    bool conversionPending() const;

    uint32_t conversionStartedAtMs() const;

    uint16_t conversionTimeMs() const;

    uint8_t resolutionBits() const;

    float temperatureC() const;

    bool isHealthy() const;

    // Number of DS18B20-family probes found by the most recent real bus
    // discovery. HydroSense requires exactly one water-temperature probe.
    uint8_t deviceCount() const;

private:
    static constexpr uint8_t RESOLUTION_BITS = 12;
    static constexpr uint8_t DS18B20_FAMILY_CODE = 0x28;
    static constexpr uint8_t READ_POWER_SUPPLY_COMMAND = 0xB4;

    OneWire oneWire;
    DallasTemperature sensors;

    DeviceAddress deviceAddress;
    bool deviceAddressKnown;

    float temperature;
    bool healthy;

    uint8_t detectedDevices;

    bool pending;
    uint32_t conversionStartedMs;
    uint16_t requiredConversionMs;

    bool validTemperature(float value) const;

    bool discoverAndConfigureDevice();

    void invalidateDeviceIdentity();
};
