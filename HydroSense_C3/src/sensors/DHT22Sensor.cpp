#include "sensors/DHT22Sensor.h"

DHT22Sensor::DHT22Sensor(uint8_t pin)
    : dht(pin, DHT22),
      temperature(NAN),
      humidity(NAN),
      healthy(false)
{
}

void DHT22Sensor::begin()
{
    dht.begin();
}

bool DHT22Sensor::validTemperature(float value)
{
    if (!isfinite(value))
    {
        return false;
    }

    return value >= -40.0f &&
           value <= 80.0f;
}

bool DHT22Sensor::validHumidity(float value)
{
    if (!isfinite(value))
    {
        return false;
    }

    return value >= 0.0f &&
           value <= 100.0f;
}

bool DHT22Sensor::read()
{
    const float newHumidity =
        dht.readHumidity();

    const float newTemperature =
        dht.readTemperature();

    if (!validTemperature(newTemperature) ||
        !validHumidity(newHumidity))
    {
        // Preserve the previous genuine measurement exactly as the
        // confirmed first testing implementation did.  The false
        // return and healthy=false still report that THIS acquisition
        // failed; SensorHealthManager decides whether the previous
        // genuine sample is still fresh enough to use.
        healthy = false;
        return false;
    }

    temperature = newTemperature;
    humidity = newHumidity;
    healthy = true;

    return true;
}

float DHT22Sensor::temperatureC() const
{
    return temperature;
}

float DHT22Sensor::humidityPct() const
{
    return humidity;
}

bool DHT22Sensor::isHealthy() const
{
    return healthy;
}
