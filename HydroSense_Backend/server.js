// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

// HydroSense Backend + Professional Control Room Router

const dns = require("node:dns");
const fs = require("node:fs");
const http = require("node:http");
const path = require("node:path");

const express = require("express");
const cors = require("cors");
const dotenv = require("dotenv");
const mongoose = require("mongoose");
const { Server } = require("socket.io");

// Load .env locally without overriding variables supplied by a host.
dotenv.config({ quiet: true });

// Keep the operating system's DNS servers unchanged.
dns.setDefaultResultOrder("ipv4first");

const connectDatabase = require("./src/config/database");
const telemetryRoutes = require("./src/routes/telemetryRoutes");
const realtimeRoutes = require("./src/routes/realtimeRoutes");
const deviceRoutes = require("./src/routes/deviceRoutes");
const sensorTrustRoutes = require("./src/routes/sensorTrustRoutes");
const mlControlRoutes = require("./src/routes/mlControlRoutes");
const mlRuntimeService = require("./src/services/mlRuntimeService");
const {
    notFound,
    errorHandler
} = require("./src/middleware/errorHandler");

// ============================================================
// CONFIGURATION
// ============================================================

function configuredPort()
{
    const value =
        Number(
            process.env.PORT ||
            5000
        );

    if (
        !Number.isInteger(value) ||
        value < 1 ||
        value > 65535
    )
    {
        throw new Error(
            "PORT must be an integer between 1 and 65535"
        );
    }

    return value;
}

function configuredCorsOrigin()
{
    const value =
        process.env.CORS_ORIGIN
            ?.trim() ||
        "*";

    if (
        value === "*"
    )
    {
        return "*";
    }

    const origins =
        value
            .split(",")
            .map(
                (origin) =>
                    origin.trim()
            )
            .filter(Boolean);

    if (
        origins.length === 0
    )
    {
        throw new Error(
            "CORS_ORIGIN does not contain a valid origin"
        );
    }

    return origins;
}

const PORT =
    configuredPort();

const corsOrigin =
    configuredCorsOrigin();

const corsOptions =
{
    origin:
        corsOrigin,

    methods:
    [
        "GET",
        "POST",
        "PATCH",
        "OPTIONS"
    ],

    allowedHeaders:
    [
        "Content-Type",
        "Authorization"
    ]
};

// ============================================================
// EXPRESS AND HTTP SERVER
// ============================================================

const app =
    express();

const httpServer =
    http.createServer(
        app
    );

// Limit slow or abandoned HTTP connections.
httpServer.requestTimeout =
    30000;

httpServer.headersTimeout =
    35000;

httpServer.keepAliveTimeout =
    5000;

app.disable(
    "x-powered-by"
);

// ============================================================
// SECURITY HEADERS
// ============================================================

app.use(
    (req, res, next) =>
    {
        void req;

        res.setHeader(
            "X-Content-Type-Options",
            "nosniff"
        );

        res.setHeader(
            "X-Frame-Options",
            "DENY"
        );

        res.setHeader(
            "Referrer-Policy",
            "no-referrer"
        );

        res.setHeader(
            "Permissions-Policy",
            "camera=(), microphone=(), geolocation=()"
        );

        next();
    }
);

app.use(
    cors(
        corsOptions
    )
);

app.use(
    express.json(
        {
            limit:
                "64kb"
        }
    )
);

app.use(
    express.urlencoded(
        {
            extended:
                true,

            limit:
                "64kb"
        }
    )
);

// ============================================================
// SOCKET.IO
// ============================================================

const io =
    new Server(
        httpServer,
        {
            cors:
                corsOptions,

            transports:
            [
                "websocket",
                "polling"
            ],

            pingInterval:
                25000,

            pingTimeout:
                20000
        }
    );

app.set(
    "io",
    io
);

io.on(
    "connection",
    (socket) =>
    {
        console.log(
            `[SOCKET] CONNECTED | ${socket.id}`
        );

        socket.emit(
            "hydrosense:server",
            {
                system:
                    "HydroSense",

                status:
                    "ONLINE",

                serverTimestamp:
                    new Date()
                        .toISOString()
            }
        );

        socket.on(
            "disconnect",
            (reason) =>
            {
                console.log(
                    `[SOCKET] DISCONNECTED | ${socket.id} | ${reason}`
                );
            }
        );
    }
);

// ============================================================
// ENTRY POINTS
// ============================================================

app.get(
    "/",
    (req, res) =>
    {
        void req;

        return res.redirect(
            302,
            "/control-room/overview"
        );
    }
);

app.get(
    "/dashboard",
    (req, res) =>
    {
        void req;

        return res.redirect(
            302,
            "/control-room/overview"
        );
    }
);

// ============================================================
// API STATUS
// ============================================================

app.get(
    "/api",
    (req, res) =>
    {
        void req;

        const databaseConnected =
            mongoose.connection
                .readyState ===
            1;

        return res
            .status(200)
            .json(
                {
                    success:
                        true,

                    system:
                        "HydroSense",

                    component:
                        "Backend API",

                    status:
                        databaseConnected
                            ? "ONLINE"
                            : "DEGRADED",

                    database:
                        databaseConnected
                            ? "CONNECTED"
                            : "DISCONNECTED",

                    controlRoom:
                        "/control-room/overview",

                    sensorTrust:
                        "/api/v1/sensor-trust/latest",

                    sensorTrustEvents:
                        "/api/v1/sensor-trust/events",

                    mlControlRoom:
                        "/control-room/ml",

                    mlControl:
                        "/api/v1/ml-control/status",

                    socketClients:
                        io.engine
                            .clientsCount,

                    timestamp:
                        new Date()
                            .toISOString()
                }
            );
    }
);

// ============================================================
// HEALTH ENDPOINT
// ============================================================

app.get(
    "/api/health",
    (req, res) =>
    {
        void req;

        const databaseConnected =
            mongoose.connection
                .readyState ===
            1;

        return res
            .status(
                databaseConnected
                    ? 200
                    : 503
            )
            .json(
                {
                    success:
                        databaseConnected,

                    service:
                        "HydroSense API",

                    status:
                        databaseConnected
                            ? "healthy"
                            : "degraded",

                    database:
                    {
                        connected:
                            databaseConnected,

                        readyState:
                            mongoose.connection
                                .readyState,

                        name:
                            mongoose.connection
                                .name ||
                            null,

                        host:
                            mongoose.connection
                                .host ||
                            null
                    },

                    realtime:
                    {
                        connectedClients:
                            io.engine
                                .clientsCount
                    },

                    sensorTrust:
                    {
                        api:
                            "READY",

                        latestEndpoint:
                            "/api/v1/sensor-trust/latest",

                        eventsEndpoint:
                            "/api/v1/sensor-trust/events"
                    },

                    machineLearning:
                    {
                        api:
                            "READY",

                        controlRoom:
                            "/control-room/ml",

                        statusEndpoint:
                            "/api/v1/ml-control/status",

                        liveEndpoint:
                            "/api/v1/ml-control/live"
                    },

                    uptimeSeconds:
                        Math.floor(
                            process.uptime()
                        ),

                    timestamp:
                        new Date()
                            .toISOString()
                }
            );
    }
);

// ============================================================
// API ROUTES
// ============================================================

app.use(
    "/api/v1/telemetry",
    telemetryRoutes
);

app.use(
    "/api/v1/realtime",
    realtimeRoutes
);

app.use(
    "/api/v1/devices",
    deviceRoutes
);

app.use(
    "/api/v1/sensor-trust",
    sensorTrustRoutes
);

app.use(
    "/api/v1/ml-control",
    mlControlRoutes
);

// ============================================================
// CONTROL ROOM FILES
// ============================================================

const publicDirectory =
    path.join(
        __dirname,
        "public"
    );

const nestedControlRoomDirectory =
    path.join(
        publicDirectory,
        "control-room"
    );

function resolveControlRoomFile(
    fileName
)
{
    const candidates =
    [
        path.join(
            publicDirectory,
            fileName
        ),

        path.join(
            nestedControlRoomDirectory,
            fileName
        )
    ];

    return (
        candidates.find(
            (candidate) =>
                fs.existsSync(
                    candidate
                )
        ) ||
        null
    );
}

function sendControlRoomFile(
    fileName
)
{
    return (
        req,
        res,
        next
    ) =>
    {
        void req;

        const resolved =
            resolveControlRoomFile(
                fileName
            );

        if (
            !resolved
        )
        {
            const error =
                new Error(
                    `Control Room asset not found: ${fileName}`
                );

            error.statusCode =
                404;

            return next(
                error
            );
        }

        res.setHeader(
            "Cache-Control",
            "no-store, no-cache, must-revalidate"
        );

        return res.sendFile(
            resolved
        );
    };
}

// ============================================================
// CONTROL ROOM STATIC ASSETS
// ============================================================

app.get(
    "/control-room/styles.css",
    sendControlRoomFile(
        "styles.css"
    )
);

app.get(
    "/control-room/app.js",
    sendControlRoomFile(
        "app.js"
    )
);

app.use(
    "/control-room/assets",

    express.static(
        path.join(
            publicDirectory,
            "assets"
        ),
        {
            fallthrough:
                true,

            etag:
                false,

            maxAge:
                0
        }
    )
);

app.use(
    "/control-room/assets",

    express.static(
        path.join(
            nestedControlRoomDirectory,
            "assets"
        ),
        {
            fallthrough:
                true,

            etag:
                false,

            maxAge:
                0
        }
    )
);

// ============================================================
// CONTROL ROOM SPA ROUTES
// ============================================================

const controlRoomRoutes =
[
    "overview",
    "live",
    "history",
    "ai",
    "ml",
    "trust",
    "health",
    "devices"
];

app.get(
    [
        "/control-room",
        "/control-room/"
    ],

    (req, res) =>
    {
        void req;

        return res.redirect(
            302,
            "/control-room/overview"
        );
    }
);

for (
    const route
    of controlRoomRoutes
)
{
    app.get(
        `/control-room/${route}`,

        sendControlRoomFile(
            "index.html"
        )
    );
}

// ============================================================
// CONTROL ROOM HEALTH
// ============================================================

app.get(
    "/api/control-room/health",
    (req, res) =>
    {
        void req;

        const indexHtml =
            resolveControlRoomFile(
                "index.html"
            );

        const stylesCss =
            resolveControlRoomFile(
                "styles.css"
            );

        const appJs =
            resolveControlRoomFile(
                "app.js"
            );

        const ready =
            Boolean(
                indexHtml &&
                stylesCss &&
                appJs
            );

        return res
            .status(
                ready
                    ? 200
                    : 503
            )
            .json(
                {
                    success:
                        ready,

                    controlRoom:
                        ready
                            ? "READY"
                            : "ASSET_MISSING",

                    pages:
                        controlRoomRoutes,

                    assets:
                    {
                        indexHtml:
                            Boolean(
                                indexHtml
                            ),

                        stylesCss:
                            Boolean(
                                stylesCss
                            ),

                        appJs:
                            Boolean(
                                appJs
                            )
                    },

                    timestamp:
                        new Date()
                            .toISOString()
                }
            );
    }
);

// ============================================================
// REALTIME DIAGNOSTIC PAGE
// ============================================================

app.get(
    "/realtime-test",
    (req, res) =>
    {
        void req;

        return res.sendFile(
            path.join(
                __dirname,
                "realtime_test.html"
            )
        );
    }
);

// ============================================================
// ERROR HANDLING
// ============================================================

app.use(
    notFound
);

app.use(
    errorHandler
);

// ============================================================
// STARTUP HELPERS
// ============================================================

function controlRoomAssetLocation(
    fileName
)
{
    const resolved =
        resolveControlRoomFile(
            fileName
        );

    return resolved
        ? path.relative(
            __dirname,
            resolved
        )
        : "MISSING";
}

function listen()
{
    return new Promise(
        (resolve, reject) =>
        {
            const handleStartupError =
                (error) =>
                {
                    reject(
                        error
                    );
                };

            httpServer.once(
                "error",
                handleStartupError
            );

            httpServer.listen(
                PORT,
                "0.0.0.0",
                () =>
                {
                    httpServer.off(
                        "error",
                        handleStartupError
                    );

                    resolve();
                }
            );
        }
    );
}

// ============================================================
// START SERVER
// ============================================================

async function startServer()
{
    try
    {
        await connectDatabase();
        await listen();

        console.log("");

        console.log(
            "===================================================="
        );

        console.log(
            "              HYDROSENSE BACKEND"
        );

        console.log(
            "===================================================="
        );

        console.log(
            "Server          : ONLINE"
        );

        console.log(
            `Port            : ${PORT}`
        );

        console.log(
            `Environment     : ${
                process.env.NODE_ENV ||
                "development"
            }`
        );

        console.log(
            "MongoDB         : CONNECTED"
        );

        console.log(
            `Control Room    : http://localhost:${PORT}/control-room/overview`
        );

        console.log(
            `Sensor Trust    : http://localhost:${PORT}/control-room/trust`
        );

        console.log(
            `ML Control      : http://localhost:${PORT}/control-room/ml`
        );

        console.log(
            `ML Control API  : http://localhost:${PORT}/api/v1/ml-control/status?deviceId=HydroSense_S3_01`
        );

        console.log(
            `Trust Latest API: http://localhost:${PORT}/api/v1/sensor-trust/latest?deviceId=HydroSense_S3_01`
        );

        console.log(
            `Trust Event API : http://localhost:${PORT}/api/v1/sensor-trust/events?deviceId=HydroSense_S3_01&limit=20`
        );

        console.log(
            `UI index        : ${controlRoomAssetLocation(
                "index.html"
            )}`
        );

        console.log(
            `UI styles       : ${controlRoomAssetLocation(
                "styles.css"
            )}`
        );

        console.log(
            `UI app          : ${controlRoomAssetLocation(
                "app.js"
            )}`
        );

        console.log(
            `UI Health       : http://localhost:${PORT}/api/control-room/health`
        );

        console.log(
            `API Health      : http://localhost:${PORT}/api/health`
        );

        console.log(
            "===================================================="
        );

        console.log("");
    }

    catch (error)
    {
        console.error(
            "[STARTUP] HydroSense backend failed:"
        );

        console.error(
            error
        );

        process.exit(
            1
        );
    }
}

// ============================================================
// GRACEFUL SHUTDOWN
// ============================================================

let shutdownStarted =
    false;

async function shutdown(
    signal
)
{
    if (
        shutdownStarted
    )
    {
        return;
    }

    shutdownStarted =
        true;

    console.log(
        `[SHUTDOWN] ${signal} received`
    );

    try
    {
        mlRuntimeService
            .shutdownAllWorkers();

        if (
            httpServer.listening
        )
        {
            await new Promise(
                (resolve, reject) =>
                {
                    httpServer.close(
                        (error) =>
                        {
                            if (
                                error
                            )
                            {
                                reject(
                                    error
                                );

                                return;
                            }

                            resolve();
                        }
                    );
                }
            );
        }

        if (
            mongoose.connection
                .readyState !==
            0
        )
        {
            await mongoose.connection
                .close();
        }

        console.log(
            "[SHUTDOWN] Complete"
        );

        process.exit(
            0
        );
    }

    catch (error)
    {
        console.error(
            "[SHUTDOWN] Failed:",
            error.message
        );

        process.exit(
            1
        );
    }
}

process.on(
    "SIGINT",
    () =>
        void shutdown(
            "SIGINT"
        )
);

process.on(
    "SIGTERM",
    () =>
        void shutdown(
            "SIGTERM"
        )
);

startServer();