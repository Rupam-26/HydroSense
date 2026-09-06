#pragma once

#include <Arduino.h>
#include <DHT.h>

class DHT22Sensor
{
public:
    explicit DHT22Sensor(uint8_t pin);

    void begin();

    bool read();

    float temperatureC() const;

    float humidityPct() const;

    bool isHealthy() const;

private:
    DHT dht;

    float temperature;
    float humidity;

    bool healthy;

    bool validTemperature(float value);

    bool validHumidity(float value);
};