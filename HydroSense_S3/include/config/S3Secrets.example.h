#pragma once

/*
 * SPDX-FileCopyrightText: 2026 Rupam Mondal
 * SPDX-License-Identifier: MIT
 *
 * Public configuration example.
 * Copy this file as S3Secrets.h and enter your private details.
 * Never place real passwords in this example file.
 */

namespace HydroSenseConfig
{
    constexpr char WIFI_SSID[] =
        "YOUR_WIFI_NAME";

    constexpr char WIFI_PASSWORD[] =
        "YOUR_WIFI_PASSWORD";

    constexpr char CLOUD_TELEMETRY_URL[] =
        "http://YOUR_BACKEND_IP:5000/api/v1/telemetry";

    constexpr char CLOUD_REALTIME_URL[] =
        "http://YOUR_BACKEND_IP:5000/api/v1/realtime";

    constexpr char CLOUD_DEVICE_ID[] =
        "HydroSense_S3_01";
}