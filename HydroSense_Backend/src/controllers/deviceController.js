const deviceService =
    require(
        "../services/deviceService"
    );

const liveStateService =
    require(
        "../services/liveStateService"
    );

async function listDevices(
    req,
    res,
    next
)
{
    try
    {
        const devices =
            await deviceService
                .listDevices();

        return res
            .status(200)
            .json({
                success:
                    true,

                count:
                    devices.length,

                offlineTimeoutMs:
                    liveStateService
                        .offlineTimeoutMs(),

                data:
                    devices
            });
    }

    catch (error)
    {
        return next(
            error
        );
    }
}

async function getDevice(
    req,
    res,
    next
)
{
    try
    {
        const device =
            await deviceService
                .getDevice(
                    req.params
                        .deviceId
                );

        if (
            !device
        )
        {
            return res
                .status(404)
                .json({
                    success:
                        false,

                    message:
                        "HydroSense device not found"
                });
        }

        return res
            .status(200)
            .json({
                success:
                    true,

                data:
                    device
            });
    }

    catch (error)
    {
        return next(
            error
        );
    }
}

async function getDeviceLive(
    req,
    res
)
{
    const live =
        liveStateService
            .getLiveState(
                req.params
                    .deviceId
            );

    if (
        !live
    )
    {
        return res
            .status(404)
            .json({
                success:
                    false,

                message:
                    "No real-time data has been received for this device since backend startup"
            });
    }

    return res
        .status(200)
        .json({
            success:
                true,

            data:
                live
        });
}

module.exports =
{
    listDevices,
    getDevice,
    getDeviceLive
};
