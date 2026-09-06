#include "sensors/DS18B20Sensor.h"

#include <math.h>
#include <string.h>

DS18B20Sensor::DS18B20Sensor(uint8_t pin)
    : oneWire(pin),
      sensors(&oneWire),
      deviceAddress{},
      deviceAddressKnown(false),
      temperature(NAN),
      healthy(false),
      detectedDevices(0),
      pending(false),
      conversionStartedMs(0),
      requiredConversionMs(
          DallasTemperature::millisToWaitForConversion(
              RESOLUTION_BITS))
{
}

void DS18B20Sensor::invalidateDeviceIdentity()
{
    memset(
        deviceAddress,
        0,
        sizeof(deviceAddress));

    deviceAddressKnown =
        false;

    detectedDevices =
        0;
}

bool DS18B20Sensor::discoverAndConfigureDevice()
{
    // Do a direct OneWire ROM search instead of DallasTemperature::begin().
    // DallasTemperature::begin() intentionally sleeps between discovery
    // retries; that is acceptable at boot but undesirable during a live
    // 250-ms sonar schedule. A direct ROM search has no fixed retry delay.
    sensors.setWaitForConversion(
        false);

    oneWire.reset_search();

    DeviceAddress firstDS18B20{};

    uint8_t matchingDevices =
        0;

    DeviceAddress candidate{};

    while (oneWire.search(
        candidate))
    {
        if (OneWire::crc8(
                candidate,
                7) !=
            candidate[7])
        {
            continue;
        }

        // HydroSense's submerged water probe is specifically a DS18B20.
        // Do not silently bind to another OneWire device family.
        if (candidate[0] !=
            DS18B20_FAMILY_CODE)
        {
            continue;
        }

        if (matchingDevices == 0)
        {
            memcpy(
                firstDS18B20,
                candidate,
                sizeof(firstDS18B20));
        }

        ++matchingDevices;
    }

    oneWire.reset_search();

    detectedDevices =
        matchingDevices;

    // The final C3 hardware architecture contains exactly one submerged
    // water-temperature probe. Reject an ambiguous multi-probe bus rather
    // than allowing index 0/search order to silently change sensor identity.
    if (matchingDevices != 1)
    {
        deviceAddressKnown =
            false;

        healthy =
            false;

        return false;
    }

    memcpy(
        deviceAddress,
        firstDS18B20,
        sizeof(deviceAddress));

    // The final HydroSense wiring uses a three-wire, externally powered
    // DS18B20 (VDD=3.3 V). Confirm that here. The asynchronous architecture
    // deliberately does not accept parasite-power operation because that
    // would require a different strong-pull-up ownership policy.
    if (oneWire.reset() == 0)
    {
        invalidateDeviceIdentity();
        healthy = false;
        return false;
    }

    oneWire.select(
        deviceAddress);

    oneWire.write(
        READ_POWER_SUPPLY_COMMAND);

    const bool externallyPowered =
        oneWire.read_bit() != 0;

    oneWire.reset();

    if (!externallyPowered)
    {
        invalidateDeviceIdentity();
        healthy = false;
        return false;
    }

    // Force and verify the accepted 12-bit configuration. If the sensor is
    // already 12-bit, DallasTemperature does not rewrite its configuration.
    if (!sensors.setResolution(
            deviceAddress,
            RESOLUTION_BITS))
    {
        invalidateDeviceIdentity();

        healthy =
            false;

        return false;
    }

    const uint8_t confirmedResolution =
        sensors.getResolution(
            deviceAddress);

    if (confirmedResolution !=
        RESOLUTION_BITS)
    {
        invalidateDeviceIdentity();

        healthy =
            false;

        return false;
    }

    requiredConversionMs =
        DallasTemperature::millisToWaitForConversion(
            confirmedResolution);

    deviceAddressKnown =
        requiredConversionMs > 0;

    if (!deviceAddressKnown)
    {
        invalidateDeviceIdentity();

        healthy =
            false;

        return false;
    }

    return true;
}

void DS18B20Sensor::begin()
{
    pending =
        false;

    conversionStartedMs =
        0;

    requiredConversionMs =
        DallasTemperature::millisToWaitForConversion(
            RESOLUTION_BITS);

    healthy =
        false;

    invalidateDeviceIdentity();

    // Startup discovery uses the same direct ROM-search path as runtime
    // recovery. Runtime calls are additionally sonar-window guarded.
    discoverAndConfigureDevice();
}

bool DS18B20Sensor::validTemperature(
    float value) const
{
    if (!isfinite(value))
    {
        return false;
    }

    if (value ==
        DEVICE_DISCONNECTED_C)
    {
        return false;
    }

    // Reject the well-known DS18B20 power-up/default value. HydroSense is
    // a hydroponic-water application, so 85 C is not a plausible valid
    // operating water sample and must never be mistaken for one.
    if (fabsf(
            value - 85.0f) <
        0.01f)
    {
        return false;
    }

    return value >= -55.0f &&
           value <= 125.0f;
}

bool DS18B20Sensor::requestConversion()
{
    if (pending)
    {
        return false;
    }

    if (!deviceAddressKnown)
    {
        if (!discoverAndConfigureDevice())
        {
            healthy =
                false;

            return false;
        }
    }

    // Addressed conversion is intentionally used instead of the broadcast
    // request. DallasTemperature validates that this exact configured ROM
    // address is still reachable before starting the conversion.
    const DallasTemperature::request_t request =
        sensors.requestTemperaturesByAddress(
            deviceAddress);

    if (!request.result)
    {
        healthy =
            false;

        invalidateDeviceIdentity();

        return false;
    }

    // Crucial Block-3 fix: use the timestamp captured by DallasTemperature
    // immediately after it actually issues START CONVERSION. An earlier
    // scheduler timestamp could permit a boundary read a millisecond early.
    conversionStartedMs =
        request.timestamp;

    pending =
        true;

    return true;
}

bool DS18B20Sensor::conversionReady(
    uint32_t nowMs) const
{
    if (!pending)
    {
        return false;
    }

    // Unsigned subtraction is intentionally millis()-wrap safe.
    return nowMs - conversionStartedMs >=
           requiredConversionMs;
}

bool DS18B20Sensor::readConversion(
    uint32_t nowMs)
{
    if (!conversionReady(
            nowMs))
    {
        return false;
    }

    // One additional scratchpad retry is allowed for a transient OneWire
    // CRC/read disturbance. No new temperature is fabricated and no new
    // conversion is started by this retry.
    const float newTemperature =
        sensors.getTempC(
            deviceAddress,
            1);

    pending =
        false;

    if (!validTemperature(
            newTemperature))
    {
        healthy =
            false;

        // A Dallas disconnected result means the configured ROM address can
        // no longer be trusted as physically present. Force a genuine bus
        // discovery before the next conversion request. For a coherent but
        // application-invalid value (such as power-up 85 C), retain identity
        // and simply reject this sample.
        if (newTemperature ==
            DEVICE_DISCONNECTED_C)
        {
            invalidateDeviceIdentity();
        }

        // Preserve the previous genuine temperature for diagnostics/history.
        // SensorHealthManager decides whether that retained sample is still
        // fresh enough to be usable.
        return false;
    }

    temperature =
        newTemperature;

    healthy =
        true;

    return true;
}

bool DS18B20Sensor::conversionPending() const
{
    return pending;
}

uint32_t DS18B20Sensor::conversionStartedAtMs() const
{
    return conversionStartedMs;
}

uint16_t DS18B20Sensor::conversionTimeMs() const
{
    return requiredConversionMs;
}

uint8_t DS18B20Sensor::resolutionBits() const
{
    return RESOLUTION_BITS;
}

float DS18B20Sensor::temperatureC() const
{
    return temperature;
}

bool DS18B20Sensor::isHealthy() const
{
    return healthy;
}

uint8_t DS18B20Sensor::deviceCount() const
{
    return detectedDevices;
}
