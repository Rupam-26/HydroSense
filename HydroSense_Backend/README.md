# HydroSense Backend

![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![Node.js](https://img.shields.io/badge/Node.js-%3E%3D20.19.0-339933?logo=node.js&logoColor=white)
![Express](https://img.shields.io/badge/Express-5.x-000000?logo=express)
![MongoDB](https://img.shields.io/badge/MongoDB-Atlas-47A248?logo=mongodb&logoColor=white)
![Socket.IO](https://img.shields.io/badge/Socket.IO-Realtime-010101?logo=socket.io)
![ESP32-S3](https://img.shields.io/badge/Device-ESP32--S3-E7352C?logo=espressif)
![Status](https://img.shields.io/badge/Status-Research%20Prototype-orange)

HydroSense Backend is the server, database, realtime communication, monitoring dashboard, Sensor Trust, and machine-learning control component of the complete HydroSense system.

The complete HydroSense project contains:

- `HydroSense_C3` — sensor acquisition, filtering, analytics, OLED display, and BLE transmission.
- `HydroSense_S3` — BLE reception, LCD display, TinyML TDS estimation, Wi-Fi, and cloud upload.
- `HydroSense_Backend` — MongoDB storage, realtime monitoring, Sensor Trust events, ML experiments, and the browser Control Room.

## System flow

```text
Physical Sensors
      |
      v
ESP32-C3
      |
      | BLE
      v
ESP32-S3
      |
      | Wi-Fi HTTP
      v
HydroSense Backend
      |
      +--> MongoDB historical telemetry
      |
      +--> Socket.IO realtime telemetry
      |
      +--> Sensor Trust event history
      |
      +--> Machine-learning control and experiments
      |
      +--> Browser Control Room