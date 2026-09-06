const liveStates =
    new Map();

function offlineTimeoutMs()
{
    const configured =
        Number(
            process.env.DEVICE_OFFLINE_MS
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

    return 15000;
}

function setLiveState(
    deviceId,
    payload
)
{
    const receivedAt =
        new Date();

    const state =
    {
        payload,

        receivedAt,

        receivedAtMs:
            receivedAt
                .getTime()
    };

    liveStates.set(
        deviceId,
        state
    );

    return state;
}

function getRawState(
    deviceId
)
{
    return (
        liveStates.get(
            deviceId
        ) ||
        null
    );
}

function classifyState(
    state
)
{
    if (
        !state
    )
    {
        return "UNKNOWN";
    }

    const ageMs =
        Date.now() -
        state.receivedAtMs;

    if (
        ageMs >
        offlineTimeoutMs()
    )
    {
        return "OFFLINE";
    }

    return (
        state.payload?.status ===
        "VALID"
            ? "ONLINE"
            : "DEGRADED"
    );
}

function serializeState(
    deviceId,
    state
)
{
    if (
        !state
    )
    {
        return null;
    }

    const ageMs =
        Math.max(
            0,
            Date.now() -
                state.receivedAtMs
        );

    return {
        deviceId,

        backendStatus:
            classifyState(
                state
            ),

        backendPacketAgeMs:
            ageMs,

        receivedAt:
            state.receivedAt
                .toISOString(),

        data:
            state.payload
    };
}

function getLiveState(
    deviceId
)
{
    return serializeState(
        deviceId,
        getRawState(
            deviceId
        )
    );
}

function getAllLiveStates()
{
    return Array
        .from(
            liveStates.entries()
        )
        .map(
            (
                [
                    deviceId,
                    state
                ]
            ) =>
                serializeState(
                    deviceId,
                    state
                )
        );
}

module.exports =
{
    setLiveState,
    getLiveState,
    getAllLiveStates,
    classifyState,
    offlineTimeoutMs
};