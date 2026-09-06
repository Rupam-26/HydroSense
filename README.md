# HydroSense

<p align="center">
  <img src="https://img.shields.io/badge/ESP32-C3-E7352C?logo=espressif&logoColor=white" alt="ESP32-C3">
  <img src="https://img.shields.io/badge/ESP32-S3-E7352C?logo=espressif&logoColor=white" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/PlatformIO-Orange?logo=platformio&logoColor=white" alt="PlatformIO">
  <img src="https://img.shields.io/badge/Node.js-Backend-339933?logo=node.js&logoColor=white" alt="Node.js">
  <img src="https://img.shields.io/badge/Python-ML-3776AB?logo=python&logoColor=white" alt="Python">
  <img src="https://img.shields.io/badge/MongoDB-Telemetry-47A248?logo=mongodb&logoColor=white" alt="MongoDB">
  <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="MIT License">
</p>

HydroSense is a modular IoT water-monitoring and Edge-AI system built using an ESP32-C3 sensor node, an ESP32-S3 processing and display node, and a Node.js backend.

The system collects environmental and reservoir measurements, validates sensor data, transfers compact telemetry over Bluetooth Low Energy, estimates Total Dissolved Solids using an INT8 TinyML soft sensor, displays live information, stores historical telemetry, and provides a real-time web dashboard.

## System Architecture

```text
DHT22 ───────────────┐
DS18B20 ─────────────┤
HC-SR04 ─────────────┤
                     ▼
              ESP32-C3 Sensor Node
              • Sensor acquisition
              • Filtering
              • Calibration
              • Data-quality analysis
              • BLE packet generation
                     │
                     │ Bluetooth Low Energy
                     ▼
              ESP32-S3 Edge Node
              • BLE packet validation
              • Derived features
              • INT8 TinyML inference
              • Sensor-trust analysis
              • LCD visualization
              • Wi-Fi communication
                     │
                     │ HTTP / JSON
                     ▼
              HydroSense Backend
              • REST API
              • Socket.IO realtime stream
              • MongoDB historical storage
              • Web dashboard
              • ML baseline services