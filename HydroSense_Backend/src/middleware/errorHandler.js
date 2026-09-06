// ============================================================
// HYDROSENSE API ERROR HANDLING
// ============================================================

// ============================================================
// 404 HANDLER
// ============================================================

function notFound(
    req,
    res
)
{
    return res
        .status(404)
        .json({
            success:
                false,

            message:
                "HydroSense API endpoint not found",

            path:
                req.originalUrl,

            method:
                req.method,

            timestamp:
                new Date()
                    .toISOString()
        });
}

// ============================================================
// CENTRAL ERROR HANDLER
//
// Express recognises an error middleware because it contains
// four parameters:
// error, req, res, next
// ============================================================

function errorHandler(
    error,
    req,
    res,
    next
)
{
    void req;
    void next;

    console.error(
        ""
    );

    console.error(
        "===================================================="
    );

    console.error(
        "             HYDROSENSE API ERROR"
    );

    console.error(
        "===================================================="
    );

    console.error(
        error.stack ||
        error.message ||
        error
    );

    console.error(
        "===================================================="
    );

    console.error(
        ""
    );

    const statusCode =
        Number(
            error.statusCode
        ) ||
        500;

    return res
        .status(
            statusCode
        )
        .json({
            success:
                false,

            message:
                statusCode >=
                500
                    ? "Internal HydroSense server error"
                    : error.message,

            timestamp:
                new Date()
                    .toISOString()
        });
}

// ============================================================
// EXPORT
// ============================================================

module.exports =
{
    notFound,
    errorHandler
};