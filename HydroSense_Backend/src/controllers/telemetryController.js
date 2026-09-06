const Telemetry =
    require(
        "../models/Telemetry"
    );

const deviceService =
    require(
        "../services/deviceService"
    );

const {
    validateHistoricalTelemetry
} =
    require(
        "../utils/telemetryValidation"
    );

function parseDate(
    value,
    fieldName
)
{
    if (
        !value
    )
    {
        return null;
    }

    const date =
        new Date(
            value
        );

    if (
        Number.isNaN(
            date.getTime()
        )
    )
    {
        const error =
            new Error(
                `${fieldName} must be a valid ISO date/time`
            );

        error.statusCode =
            400;

        throw error;
    }

    return date;
}

function historyWindow(
    query
)
{
    const to =
        parseDate(
            query.to,
            "to"
        ) ||
        new Date();

    const from =
        parseDate(
            query.from,
            "from"
        ) ||
        new Date(
            to.getTime() -
            24 *
            60 *
            60 *
            1000
        );

    if (
        from >
        to
    )
    {
        const error =
            new Error(
                "from must be earlier than to"
            );

        error.statusCode =
            400;

        throw error;
    }

    return {
        from,
        to
    };
}

// ============================================================
// STORE HISTORICAL SAMPLE
// ============================================================

async function createTelemetry(
    req,
    res,
    next
)
{
    try
    {
        const errors =
            validateHistoricalTelemetry(
                req.body
            );

        if (
            errors.length >
            0
        )
        {
            return res
                .status(400)
                .json({
                    success:
                        false,

                    message:
                        "Invalid HydroSense historical telemetry payload",

                    errors
                });
        }

        const telemetry =
            await Telemetry.create(
                req.body
            );

        await deviceService
            .touchHistorical(
                telemetry
            );

        const io =
            req.app.get(
                "io"
            );

        if (
            io
        )
        {
            io.emit(
                "hydrosense:history-stored",
                {
                    deviceId:
                        telemetry.deviceId,

                    sequence:
                        telemetry.sequence,

                    timestamp:
                        telemetry.timestamp
                            .toISOString()
                }
            );
        }

        console.log(
            `[Telemetry] STORED | device=${telemetry.deviceId} | seq=${telemetry.sequence}`
        );

        return res
            .status(201)
            .json({
                success:
                    true,

                message:
                    "HydroSense telemetry stored successfully",

                data:
                    telemetry
            });
    }

    catch (error)
    {
        if (
            error.name ===
            "ValidationError"
        )
        {
            return res
                .status(400)
                .json({
                    success:
                        false,

                    message:
                        "Telemetry validation failed",

                    error:
                        error.message
                });
        }

        return next(
            error
        );
    }
}

// ============================================================
// LATEST HISTORICAL SAMPLE
// ============================================================

async function getLatestTelemetry(
    req,
    res,
    next
)
{
    try
    {
        const filter =
            {};

        if (
            req.query.deviceId
        )
        {
            filter.deviceId =
                String(
                    req.query
                        .deviceId
                )
                    .trim();
        }

        const telemetry =
            await Telemetry
                .findOne(
                    filter
                )
                .sort({
                    timestamp:
                        -1
                })
                .lean();

        if (
            !telemetry
        )
        {
            return res
                .status(404)
                .json({
                    success:
                        false,

                    message:
                        "No HydroSense telemetry available"
                });
        }

        return res
            .status(200)
            .json({
                success:
                    true,

                data:
                    telemetry
            });
    }

    catch (error)
    {
        return next(
            error
        );
    }
}

// ============================================================
// HISTORICAL SERIES
// ============================================================

async function getTelemetryHistory(
    req,
    res,
    next
)
{
    try
    {
        const {
            from,
            to
        } =
            historyWindow(
                req.query
            );

        const requestedLimit =
            Number(
                req.query.limit ||
                1000
            );

        const limit =
            Number.isFinite(
                requestedLimit
            )
                ? Math.min(
                      Math.max(
                          Math.floor(
                              requestedLimit
                          ),
                          1
                      ),
                      5000
                  )
                : 1000;

        const filter =
        {
            timestamp:
            {
                $gte:
                    from,

                $lte:
                    to
            }
        };

        if (
            req.query.deviceId
        )
        {
            filter.deviceId =
                String(
                    req.query
                        .deviceId
                )
                    .trim();
        }

        const data =
            await Telemetry
                .find(
                    filter
                )
                .sort({
                    timestamp:
                        1
                })
                .limit(
                    limit
                )
                .lean();

        return res
            .status(200)
            .json({
                success:
                    true,

                query:
                {
                    deviceId:
                        filter.deviceId ||
                        null,

                    from:
                        from
                            .toISOString(),

                    to:
                        to
                            .toISOString(),

                    limit
                },

                count:
                    data.length,

                data
            });
    }

    catch (error)
    {
        return next(
            error
        );
    }
}

// ============================================================
// HISTORICAL SUMMARY
// ============================================================

async function getTelemetrySummary(
    req,
    res,
    next
)
{
    try
    {
        const {
            from,
            to
        } =
            historyWindow(
                req.query
            );

        const match =
        {
            timestamp:
            {
                $gte:
                    from,

                $lte:
                    to
            }
        };

        if (
            req.query.deviceId
        )
        {
            match.deviceId =
                String(
                    req.query
                        .deviceId
                )
                    .trim();
        }

        const [
            summary
        ] =
            await Telemetry.aggregate(
                [
                    {
                        $match:
                            match
                    },

                    {
                        $group:
                        {
                            _id:
                                null,

                            count:
                            {
                                $sum:
                                    1
                            },

                            firstTimestamp:
                            {
                                $min:
                                    "$timestamp"
                            },

                            lastTimestamp:
                            {
                                $max:
                                    "$timestamp"
                            },

                            airTemperatureAvg:
                            {
                                $avg:
                                    "$measured.airTemperatureC"
                            },

                            airTemperatureMin:
                            {
                                $min:
                                    "$measured.airTemperatureC"
                            },

                            airTemperatureMax:
                            {
                                $max:
                                    "$measured.airTemperatureC"
                            },

                            humidityAvg:
                            {
                                $avg:
                                    "$measured.humidityPct"
                            },

                            humidityMin:
                            {
                                $min:
                                    "$measured.humidityPct"
                            },

                            humidityMax:
                            {
                                $max:
                                    "$measured.humidityPct"
                            },

                            waterTemperatureAvg:
                            {
                                $avg:
                                    "$measured.waterTemperatureC"
                            },

                            waterTemperatureMin:
                            {
                                $min:
                                    "$measured.waterTemperatureC"
                            },

                            waterTemperatureMax:
                            {
                                $max:
                                    "$measured.waterTemperatureC"
                            },

                            waterLevelAvg:
                            {
                                $avg:
                                    "$measured.waterLevelPct"
                            },

                            waterLevelMin:
                            {
                                $min:
                                    "$measured.waterLevelPct"
                            },

                            waterLevelMax:
                            {
                                $max:
                                    "$measured.waterLevelPct"
                            },

                            tdsAvg:
                            {
                                $avg:
                                    "$estimated.tdsPpm"
                            },

                            tdsMin:
                            {
                                $min:
                                    "$estimated.tdsPpm"
                            },

                            tdsMax:
                            {
                                $max:
                                    "$estimated.tdsPpm"
                            },

                            vpdAvg:
                            {
                                $avg:
                                    "$derived.vpdKPa"
                            },

                            vpdMin:
                            {
                                $min:
                                    "$derived.vpdKPa"
                            },

                            vpdMax:
                            {
                                $max:
                                    "$derived.vpdKPa"
                            },

                            qualityAvg:
                            {
                                $avg:
                                    "$quality.scorePct"
                            },

                            qualityMin:
                            {
                                $min:
                                    "$quality.scorePct"
                            },

                            qualityMax:
                            {
                                $max:
                                    "$quality.scorePct"
                            }
                        }
                    },

                    {
                        $project:
                        {
                            _id:
                                0
                        }
                    }
                ]
            );

        return res
            .status(200)
            .json({
                success:
                    true,

                query:
                {
                    deviceId:
                        match.deviceId ||
                        null,

                    from:
                        from
                            .toISOString(),

                    to:
                        to
                            .toISOString()
                },

                data:
                    summary ||
                    {
                        count:
                            0,

                        firstTimestamp:
                            null,

                        lastTimestamp:
                            null
                    }
            });
    }

    catch (error)
    {
        return next(
            error
        );
    }
}

module.exports =
{
    createTelemetry,
    getLatestTelemetry,
    getTelemetryHistory,
    getTelemetrySummary
};