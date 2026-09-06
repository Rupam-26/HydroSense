// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

// ============================================================
// HYDROSENSE DEVICE SERVICE
//
// Responsibilities:
//
// - Maintain persistent device registry in MongoDB
// - Update device state from realtime S3 telemetry
// - Update device state from historical telemetry
// - Avoid writing to MongoDB for every realtime packet
// - Combine persistent device information with live RAM state
//
// IMPORTANT:
//
// Realtime sensor values are NOT fabricated here.
// The service only records state originating from genuine
// HydroSense S3 telemetry.
// ============================================================

const Device =
    require(
        "../models/Device"
    );

const liveStateService =
    require(
        "./liveStateService"
    );

// ============================================================
// REALTIME DATABASE WRITE THROTTLE
// ============================================================

const lastRealtimeDatabaseTouch =
    new Map();

const realtimeDatabaseTouchInFlight =
    new Set();

// ============================================================
// DATABASE TOUCH INTERVAL
// ============================================================

function databaseTouchIntervalMs()
{
    const configured =
        Number(
            process.env
                .DEVICE_DB_TOUCH_INTERVAL_MS
        );

    if (
        Number.isFinite(
            configured
        ) &&
        configured >=
            1000
    )
    {
        return configured;
    }

    return 30000;
}

// ============================================================
// DETERMINE REALTIME DEVICE STATUS
// ============================================================

function realtimeStatus(
    payload
)
{
    if (
        !payload
    )
    {
        return "UNKNOWN";
    }

    if (
        payload.status ===
        "VALID"
    )
    {
        return "ONLINE";
    }

    return "DEGRADED";
}

// ============================================================
// UPDATE DEVICE FROM REALTIME TELEMETRY
// ============================================================

async function touchRealtime(
    payload,
    receivedAt
)
{
    if (
        !payload ||
        !payload.deviceId
    )
    {
        return;
    }

    const deviceId =
        payload.deviceId;

    const nowMs =
        receivedAt
            .getTime();

    const previousTouch =
        lastRealtimeDatabaseTouch
            .get(
                deviceId
            ) ||
        0;

    if (
        nowMs -
            previousTouch <
        databaseTouchIntervalMs()
    )
    {
        return;
    }

    if (
        realtimeDatabaseTouchInFlight
            .has(
                deviceId
            )
    )
    {
        return;
    }

    realtimeDatabaseTouchInFlight
        .add(
            deviceId
        );

    try
    {
        await Device.findOneAndUpdate(
            {
                deviceId:
                    deviceId
            },

            {
                $set:
                {
                    status:
                        realtimeStatus(
                            payload
                        ),

                    lastSeenAt:
                        receivedAt,

                    lastRealtimeAt:
                        receivedAt,

                    lastSequence:
                        payload.sequence,

                    lastPacketAgeMs:
                        payload.packetAgeMs,

                    lastQualityScorePct:
                        payload.quality
                            ?.scorePct ??
                        null,

                    health:
                        payload.health ||
                        {}
                },

                $setOnInsert:
                {
                    deviceId:
                        deviceId
                }
            },

            {
                upsert:
                    true,

                returnDocument:
                    "after",

                setDefaultsOnInsert:
                    true
            }
        );

        // Start throttling only after MongoDB successfully saves
        // the update. Failed writes can be retried immediately.
        lastRealtimeDatabaseTouch
            .set(
                deviceId,
                nowMs
            );
    }
    finally
    {
        realtimeDatabaseTouchInFlight
            .delete(
                deviceId
            );
    }
}

// ============================================================
// UPDATE DEVICE FROM HISTORICAL TELEMETRY
// ============================================================

async function touchHistorical(
    telemetry
)
{
    if (
        !telemetry ||
        !telemetry.deviceId
    )
    {
        return;
    }

    const receivedAt =
        telemetry.timestamp ||
        new Date();

    const deviceStatus =
        telemetry.bleConnected &&
        telemetry.wifiConnected
            ? "ONLINE"
            : "DEGRADED";

    await Device.findOneAndUpdate(
        {
            deviceId:
                telemetry.deviceId
        },

        {
            $set:
            {
                status:
                    deviceStatus,

                lastSeenAt:
                    receivedAt,

                lastHistoricalAt:
                    receivedAt,

                lastSequence:
                    telemetry.sequence,

                lastPacketAgeMs:
                    telemetry.packetAgeMs,

                lastQualityScorePct:
                    telemetry.quality
                        ?.scorePct ??
                    null,

                "health.bleConnected":
                    telemetry
                        .bleConnected,

                "health.wifiConnected":
                    telemetry
                        .wifiConnected,

                "health.packetValid":
                    true
            },

            $setOnInsert:
            {
                deviceId:
                    telemetry.deviceId
            }
        },

        {
            upsert:
                true,

            returnDocument:
                "after",

            setDefaultsOnInsert:
                true
        }
    );
}

// ============================================================
// MERGE MONGODB DEVICE + REALTIME RAM STATE
// ============================================================

function mergeDeviceWithLive(
    device
)
{
    if (
        !device ||
        !device.deviceId
    )
    {
        return null;
    }

    const live =
        liveStateService
            .getLiveState(
                device.deviceId
            );

    if (
        !live
    )
    {
        let runtimeStatus =
            device.status ||
            "UNKNOWN";

        if (
            device.lastSeenAt
        )
        {
            const lastSeenMs =
                new Date(
                    device.lastSeenAt
                )
                    .getTime();

            const ageMs =
                Date.now() -
                lastSeenMs;

            if (
                Number.isFinite(
                    ageMs
                ) &&
                ageMs >
                    liveStateService
                        .offlineTimeoutMs()
            )
            {
                runtimeStatus =
                    "OFFLINE";
            }
        }

        return {
            ...device,

            runtimeStatus:
                runtimeStatus,

            live:
                null
        };
    }

    return {
        ...device,

        runtimeStatus:
            live.backendStatus,

        lastSeenAt:
            live.receivedAt,

        live:
            live
    };
}

// ============================================================
// LIST ALL DEVICES
// ============================================================

async function listDevices()
{
    const storedDevices =
        await Device
            .find()
            .sort({
                deviceId:
                    1
            })
            .lean();

    const byId =
        new Map();

    for (
        const device
        of storedDevices
    )
    {
        const merged =
            mergeDeviceWithLive(
                device
            );

        if (
            merged
        )
        {
            byId.set(
                device.deviceId,
                merged
            );
        }
    }

    const liveStates =
        liveStateService
            .getAllLiveStates();

    for (
        const live
        of liveStates
    )
    {
        if (
            !byId.has(
                live.deviceId
            )
        )
        {
            byId.set(
                live.deviceId,
                {
                    deviceId:
                        live.deviceId,

                    displayName:
                        null,

                    locationLabel:
                        null,

                    status:
                        "UNKNOWN",

                    runtimeStatus:
                        live.backendStatus,

                    lastSeenAt:
                        live.receivedAt,

                    live:
                        live
                }
            );
        }
    }

    return Array.from(
        byId.values()
    );
}

// ============================================================
// GET ONE DEVICE
// ============================================================

async function getDevice(
    deviceId
)
{
    if (
        !deviceId
    )
    {
        return null;
    }

    const stored =
        await Device
            .findOne({
                deviceId:
                    deviceId
            })
            .lean();

    const live =
        liveStateService
            .getLiveState(
                deviceId
            );

    if (
        !stored &&
        !live
    )
    {
        return null;
    }

    if (
        !stored
    )
    {
        return {
            deviceId:
                deviceId,

            displayName:
                null,

            locationLabel:
                null,

            status:
                "UNKNOWN",

            runtimeStatus:
                live.backendStatus,

            lastSeenAt:
                live.receivedAt,

            live:
                live
        };
    }

    return mergeDeviceWithLive(
        stored
    );
}

// ============================================================
// EXPORTS
// ============================================================

module.exports =
{
    touchRealtime,
    touchHistorical,
    listDevices,
    getDevice
};