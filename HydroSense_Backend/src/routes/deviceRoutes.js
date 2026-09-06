const express =
    require(
        "express"
    );

const {
    listDevices,
    getDevice,
    getDeviceLive
} =
    require(
        "../controllers/deviceController"
    );

const router =
    express.Router();

router.get(
    "/",
    listDevices
);

router.get(
    "/:deviceId/live",
    getDeviceLive
);

router.get(
    "/:deviceId",
    getDevice
);

module.exports =
    router;