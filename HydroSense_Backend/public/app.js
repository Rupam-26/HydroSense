(() => {
    "use strict";

    // HydroSense Control Room SPA.
    // No sensor value is generated in the browser.
    // Unavailable values remain unavailable.

    const state = {
        socket: null,
        socketConnected: false,

        backendHealth: null,

        devices: [],
        selectedDeviceId: "",

        latestPayload: null,

        realtimeSeries: [],

        historyHours: 24,
        historySeries: [],
        historySummary: null,
        historyRequestId: 0,

        trustEvents: [],

        mlControl: null,
        mlModels: [],
        mlLive: null,
        mlFormDeviceId: "",

        currentRoute: "overview"
    };


    // ============================================================
    // CONTROL ROOM ROUTES
    // ============================================================

    const views = {
        overview: [
            "System Overview",
            "Live state of the HydroSense hydroponics platform"
        ],

        live: [
            "Live Monitoring",
            "Realtime Socket.IO telemetry from the active S3 gateway"
        ],

        history: [
            "History & Analytics",
            "MongoDB-backed telemetry history and operating trends"
        ],

        ai: [
            "Edge AI",
            "TinyML TDS soft-sensor inference running on the ESP32-S3"
        ],

        ml: [
            "ML Control",
            "Website-controlled baseline acquisition, Isolation Forest training and labelled anomaly validation"
        ],

        trust: [
            "Sensor Trust",
            "Cross-layer measurement trust, anomaly evidence and probe-degradation assessment"
        ],

        health: [
            "System Health",
            "Sensor, communication, backend and database diagnostics"
        ],

        devices: [
            "Devices",
            "HydroSense device registry and runtime state"
        ]
    };


    // ============================================================
    // CHART PALETTE
    // ============================================================

    const palette = {
        air: "#35d6d1",
        water: "#6cb6ff",
        humidity: "#c793ff",
        level: "#5be49b",
        tds: "#ffc867",
        vpd: "#ff8ba0",

        grid: "rgba(125,162,177,.12)",
        axis: "rgba(134,161,173,.70)"
    };


    // ============================================================
    // BASIC HELPERS
    // ============================================================

    const $ = (id) =>
        document.getElementById(id);


    const finite = (value) =>
        typeof value === "number" &&
        Number.isFinite(value);


    const valueOrNull = (value) =>
        finite(value)
            ? value
            : null;


    const number = (
        value,
        decimals = 1
    ) =>
        finite(value)
            ? value.toFixed(decimals)
            : "--";


    const integer = (value) =>
        finite(value)
            ? String(
                Math.round(value)
            )
            : "--";


    function time(value) {
        const date =
            new Date(value);

        return Number.isNaN(
            date.getTime()
        )
            ? "--"
            : date.toLocaleTimeString(
                [],
                {
                    hour: "2-digit",
                    minute: "2-digit",
                    second: "2-digit"
                }
            );
    }


    function dateTime(value) {
        const date =
            new Date(value);

        return Number.isNaN(
            date.getTime()
        )
            ? "--"
            : date.toLocaleString();
    }


    function duration(seconds) {
        if (!finite(seconds)) {
            return "--";
        }

        const total =
            Math.max(
                0,
                Math.floor(seconds)
            );

        const d =
            Math.floor(
                total / 86400
            );

        const h =
            Math.floor(
                (total % 86400) /
                3600
            );

        const m =
            Math.floor(
                (total % 3600) /
                60
            );

        const s =
            total % 60;

        if (d) {
            return `${d}d ${h}h ${m}m`;
        }

        if (h) {
            return `${h}h ${m}m ${s}s`;
        }

        return `${m}m ${s}s`;
    }


    function setText(
        id,
        value
    ) {
        const el =
            $(id);

        if (el) {
            el.textContent =
                value;
        }
    }


    function statusClass(
        el,
        status
    ) {
        if (!el) {
            return;
        }

        el.classList.remove(
            "good",
            "warn",
            "bad",
            "unknown"
        );

        if (
            status === true ||
            [
                "good",
                "ONLINE",
                "VALID"
            ].includes(status)
        ) {
            el.classList.add(
                "good"
            );
        }

        else if (
            [
                "warn",
                "DEGRADED"
            ].includes(status)
        ) {
            el.classList.add(
                "warn"
            );
        }

        else if (
            status === false ||
            [
                "bad",
                "OFFLINE",
                "INVALID"
            ].includes(status)
        ) {
            el.classList.add(
                "bad"
            );
        }

        else {
            el.classList.add(
                "unknown"
            );
        }
    }


    const setDot = (
        id,
        status
    ) =>
        statusClass(
            $(id),
            status
        );


    function setPill(
        id,
        status,
        label
    ) {
        const el =
            $(id);

        if (!el) {
            return;
        }

        statusClass(
            el,
            status
        );

        el.textContent =
            label;
    }


    function setSensorState(
        id,
        valid
    ) {
        const el =
            $(id);

        if (!el) {
            return;
        }

        statusClass(
            el,

            valid === true
                ? "good"
                : valid === false
                    ? "bad"
                    : "unknown"
        );

        el.textContent =
            valid === true
                ? "VALID"
                : valid === false
                    ? "UNAVAILABLE"
                    : "NO DATA";
    }


    function showToast(message) {
        const el =
            $("toast");

        if (!el) {
            return;
        }

        el.textContent =
            message;

        el.classList.remove(
            "hidden"
        );

        clearTimeout(
            showToast.timer
        );

        showToast.timer =
            setTimeout(
                () =>
                    el.classList.add(
                        "hidden"
                    ),
                2600
            );
    }


    function showBanner(
        message = ""
    ) {
        const el =
            $("connectionBanner");

        if (!el) {
            return;
        }

        el.textContent =
            message;

        el.classList.toggle(
            "hidden",
            !message
        );
    }


    // ============================================================
    // HTTP HELPERS
    // ============================================================

    async function fetchJson(url) {
        const response =
            await fetch(
                url,
                {
                    headers: {
                        Accept:
                            "application/json"
                    },

                    cache:
                        "no-store"
                }
            );

        const payload =
            await response
                .json()
                .catch(
                    () =>
                        null
                );

        if (!response.ok) {
            throw new Error(
                payload?.message ||
                `HTTP ${response.status}`
            );
        }

        return payload;
    }


    async function sendJson(
        url,
        method,
        body = {}
    ) {
        const response =
            await fetch(
                url,
                {
                    method,

                    headers: {
                        Accept:
                            "application/json",

                        "Content-Type":
                            "application/json"
                    },

                    cache:
                        "no-store",

                    body:
                        JSON.stringify(
                            body
                        )
                }
            );

        const payload =
            await response
                .json()
                .catch(
                    () =>
                        null
                );

        if (!response.ok) {
            const detail =
                Array.isArray(
                    payload?.details
                )
                    ? ` ${
                        payload.details
                            .map(
                                (item) =>
                                    item.message ||
                                    item.title ||
                                    String(item)
                            )
                            .join(
                                " | "
                            )
                    }`
                    : "";

            throw new Error(
                (
                    payload?.message ||
                    `HTTP ${response.status}`
                ) +
                detail
            );
        }

        return payload;
    }


    // ============================================================
    // SPA ROUTER
    // ============================================================

    const validRoutes =
        new Set(
            Object.keys(
                views
            )
        );


    const routeUrl = (route) =>
        `/control-room/${
            validRoutes.has(route)
                ? route
                : "overview"
        }`;


    function routeFromLocation() {
        const parts =
            location.pathname
                .split("/")
                .filter(
                    Boolean
                );

        const candidate =
            parts[0] ===
                "control-room"
                ? (
                    parts[1] ||
                    "overview"
                )
                : "overview";

        return validRoutes.has(
            candidate
        )
            ? candidate
            : "overview";
    }


    function renderRoute(
        route,
        {
            loadData = true,
            scroll = true
        } = {}
    ) {
        const safe =
            validRoutes.has(route)
                ? route
                : "overview";

        state.currentRoute =
            safe;


        document
            .querySelectorAll(
                ".nav-item"
            )
            .forEach(
                (button) => {
                    const active =
                        button.dataset
                            .view ===
                        safe;

                    button
                        .classList
                        .toggle(
                            "active",
                            active
                        );

                    if (active) {
                        button.setAttribute(
                            "aria-current",
                            "page"
                        );
                    } else {
                        button.removeAttribute(
                            "aria-current"
                        );
                    }
                }
            );


        document
            .querySelectorAll(
                ".view"
            )
            .forEach(
                (view) => {
                    const active =
                        view.id ===
                        `view-${safe}`;

                    view.classList
                        .toggle(
                            "active",
                            active
                        );

                    view.setAttribute(
                        "aria-hidden",
                        String(
                            !active
                        )
                    );
                }
            );


        const [
            title,
            subtitle
        ] =
            views[safe];


        setText(
            "viewTitle",
            title
        );

        setText(
            "viewSubtitle",
            subtitle
        );


        document.title =
            `${title} · HydroSense Control Room`;


        document.body.dataset.route =
            safe;


        $("sidebar")
            ?.classList
            .remove(
                "open"
            );


        try {
            localStorage.setItem(
                "hydrosense:lastRoute",
                safe
            );
        } catch (_) {
        }


        if (loadData) {
            if (
                safe ===
                "history"
            ) {
                void loadHistory();
            }

            else if (
                safe ===
                "ml"
            ) {
                void loadCurrentLiveState();

                void loadMLControl();
            }

            else if (
                safe ===
                "trust"
            ) {
                void loadCurrentLiveState();

                void loadTrustEvents();
            }

            else if (
                safe ===
                "devices"
            ) {
                void loadDevices();
            }

            else if (
                safe ===
                "health"
            ) {
                void loadBackendHealth();
            }

            else {
                void loadCurrentLiveState();
            }
        }


        if (scroll) {
            window.scrollTo({
                top: 0,

                behavior:
                    "smooth"
            });
        }


        setTimeout(
            redrawAllCharts,
            80
        );
    }


    function navigate(
        route,
        {
            replace = false
        } = {}
    ) {
        const safe =
            validRoutes.has(route)
                ? route
                : "overview";

        const url =
            routeUrl(
                safe
            );


        if (
            location.pathname !==
            url
        ) {
            history[
                replace
                    ? "replaceState"
                    : "pushState"
            ](
                {
                    route:
                        safe
                },

                "",

                url
            );
        }


        renderRoute(
            safe
        );
    }


    function bindNavigation() {
        document
            .querySelectorAll(
                ".nav-item"
            )
            .forEach(
                (button) => {
                    button.addEventListener(
                        "click",
                        () =>
                            navigate(
                                button.dataset
                                    .view
                            )
                    );
                }
            );


        window.addEventListener(
            "popstate",

            () =>
                renderRoute(
                    routeFromLocation(),

                    {
                        scroll:
                            false
                    }
                )
        );


        $("mobileMenu")
            ?.addEventListener(
                "click",

                () =>
                    $("sidebar")
                        ?.classList
                        .toggle(
                            "open"
                        )
            );
    }


    // ============================================================
    // BACKEND HEALTH
    // ============================================================

    async function loadBackendHealth() {
        try {
            const result =
                await fetchJson(
                    "/api/health"
                );


            state.backendHealth =
                result;


            const db =
                result.database
                    ?.connected ===
                true;


            setDot(
                "sidebarBackendDot",

                result.success
                    ? "good"
                    : "warn"
            );


            setText(
                "sidebarBackendText",

                result.success
                    ? "Backend online"
                    : "Backend degraded"
            );


            setDot(
                "chainApiDot",

                result.success
                    ? "good"
                    : "warn"
            );


            setText(
                "chainApiText",

                result.success
                    ? "Online"
                    : "Degraded"
            );


            setDot(
                "chainDbDot",

                db
                    ? "good"
                    : "bad"
            );


            setText(
                "chainDbText",

                db
                    ? "Connected"
                    : "Offline"
            );


            setDot(
                "healthApiDot",

                result.success
                    ? "good"
                    : "warn"
            );


            setText(
                "healthApiText",

                result.success
                    ? "ONLINE"
                    : "DEGRADED"
            );


            setDot(
                "healthDbDot",

                db
                    ? "good"
                    : "bad"
            );


            setText(
                "healthDbText",

                db
                    ? "CONNECTED"
                    : "DISCONNECTED"
            );


            setText(
                "backendUptime",

                duration(
                    result.uptimeSeconds
                )
            );


            setText(
                "socketClients",

                integer(
                    result.realtime
                        ?.connectedClients
                )
            );


            showBanner();
        }

        catch (error) {
            state.backendHealth =
                null;


            setDot(
                "sidebarBackendDot",
                "bad"
            );


            setText(
                "sidebarBackendText",
                "Backend unreachable"
            );


            setDot(
                "chainApiDot",
                "bad"
            );


            setText(
                "chainApiText",
                "Offline"
            );


            setDot(
                "chainDbDot",
                "unknown"
            );


            setText(
                "chainDbText",
                "Unknown"
            );


            setDot(
                "healthApiDot",
                "bad"
            );


            setText(
                "healthApiText",
                "OFFLINE"
            );


            setDot(
                "healthDbDot",
                "unknown"
            );


            setText(
                "healthDbText",
                "UNKNOWN"
            );


            showBanner(
                `Backend health check failed: ${error.message}`
            );
        }
    }


    // ============================================================
    // ESCAPE HTML
    // ============================================================

    function escapeHtml(value) {
        return String(
            value ?? ""
        )
            .replaceAll(
                "&",
                "&amp;"
            )
            .replaceAll(
                "<",
                "&lt;"
            )
            .replaceAll(
                ">",
                "&gt;"
            )
            .replaceAll(
                '"',
                "&quot;"
            )
            .replaceAll(
                "'",
                "&#039;"
            );
    }


    // ============================================================
    // DEVICES
    // ============================================================

    function selectedDevice() {
        return (
            state.devices.find(
                (device) =>
                    device.deviceId ===
                    state.selectedDeviceId
            ) ||
            null
        );
    }


    async function loadDevices() {
        try {
            const result =
                await fetchJson(
                    "/api/v1/devices"
                );


            state.devices =
                Array.isArray(
                    result.data
                )
                    ? result.data
                    : [];


            let stored =
                "";


            try {
                stored =
                    localStorage.getItem(
                        "hydrosense:selectedDevice"
                    ) ||
                    "";
            } catch (_) {
            }


            const preferred =
                state.selectedDeviceId ||
                stored;


            const exists =
                state.devices.some(
                    (device) =>
                        device.deviceId ===
                        preferred
                );


            state.selectedDeviceId =
                exists
                    ? preferred
                    : (
                        state.devices[0]
                            ?.deviceId ||
                        ""
                    );


            const select =
                $("deviceSelect");


            if (select) {
                select.innerHTML =
                    "";


                if (
                    !state.devices
                        .length
                ) {
                    const option =
                        document.createElement(
                            "option"
                        );

                    option.value =
                        "";

                    option.textContent =
                        "No device discovered";

                    select.appendChild(
                        option
                    );
                }

                else {
                    state.devices
                        .forEach(
                            (device) => {
                                const option =
                                    document.createElement(
                                        "option"
                                    );

                                option.value =
                                    device.deviceId;

                                option.textContent =
                                    device.displayName ||
                                    device.deviceId;

                                select.appendChild(
                                    option
                                );
                            }
                        );


                    select.value =
                        state.selectedDeviceId;
                }
            }


            if (
                state.selectedDeviceId
            ) {
                try {
                    localStorage.setItem(
                        "hydrosense:selectedDevice",

                        state.selectedDeviceId
                    );
                } catch (_) {
                }
            }


            renderDeviceTable();

            renderSelectedDeviceRegistryState();


            if (
                state.selectedDeviceId
            ) {
                await loadCurrentLiveState();
            }
        }

        catch (error) {
            showBanner(
                `Device registry unavailable: ${error.message}`
            );
        }
    }


    function renderSelectedDeviceRegistryState() {
        const device =
            selectedDevice();


        const status =
            device?.runtimeStatus ||
            device?.status ||
            "UNKNOWN";


        setText(
            "overviewDeviceName",

            device?.displayName ||
            state.selectedDeviceId ||
            "Waiting for device"
        );


        setPill(
            "overviewDeviceStatus",

            status,

            status
        );


        setText(
            "overviewLastSeen",

            device?.lastSeenAt
                ? `Last seen ${dateTime(device.lastSeenAt)}`
                : "No realtime packet received yet."
        );


        setDot(
            "chainS3Dot",

            status === "ONLINE"
                ? "good"
                : status === "DEGRADED"
                    ? "warn"
                    : status === "OFFLINE"
                        ? "bad"
                        : "unknown"
        );


        setText(
            "chainS3Text",

            status === "UNKNOWN"
                ? "Unknown"
                : status[0] +
                    status
                        .slice(1)
                        .toLowerCase()
        );
    }


    function renderDeviceTable() {
        const body =
            $("deviceTableBody");


        if (!body) {
            return;
        }


        body.innerHTML =
            "";


        if (
            !state.devices
                .length
        ) {
            body.innerHTML =
                '<tr><td colspan="5" class="empty-table">No HydroSense device has been registered yet.</td></tr>';

            return;
        }


        state.devices
            .forEach(
                (device) => {
                    const row =
                        document.createElement(
                            "tr"
                        );


                    const runtime =
                        device.runtimeStatus ||
                        device.status ||
                        "UNKNOWN";


                    const quality =
                        finite(
                            device.lastQualityScorePct
                        )
                            ? `${device.lastQualityScorePct.toFixed(0)}%`
                            : "--";


                    const sequence =
                        finite(
                            device.lastSequence
                        )
                            ? Math.round(
                                device.lastSequence
                            )
                            : "--";


                    row.innerHTML = `
                        <td>
                            <strong>${escapeHtml(device.displayName || device.deviceId)}</strong>
                            <br>
                            <span class="panel-caption">${escapeHtml(device.deviceId)}</span>
                        </td>

                        <td>${escapeHtml(runtime)}</td>

                        <td>${quality}</td>

                        <td>${sequence}</td>

                        <td>${escapeHtml(dateTime(device.lastSeenAt))}</td>
                    `;


                    row.addEventListener(
                        "click",

                        () => {
                            state.selectedDeviceId =
                                device.deviceId;


                            state.realtimeSeries =
                                [];


                            try {
                                localStorage.setItem(
                                    "hydrosense:selectedDevice",

                                    device.deviceId
                                );
                            } catch (_) {
                            }


                            if (
                                $("deviceSelect")
                            ) {
                                $("deviceSelect").value =
                                    device.deviceId;
                            }


                            renderSelectedDeviceRegistryState();


                            void loadCurrentLiveState();


                            navigate(
                                "overview"
                            );
                        }
                    );


                    body.appendChild(
                        row
                    );
                }
            );
    }


    async function loadCurrentLiveState() {
        if (
            !state.selectedDeviceId
        ) {
            return;
        }


        try {
            const result =
                await fetchJson(
                    `/api/v1/devices/${encodeURIComponent(state.selectedDeviceId)}/live`
                );


            const payload =
                result.data?.data;


            if (payload) {
                ingestRealtimePayload(
                    payload,
                    false
                );
            }
        }

        catch (error) {
            if (
                !String(
                    error.message
                ).includes(
                    "No real-time data"
                )
            ) {
                console.warn(
                    "Live state load failed:",

                    error.message
                );
            }
        }
    }


    // ============================================================
    // SOCKET.IO REALTIME
    // ============================================================

    function connectSocket() {
        state.socket =
            io({
                transports: [
                    "websocket",
                    "polling"
                ],

                reconnection:
                    true,

                reconnectionDelay:
                    800,

                reconnectionDelayMax:
                    5000
            });


        state.socket.on(
            "connect",

            () => {
                state.socketConnected =
                    true;


                setPill(
                    "liveSocketStatus",
                    "ONLINE",
                    "CONNECTED"
                );


                setDot(
                    "healthSocketDot",
                    "good"
                );


                setText(
                    "healthSocketText",
                    "CONNECTED"
                );


                showBanner();


                void loadBackendHealth();
            }
        );


        const disconnected =
            () => {
                state.socketConnected =
                    false;


                setPill(
                    "liveSocketStatus",
                    "OFFLINE",
                    "DISCONNECTED"
                );


                setDot(
                    "healthSocketDot",
                    "bad"
                );


                setText(
                    "healthSocketText",
                    "DISCONNECTED"
                );
            };


        state.socket.on(
            "disconnect",
            disconnected
        );


        state.socket.on(
            "connect_error",

            (error) => {
                disconnected();


                showBanner(
                    `Realtime socket unavailable: ${error.message}`
                );
            }
        );


        // --------------------------------------------------------
        // ORIGINAL S3 REALTIME STREAM
        // --------------------------------------------------------

        state.socket.on(
            "hydrosense:live",

            (payload) => {
                if (
                    !payload?.deviceId
                ) {
                    return;
                }


                if (
                    !state.selectedDeviceId
                ) {
                    state.selectedDeviceId =
                        payload.deviceId;


                    void loadDevices();
                }


                if (
                    payload.deviceId ===
                    state.selectedDeviceId
                ) {
                    ingestRealtimePayload(
                        payload,
                        true
                    );
                }
            }
        );


        // --------------------------------------------------------
        // HISTORICAL RECORD STORED
        // --------------------------------------------------------

        state.socket.on(
            "hydrosense:history-stored",

            (event) => {
                if (
                    event?.deviceId !==
                    state.selectedDeviceId
                ) {
                    return;
                }


                if (
                    state.currentRoute ===
                    "history"
                ) {
                    void loadHistory();
                }


                if (
                    state.currentRoute ===
                    "ml"
                ) {
                    void loadMLControl({
                        preserveForm:
                            true
                    });
                }
            }
        );


        // --------------------------------------------------------
        // SERVER ISOLATION FOREST REALTIME
        // --------------------------------------------------------

        state.socket.on(
            "hydrosense:ml-live",

            (payload) => {
                if (
                    payload?.deviceId !==
                    state.selectedDeviceId
                ) {
                    return;
                }


                state.mlLive =
                    payload;


                renderMLLive();
            }
        );


        // --------------------------------------------------------
        // TRAINING PROGRESS
        // --------------------------------------------------------

        state.socket.on(
            "hydrosense:ml-training",

            (event) => {
                if (
                    event?.deviceId !==
                    state.selectedDeviceId
                ) {
                    return;
                }


                if (
                    state.mlControl
                ) {
                    state.mlControl.training =
                        event;
                }


                renderMLTraining();


                if (
                    [
                        "COMPLETE",
                        "COMPLETED",
                        "FAILED"
                    ].includes(
                        event?.status
                    )
                ) {
                    setTimeout(
                        () =>
                            void loadMLControl({
                                preserveForm:
                                    true
                            }),

                        500
                    );
                }
            }
        );


        // --------------------------------------------------------
        // SENSOR TRUST TRANSITIONS
        // --------------------------------------------------------

        state.socket.on(
            "hydrosense:sensor-trust-event",

            (event) => {
                if (
                    event?.deviceId !==
                    state.selectedDeviceId
                ) {
                    return;
                }


                state.trustEvents.unshift(
                    event
                );


                if (
                    state.trustEvents.length >
                    100
                ) {
                    state.trustEvents.length =
                        100;
                }


                renderTrustEvents();
            }
        );
    }


    // ============================================================
    // INGEST REALTIME PAYLOAD
    // ============================================================

    function ingestRealtimePayload(
        payload,
        appendSeries
    ) {
        state.latestPayload =
            payload;


        if (appendSeries) {
            state.realtimeSeries.push({
                time:
                    payload.serverTimestamp ||
                    new Date()
                        .toISOString(),

                air:
                    payload.measured
                        ?.airTemperatureValid
                        ? valueOrNull(
                            payload.measured
                                .airTemperatureC
                        )
                        : null,

                humidity:
                    payload.measured
                        ?.humidityValid
                        ? valueOrNull(
                            payload.measured
                                .humidityPct
                        )
                        : null,

                water:
                    payload.measured
                        ?.waterTemperatureValid
                        ? valueOrNull(
                            payload.measured
                                .waterTemperatureC
                        )
                        : null,

                level:
                    payload.measured
                        ?.waterLevelValid
                        ? valueOrNull(
                            payload.measured
                                .waterLevelPct
                        )
                        : null,

                tds:
                    payload.estimated
                        ?.valid
                        ? valueOrNull(
                            payload.estimated
                                .tdsPpm
                        )
                        : null,

                vpd:
                    payload.derived
                        ?.environmentalValid
                        ? valueOrNull(
                            payload.derived
                                .vpdKPa
                        )
                        : null
            });


            if (
                state.realtimeSeries.length >
                180
            ) {
                state.realtimeSeries.splice(
                    0,

                    state.realtimeSeries.length -
                    180
                );
            }
        }


        renderRealtimePayload(
            payload
        );


        redrawRealtimeCharts();
    }


    // ============================================================
    // RENDER REALTIME PAYLOAD
    // ============================================================

    function renderRealtimePayload(
        payload
    ) {
        const health =
            payload.health ||
            {};


        const measured =
            payload.measured ||
            {};


        const derived =
            payload.derived ||
            {};


        const estimated =
            payload.estimated ||
            {};


        const quality =
            payload.quality ||
            {};


        setText(
            "overviewDeviceName",

            payload.deviceId ||
            state.selectedDeviceId ||
            "HydroSense device"
        );


        setPill(
            "overviewDeviceStatus",

            payload.status ===
                "VALID"
                ? "ONLINE"
                : "DEGRADED",

            payload.status ===
                "VALID"
                ? "ONLINE"
                : "DEGRADED"
        );


        setText(
            "overviewLastSeen",

            `Realtime packet received ${
                time(
                    payload.serverTimestamp ||
                    new Date()
                )
            } · sequence ${
                integer(
                    payload.sequence
                )
            }`
        );


        const sensorRows = [
            [
                "airTemperature",
                "airTemperatureState",
                measured.airTemperatureValid,
                measured.airTemperatureC,
                1
            ],

            [
                "humidity",
                "humidityState",
                measured.humidityValid,
                measured.humidityPct,
                1
            ],

            [
                "waterTemperature",
                "waterTemperatureState",
                measured.waterTemperatureValid,
                measured.waterTemperatureC,
                2
            ],

            [
                "waterLevel",
                "waterLevelState",
                measured.waterLevelValid,
                measured.waterLevelPct,
                1
            ]
        ];


        sensorRows.forEach(
            ([
                valueId,
                stateId,
                valid,
                value,
                decimals
            ]) => {
                setText(
                    valueId,

                    valid === true
                        ? number(
                            value,
                            decimals
                        )
                        : "--"
                );


                setSensorState(
                    stateId,
                    valid
                );
            }
        );


        setText(
            "stableDistance",

            measured.stableDistanceValid
                ? number(
                    measured.stableDistanceCm,
                    1
                )
                : "--"
        );


        setText(
            "distanceCaption",

            `Distance ${
                measured.stableDistanceValid
                    ? number(
                        measured.stableDistanceCm,
                        1
                    )
                    : "--"
            } cm`
        );


        setText(
            "packetAge",

            integer(
                payload.packetAgeMs
            )
        );


        setText(
            "reservoirValidity",

            health.reservoirValid === true
                ? "VALID"
                : health.reservoirValid === false
                    ? "INVALID"
                    : "UNKNOWN"
        );


        setText(
            "calibrationStatus",

            health.calibrationValid === true
                ? "VALID"
                : health.calibrationValid === false
                    ? "INVALID"
                    : "UNKNOWN"
        );


        const level =
            measured.waterLevelValid &&
            finite(
                measured.waterLevelPct
            )
                ? Math.min(
                    100,

                    Math.max(
                        0,

                        measured.waterLevelPct
                    )
                )
                : 0;


        if (
            $("tankWater")
        ) {
            $("tankWater").style.height =
                `${level}%`;


            $("tankWater").style.opacity =
                measured.waterLevelValid
                    ? "1"
                    : ".18";
        }


        setText(
            "tankLevelLabel",

            measured.waterLevelValid
                ? number(
                    measured.waterLevelPct,
                    0
                )
                : "--"
        );


        // --------------------------------------------------------
        // TINYML TDS
        // --------------------------------------------------------

        setText(
            "tdsValue",

            estimated.valid
                ? number(
                    estimated.tdsPpm,
                    1
                )
                : "--"
        );


        setPill(
            "tdsStatus",

            estimated.valid
                ? "VALID"
                : "INVALID",

            estimated.valid
                ? "VALID"
                : "UNAVAILABLE"
        );


        setText(
            "tdsModel",

            estimated.model ||
            "--"
        );


        setText(
            "inferenceMicros",

            integer(
                estimated.inferenceMicros
            )
        );


        setText(
            "tdsInputValidity",

            health.environmentalFeaturesValid &&
            health.thermalFeaturesValid
                ? "VALID"
                : "INVALID"
        );


        // --------------------------------------------------------
        // QUALITY
        // --------------------------------------------------------

        setText(
            "qualityScore",

            number(
                quality.scorePct,
                0
            )
        );


        setText(
            "qualityFlags",

            finite(
                quality.flags
            )
                ? `0x${
                    Math.round(
                        quality.flags
                    )
                        .toString(16)
                        .toUpperCase()
                }`
                : "--"
        );


        setText(
            "crcFailures",

            integer(
                quality.crcFailures
            )
        );


        setText(
            "wrongLengths",

            integer(
                quality.wrongLengths
            )
        );


        setText(
            "queueDrops",

            integer(
                quality.queueDrops
            )
        );


        if (
            $("qualityFill")
        ) {
            $("qualityFill").style.width =
                finite(
                    quality.scorePct
                )
                    ? `${
                        Math.min(
                            100,

                            Math.max(
                                0,

                                quality.scorePct
                            )
                        )
                    }%`
                    : "0%";
        }


        // --------------------------------------------------------
        // DERIVED FEATURES
        // --------------------------------------------------------

        setText(
            "dewPoint",

            derived.environmentalValid
                ? number(
                    derived.dewPointC,
                    2
                )
                : "--"
        );


        setText(
            "vpd",

            derived.environmentalValid
                ? number(
                    derived.vpdKPa,
                    3
                )
                : "--"
        );


        setText(
            "absoluteHumidity",

            derived.environmentalValid
                ? number(
                    derived.absoluteHumidityGm3,
                    2
                )
                : "--"
        );


        setText(
            "airWaterDelta",

            derived.thermalValid
                ? number(
                    derived.airWaterDeltaC,
                    2
                )
                : "--"
        );


        renderChain(
            health,
            payload
        );


        renderHealth(
            health,
            payload,
            quality
        );


        renderAi(
            health,
            payload,
            estimated
        );


        renderSensorTrust(
            payload
        );


        if (
            state.currentRoute ===
            "ml"
        ) {
            renderMLLive();
        }


        setText(
            "liveSequence",

            `Sequence ${
                integer(
                    payload.sequence
                )
            }`
        );


        setText(
            "rawPayload",

            JSON.stringify(
                payload,
                null,
                2
            )
        );
    }


    // ============================================================
    // CONNECTION CHAIN
    // ============================================================

    function renderChain(
        health,
        payload
    ) {
        const items = [
            [
                "chainC3Dot",
                "chainC3Text",
                health.packetValid,
                "Packet valid",
                "Invalid"
            ],

            [
                "chainBleDot",
                "chainBleText",
                health.bleConnected,
                "Connected",
                "Offline"
            ],

            [
                "chainWifiDot",
                "chainWifiText",
                health.wifiConnected,
                "Connected",
                "Offline"
            ]
        ];


        items.forEach(
            ([
                dot,
                textId,
                ok,
                yes,
                no
            ]) => {
                setDot(
                    dot,

                    ok === true
                        ? "good"
                        : ok === false
                            ? "bad"
                            : "unknown"
                );


                setText(
                    textId,

                    ok === true
                        ? yes
                        : ok === false
                            ? no
                            : "Unknown"
                );
            }
        );


        setDot(
            "chainS3Dot",

            payload.status ===
                "VALID"
                ? "good"
                : "warn"
        );


        setText(
            "chainS3Text",

            payload.status ===
                "VALID"
                ? "Online"
                : "Degraded"
        );
    }


    // ============================================================
    // SYSTEM HEALTH
    // ============================================================

    function renderHealth(
        health,
        payload,
        quality
    ) {
        [
            [
                "healthC3Dot",
                "healthC3Text",
                health.packetValid
            ],

            [
                "healthBleDot",
                "healthBleText",
                health.bleConnected
            ],

            [
                "healthWifiDot",
                "healthWifiText",
                health.wifiConnected
            ],

            [
                "healthDhtDot",
                "healthDhtText",
                health.dht22Valid
            ],

            [
                "healthDsDot",
                "healthDsText",
                health.ds18b20Valid
            ],

            [
                "healthUltrasonicDot",
                "healthUltrasonicText",
                health.ultrasonicValid
            ],

            [
                "healthReservoirDot",
                "healthReservoirText",
                health.reservoirValid
            ],

            [
                "healthCalibrationDot",
                "healthCalibrationText",
                health.calibrationValid
            ]
        ]
            .forEach(
                ([
                    dot,
                    textId,
                    ok
                ]) => {
                    setDot(
                        dot,

                        ok === true
                            ? "good"
                            : ok === false
                                ? "bad"
                                : "unknown"
                    );


                    setText(
                        textId,

                        ok === true
                            ? "VALID"
                            : ok === false
                                ? "INVALID"
                                : "UNKNOWN"
                    );
                }
            );


        setText(
            "healthPacketAge",

            integer(
                payload.packetAgeMs
            )
        );


        setText(
            "healthSequence",

            integer(
                payload.sequence
            )
        );


        setText(
            "healthQuality",

            number(
                quality.scorePct,
                0
            )
        );


        setText(
            "healthCrc",

            integer(
                quality.crcFailures
            )
        );


        setText(
            "healthWrongLengths",

            integer(
                quality.wrongLengths
            )
        );


        setText(
            "healthBadHeaders",

            integer(
                quality.badHeaders
            )
        );


        setText(
            "healthBadVersions",

            integer(
                quality.badVersions
            )
        );


        setText(
            "healthQueueDrops",

            integer(
                quality.queueDrops
            )
        );
    }


    // ============================================================
    // EDGE AI PAGE
    // ============================================================

    function renderAi(
        health,
        payload,
        estimated
    ) {
        setText(
            "aiPageTds",

            estimated.valid
                ? number(
                    estimated.tdsPpm,
                    1
                )
                : "--"
        );


        setPill(
            "aiPageStatus",

            estimated.valid
                ? "VALID"
                : "INVALID",

            estimated.valid
                ? "VALID"
                : "UNAVAILABLE"
        );


        setText(
            "aiPageModel",

            estimated.model ||
            "--"
        );


        setText(
            "aiPageValid",

            estimated.valid === true
                ? "VALID"
                : estimated.valid === false
                    ? "INVALID"
                    : "UNKNOWN"
        );


        setText(
            "aiEnvironmentValid",

            health.environmentalFeaturesValid === true
                ? "VALID"
                : health.environmentalFeaturesValid === false
                    ? "INVALID"
                    : "UNKNOWN"
        );


        setText(
            "aiThermalValid",

            health.thermalFeaturesValid === true
                ? "VALID"
                : health.thermalFeaturesValid === false
                    ? "INVALID"
                    : "UNKNOWN"
        );


        setText(
            "aiPageInference",

            integer(
                estimated.inferenceMicros
            )
        );


        setText(
            "aiPageSequence",

            integer(
                payload.sequence
            )
        );
    }


    // ============================================================
    // SENSOR TRUST
    // ============================================================

    function trustUiStatus(
        stateText
    ) {
        if (
            stateText ===
            "MEASUREMENT CURRENTLY TRUSTED"
        ) {
            return "good";
        }


        if (
            stateText ===
                "TRUST REDUCED - MONITOR SENSOR" ||

            stateText ===
                "LEARNING NORMAL SENSOR BEHAVIOUR" ||

            stateText ===
                "PARTIAL SENSOR COVERAGE - ONE OR MORE MEASUREMENTS UNAVAILABLE"
        ) {
            return "warn";
        }


        if (
            stateText ===
                "LOW SENSOR TRUST - INSPECTION RECOMMENDED" ||

            stateText ===
                "MEASUREMENT NOT RELIABLE FOR OPERATIONAL DECISIONS"
        ) {
            return "bad";
        }


        return "unknown";
    }


    function foulingUiStatus(
        stateText
    ) {
        if (
            stateText ===
            "NO SIGNIFICANT FOULING-COMPATIBLE DEGRADATION"
        ) {
            return "good";
        }


        if (
            stateText ===
                "POSSIBLE WATER-PROBE RESPONSE DEGRADATION - MONITOR" ||

            stateText ===
                "LEARNING CLEAN WATER-PROBE RESPONSE" ||

            stateText ===
                "NOT ENOUGH COMPARABLE THERMAL EVENTS YET"
        ) {
            return "warn";
        }


        if (
            stateText ===
                "FOULING-COMPATIBLE RESPONSE DEGRADATION - INSPECT PROBE" ||

            stateText ===
                "FOULING ASSESSMENT PAUSED - SENSOR SIGNAL UNRELIABLE"
        ) {
            return "bad";
        }


        return "unknown";
    }


    function setTrustComponent(
        valueId,
        barId,
        value
    ) {
        setText(
            valueId,

            finite(value)
                ? `${value.toFixed(0)} / 100`
                : "NOT AVAILABLE"
        );


        const bar =
            $(barId);


        if (bar) {
            bar.style.width =
                finite(value)
                    ? `${
                        Math.min(
                            100,

                            Math.max(
                                0,
                                value
                            )
                        )
                    }%`
                    : "0%";
        }
    }


    function renderTrustSensor(
        prefix,
        analysis
    ) {
        const stateText =
            analysis?.state ||
            "MEASUREMENT UNAVAILABLE";


        const score =
            analysis?.trustScorePct;


        const components =
            analysis?.components ||
            {};


        setText(
            `${prefix}Score`,

            finite(score)
                ? score.toFixed(1)
                : "--"
        );


        setPill(
            `${prefix}State`,

            trustUiStatus(
                stateText
            ),

            stateText
        );


        setText(
            `${prefix}Reason`,

            analysis?.primaryReason ||
            "No Sensor Trust explanation is available yet."
        );


        setTrustComponent(
            `${prefix}Measurement`,
            `${prefix}MeasurementBar`,
            components.measurementIntegrityPct
        );


        setTrustComponent(
            `${prefix}Quality`,
            `${prefix}QualityBar`,
            components.dataQualityPct
        );


        setTrustComponent(
            `${prefix}Temporal`,
            `${prefix}TemporalBar`,
            components.temporalReliabilityPct
        );


        setTrustComponent(
            `${prefix}Learned`,
            `${prefix}LearnedBar`,
            components.learnedBehaviourConsistencyPct
        );


        setTrustComponent(
            `${prefix}Communication`,
            `${prefix}CommunicationBar`,
            components.communicationIntegrityPct
        );


        if (
            $(`${prefix}Domain`)
        ) {
            setTrustComponent(
                `${prefix}Domain`,
                `${prefix}DomainBar`,
                components.domainSpecificDegradationIntegrityPct
            );
        }
    }


    function renderFouling(surface) {
        const stateText =
            surface?.state ||
            "NOT ENOUGH COMPARABLE THERMAL EVENTS YET";


        setPill(
            "trustFoulingState",

            foulingUiStatus(
                stateText
            ),

            stateText
        );


        setText(
            "trustFoulingEvidence",

            finite(
                surface?.evidenceIndexPct
            )
                ? number(
                    surface.evidenceIndexPct,
                    1
                )
                : "--"
        );


        setText(
            "trustFoulingConfidence",

            finite(
                surface?.assessmentConfidencePct
            )
                ? number(
                    surface.assessmentConfidencePct,
                    1
                )
                : "--"
        );


        setText(
            "trustFoulingCleanEvents",

            finite(
                surface?.cleanResponseEvents
            ) &&
            finite(
                surface?.requiredCleanResponseEvents
            )
                ? `${
                    integer(
                        surface.cleanResponseEvents
                    )
                } / ${
                    integer(
                        surface.requiredCleanResponseEvents
                    )
                }`
                : "--"
        );


        setText(
            "trustFoulingTestEvents",

            integer(
                surface?.comparableTestEvents
            )
        );


        setText(
            "trustFoulingRelative",

            finite(
                surface?.responseRelativeToCleanPct
            )
                ? number(
                    surface.responseRelativeToCleanPct,
                    1
                )
                : "--"
        );


        setText(
            "trustFoulingRejected",

            integer(
                surface?.rejectedEvents
            )
        );


        const list =
            $("trustFoulingEvidenceList");


        if (!list) {
            return;
        }


        const evidence = [
            [
                surface?.repeatedResponseAttenuation === true,

                "Repeated comparable thermal events show reduced water-probe response."
            ],

            [
                surface?.waterResponseSuppression === true,

                "Water-probe dynamics are suppressed relative to the learned clean behaviour."
            ],

            [
                surface?.thermalRelationshipShift === true,

                "The air-water thermal relationship has shifted; this is supporting evidence only."
            ],

            [
                surface?.reservoirStableForAssessment === true,

                "Reservoir level remained stable enough for the current thermal comparison."
            ],

            [
                surface?.thermalExcitationStrongEnough === true,

                "A meaningful thermal excitation was available for response assessment."
            ],

            [
                surface?.currentEventComparableToBaseline === true,

                "The current event is comparable with the learned clean conditions."
            ]
        ];


        const active =
            evidence.filter(
                ([ok]) =>
                    ok
            );


        if (
            !surface?.assessmentAvailable &&
            active.length ===
            0
        ) {
            list.innerHTML =
                '<div><span class="status-dot unknown"></span><span>HydroSense is waiting for enough physically comparable thermal-response evidence. No algae claim is being made.</span></div>';

            return;
        }


        list.innerHTML =
            active.length
                ? active
                    .map(
                        ([, label]) =>
                            `<div><span class="status-dot good"></span><span>${escapeHtml(label)}</span></div>`
                    )
                    .join("")
                : '<div><span class="status-dot good"></span><span>No significant fouling-compatible response degradation is currently supported.</span></div>';
    }


    function renderAnomalyEvidence(
        anomaly
    ) {
        const overall =
            anomaly?.overallCondition ||
            "MEASUREMENT UNAVAILABLE";


        setPill(
            "trustAnomalyOverall",

            overall ===
                "NORMAL SENSOR BEHAVIOUR"
                ? "good"
                : overall.includes(
                    "WARNING"
                )
                    ? "bad"
                    : overall.includes(
                        "MONITOR"
                    )
                        ? "warn"
                        : "unknown",

            overall
        );


        const list =
            $("trustAnomalyList");


        if (!list) {
            return;
        }


        const sensors = [
            [
                "Air Temperature Sensor",
                anomaly?.airTemperature
            ],

            [
                "Humidity Sensor",
                anomaly?.humidity
            ],

            [
                "Water Temperature Sensor",
                anomaly?.waterTemperature
            ],

            [
                "Reservoir Distance Sensor",
                anomaly?.reservoirDistance
            ]
        ];


        const labels = {
            possibleStuckOutput:
                "Possible stuck sensor output",

            excessiveMeasurementNoise:
                "Excessive measurement noise",

            suddenMeasurementChange:
                "Sudden measurement change",

            changedResponseDynamics:
                "Changed response dynamics",

            suppressedResponseDynamics:
                "Suppressed response dynamics",

            amplifiedResponseDynamics:
                "Amplified response dynamics"
        };


        const rows =
            [];


        for (
            const [
                sensorLabel,
                sensor
            ]
            of sensors
        ) {
            const evidence =
                sensor?.evidence ||
                {};


            for (
                const [
                    key,
                    label
                ]
                of Object.entries(
                    labels
                )
            ) {
                if (
                    evidence[key] ===
                    true
                ) {
                    rows.push(
                        `<div class="trust-anomaly-item"><span class="status-dot warn"></span><div><strong>${escapeHtml(sensorLabel)}</strong>${escapeHtml(label)}</div></div>`
                    );
                }
            }
        }


        list.innerHTML =
            rows.length
                ? rows.join("")
                : '<div class="trust-anomaly-empty">No active sensor anomaly evidence is currently reported by the ESP32-S3.</div>';
    }


    function renderSensorTrust(
        payload
    ) {
        const trust =
            payload?.sensorTrust;


        const anomaly =
            payload?.anomaly;


        if (!trust) {
            setText(
                "trustOverallScore",
                "--"
            );


            setPill(
                "trustOverallState",
                "unknown",
                "SENSOR TRUST NOT AVAILABLE"
            );


            setText(
                "trustOverallCoverage",
                "--"
            );


            setText(
                "trustCommunicationIntegrity",
                "--"
            );


            setText(
                "trustBaselineState",
                "WAITING FOR SENSOR TRUST FIRMWARE"
            );


            renderTrustSensor(
                "trustAir",
                null
            );


            renderTrustSensor(
                "trustHumidity",
                null
            );


            renderTrustSensor(
                "trustWater",
                null
            );


            renderTrustSensor(
                "trustReservoir",
                null
            );


            renderFouling(
                null
            );


            renderAnomalyEvidence(
                null
            );


            return;
        }


        setText(
            "trustOverallScore",

            finite(
                trust.overallTrustScorePct
            )
                ? number(
                    trust.overallTrustScorePct,
                    1
                )
                : "--"
        );


        setPill(
            "trustOverallState",

            trustUiStatus(
                trust.overallState
            ),

            trust.overallState ||
            "UNKNOWN"
        );


        setText(
            "trustOverallCoverage",

            finite(
                trust.overallEvidenceCoveragePct
            )
                ? number(
                    trust.overallEvidenceCoveragePct,
                    1
                )
                : "--"
        );


        setText(
            "trustCommunicationIntegrity",

            finite(
                trust.communicationIntegrityPct
            )
                ? number(
                    trust.communicationIntegrityPct,
                    1
                )
                : "--"
        );


        setText(
            "trustBaselineState",

            trust.baselineReady
                ? "READY"
                : "LEARNING NORMAL BEHAVIOUR"
        );


        renderTrustSensor(
            "trustAir",
            trust.airTemperature
        );


        renderTrustSensor(
            "trustHumidity",
            trust.humidity
        );


        renderTrustSensor(
            "trustWater",
            trust.waterTemperature
        );


        renderTrustSensor(
            "trustReservoir",
            trust.reservoirDistance
        );


        renderFouling(
            anomaly?.surfaceFouling
        );


        renderAnomalyEvidence(
            anomaly
        );
    }


    // ============================================================
    // SENSOR TRUST EVENT HISTORY
    // ============================================================

    async function loadTrustEvents() {
        if (
            !state.selectedDeviceId
        ) {
            state.trustEvents =
                [];


            renderTrustEvents();


            return;
        }


        try {
            const result =
                await fetchJson(
                    `/api/v1/sensor-trust/events?deviceId=${encodeURIComponent(state.selectedDeviceId)}&limit=100`
                );


            state.trustEvents =
                Array.isArray(
                    result.data
                )
                    ? result.data
                    : [];


            renderTrustEvents();
        }

        catch (error) {
            state.trustEvents =
                [];


            renderTrustEvents();


            if (
                !String(
                    error.message
                ).includes(
                    "No Sensor Trust"
                )
            ) {
                console.warn(
                    "Sensor Trust event load failed:",

                    error.message
                );
            }
        }
    }


    function renderTrustEvents() {
        const body =
            $("trustEventTableBody");


        if (!body) {
            return;
        }


        body.innerHTML =
            "";


        if (
            !state.trustEvents
                .length
        ) {
            body.innerHTML =
                '<tr><td colspan="6" class="empty-table">No Sensor Trust transitions have been recorded yet. Normal unchanged operation intentionally creates no event rows.</td></tr>';

            return;
        }


        for (
            const event
            of state.trustEvents
        ) {
            const row =
                document.createElement(
                    "tr"
                );


            const trust =
                finite(
                    event.trustScorePct
                )
                    ? `${event.trustScorePct.toFixed(1)} / 100`
                    : "--";


            const severity =
                event.severity ||
                "INFO";


            row.innerHTML = `
                <td>${escapeHtml(dateTime(event.sourceTimestamp || event.createdAt))}</td>

                <td>
                    <strong>${escapeHtml(event.sensorLabel || event.sensorKey || "Sensor")}</strong>
                </td>

                <td>
                    <span class="event-severity ${escapeHtml(severity)}">${escapeHtml(severity)}</span>
                </td>

                <td>${escapeHtml(event.title || event.eventType || "Sensor Trust event")}</td>

                <td>${escapeHtml(trust)}</td>

                <td>${escapeHtml(event.message || event.primaryReason || "--")}</td>
            `;


            body.appendChild(
                row
            );
        }
    }


    // ============================================================
    // HISTORY
    // ============================================================

    function bindHistoryRanges() {
        $("historyRanges")
            ?.querySelectorAll(
                "button"
            )
            .forEach(
                (button) => {
                    button.addEventListener(
                        "click",

                        () => {
                            const hours =
                                Number(
                                    button.dataset
                                        .hours
                                );


                            if (
                                !Number.isFinite(
                                    hours
                                )
                            ) {
                                return;
                            }


                            state.historyHours =
                                hours;


                            $("historyRanges")
                                ?.querySelectorAll(
                                    "button"
                                )
                                .forEach(
                                    (b) =>
                                        b.classList
                                            .toggle(
                                                "active",

                                                b === button
                                            )
                                );


                            void loadHistory();
                        }
                    );
                }
            );
    }


    async function loadHistory() {
        if (
            !state.selectedDeviceId
        ) {
            state.historySeries =
                [];


            state.historySummary =
                null;


            renderHistory();


            return;
        }


        const requestId =
            ++state.historyRequestId;


        $("historyLoading")
            ?.classList
            .remove(
                "hidden"
            );


        const to =
            new Date();


        const from =
            new Date(
                to.getTime() -
                state.historyHours *
                3600000
            );


        const params =
            new URLSearchParams({
                deviceId:
                    state.selectedDeviceId,

                from:
                    from.toISOString(),

                to:
                    to.toISOString(),

                limit:
                    "5000"
            });


        try {
            const [
                historyResult,
                summaryResult
            ] =
                await Promise.all([
                    fetchJson(
                        `/api/v1/telemetry/history?${params}`
                    ),

                    fetchJson(
                        `/api/v1/telemetry/summary?${params}`
                    )
                ]);


            if (
                requestId !==
                state.historyRequestId
            ) {
                return;
            }


            state.historySeries =
                Array.isArray(
                    historyResult.data
                )
                    ? historyResult.data
                    : [];


            state.historySummary =
                summaryResult.data ||
                null;


            renderHistory();
        }

        catch (error) {
            if (
                requestId ===
                state.historyRequestId
            ) {
                state.historySeries =
                    [];


                state.historySummary =
                    null;


                renderHistory();


                showToast(
                    `History load failed: ${error.message}`
                );
            }
        }

        finally {
            if (
                requestId ===
                state.historyRequestId
            ) {
                $("historyLoading")
                    ?.classList
                    .add(
                        "hidden"
                    );
            }
        }
    }


    function renderHistory() {
        const s =
            state.historySummary ||
            {};


        setText(
            "historySamples",

            finite(
                s.count
            )
                ? integer(
                    s.count
                )
                : "0"
        );


        setText(
            "historyAirAvg",

            number(
                s.airTemperatureAvg,
                1
            )
        );


        setText(
            "historyWaterAvg",

            number(
                s.waterTemperatureAvg,
                2
            )
        );


        setText(
            "historyHumidityAvg",

            number(
                s.humidityAvg,
                1
            )
        );


        setText(
            "historyLevelAvg",

            number(
                s.waterLevelAvg,
                1
            )
        );


        setText(
            "historyTdsAvg",

            number(
                s.tdsAvg,
                1
            )
        );


        redrawHistoryCharts();
    }


    // ============================================================
    // CANVAS CHART ENGINE
    // ============================================================

    function prepareCanvas(canvas) {
        if (!canvas) {
            return null;
        }


        const rect =
            canvas.getBoundingClientRect();


        const width =
            Math.max(
                300,

                Math.floor(
                    rect.width
                )
            );


        const height =
            Math.max(
                180,

                Math.floor(
                    rect.height
                )
            );


        const ratio =
            Math.min(
                devicePixelRatio ||
                1,

                2
            );


        canvas.width =
            Math.floor(
                width *
                ratio
            );


        canvas.height =
            Math.floor(
                height *
                ratio
            );


        const ctx =
            canvas.getContext(
                "2d"
            );


        ctx.setTransform(
            ratio,
            0,
            0,
            ratio,
            0,
            0
        );


        return {
            ctx,
            width,
            height
        };
    }


    function formatAxis(value) {
        const a =
            Math.abs(
                value
            );


        return value.toFixed(
            a >= 100
                ? 0
                : a >= 10
                    ? 1
                    : 2
        );
    }


    function chartTime(
        value,
        longTime
    ) {
        const date =
            new Date(value);


        if (
            Number.isNaN(
                date.getTime()
            )
        ) {
            return "";
        }


        return longTime
            ? date.toLocaleString(
                [],
                {
                    month: "short",
                    day: "numeric",
                    hour: "2-digit",
                    minute: "2-digit"
                }
            )
            : date.toLocaleTimeString(
                [],
                {
                    hour: "2-digit",
                    minute: "2-digit"
                }
            );
    }


    function drawChart(
        canvasId,
        points,
        series,
        emptyId,
        options = {}
    ) {
        const canvas =
            $(canvasId);


        const empty =
            $(emptyId);


        const prepared =
            prepareCanvas(
                canvas
            );


        if (!prepared) {
            return;
        }


        const {
            ctx,
            width,
            height
        } =
            prepared;


        ctx.clearRect(
            0,
            0,
            width,
            height
        );


        const data =
            Array.isArray(
                points
            )
                ? points
                : [];


        const values =
            [];


        series.forEach(
            (line) =>
                data.forEach(
                    (point) => {
                        const value =
                            line.value(
                                point
                            );


                        if (
                            finite(value)
                        ) {
                            values.push(
                                value
                            );
                        }
                    }
                )
        );


        if (
            data.length <
                2 ||

            values.length <
                2
        ) {
            empty
                ?.classList
                .remove(
                    "hidden"
                );

            return;
        }


        empty
            ?.classList
            .add(
                "hidden"
            );


        const pad = {
            left: 48,
            right: 18,
            top: 18,
            bottom: 32
        };


        const plotW =
            width -
            pad.left -
            pad.right;


        const plotH =
            height -
            pad.top -
            pad.bottom;


        let min =
            Math.min(
                ...values
            );


        let max =
            Math.max(
                ...values
            );


        if (
            Math.abs(
                max -
                min
            ) <
            0.0001
        ) {
            const bump =
                Math.abs(
                    max ||
                    1
                ) *
                0.05 ||
                1;


            min -=
                bump;


            max +=
                bump;
        }


        const span =
            max -
            min;


        min -=
            span *
            0.08;


        max +=
            span *
            0.08;


        const y =
            (value) =>
                pad.top +
                (
                    1 -
                    (
                        value -
                        min
                    ) /
                    (
                        max -
                        min
                    )
                ) *
                plotH;


        const x =
            (index) =>
                pad.left +
                index /
                Math.max(
                    1,

                    data.length -
                    1
                ) *
                plotW;


        ctx.font =
            "10px system-ui";


        ctx.textAlign =
            "right";


        ctx.textBaseline =
            "middle";


        for (
            let row = 0;
            row <= 4;
            row++
        ) {
            const ratio =
                row /
                4;


            const py =
                pad.top +
                ratio *
                plotH;


            const value =
                max -
                ratio *
                (
                    max -
                    min
                );


            ctx.strokeStyle =
                palette.grid;


            ctx.beginPath();


            ctx.moveTo(
                pad.left,
                py
            );


            ctx.lineTo(
                width -
                pad.right,
                py
            );


            ctx.stroke();


            ctx.fillStyle =
                palette.axis;


            ctx.fillText(
                formatAxis(
                    value
                ),

                pad.left -
                8,

                py
            );
        }


        const labels =
            Math.min(
                5,

                data.length
            );


        ctx.textAlign =
            "center";


        ctx.textBaseline =
            "top";


        ctx.fillStyle =
            palette.axis;


        for (
            let tick = 0;
            tick < labels;
            tick++
        ) {
            const index =
                Math.round(
                    tick /
                    Math.max(
                        1,

                        labels -
                        1
                    ) *
                    (
                        data.length -
                        1
                    )
                );


            ctx.fillText(
                chartTime(
                    data[index].time ||
                    data[index].timestamp,

                    options.longTime
                ),

                x(
                    index
                ),

                height -
                pad.bottom +
                10
            );
        }


        series.forEach(
            (line) => {
                ctx.strokeStyle =
                    line.color;


                ctx.lineWidth =
                    2;


                ctx.lineJoin =
                    "round";


                ctx.lineCap =
                    "round";


                let started =
                    false;


                ctx.beginPath();


                data.forEach(
                    (
                        point,
                        index
                    ) => {
                        const value =
                            line.value(
                                point
                            );


                        if (
                            !finite(
                                value
                            )
                        ) {
                            started =
                                false;

                            return;
                        }


                        if (
                            !started
                        ) {
                            ctx.moveTo(
                                x(index),

                                y(value)
                            );


                            started =
                                true;
                        }

                        else {
                            ctx.lineTo(
                                x(index),

                                y(value)
                            );
                        }
                    }
                );


                ctx.stroke();
            }
        );
    }


    function redrawRealtimeCharts() {
        const p =
            state.realtimeSeries;


        drawChart(
            "overviewChart",

            p,

            [
                {
                    color:
                        palette.air,

                    value:
                        (x) =>
                            x.air
                },

                {
                    color:
                        palette.water,

                    value:
                        (x) =>
                            x.water
                },

                {
                    color:
                        palette.humidity,

                    value:
                        (x) =>
                            x.humidity
                },

                {
                    color:
                        palette.level,

                    value:
                        (x) =>
                            x.level
                }
            ],

            "overviewChartEmpty"
        );


        drawChart(
            "liveEnvironmentChart",

            p,

            [
                {
                    color:
                        palette.air,

                    value:
                        (x) =>
                            x.air
                },

                {
                    color:
                        palette.water,

                    value:
                        (x) =>
                            x.water
                },

                {
                    color:
                        palette.humidity,

                    value:
                        (x) =>
                            x.humidity
                }
            ],

            "liveEnvironmentEmpty"
        );


        drawChart(
            "liveReservoirChart",

            p,

            [
                {
                    color:
                        palette.level,

                    value:
                        (x) =>
                            x.level
                },

                {
                    color:
                        palette.tds,

                    value:
                        (x) =>
                            x.tds
                }
            ],

            "liveReservoirEmpty"
        );


        drawChart(
            "aiTdsChart",

            p,

            [
                {
                    color:
                        palette.tds,

                    value:
                        (x) =>
                            x.tds
                }
            ],

            "aiTdsEmpty"
        );
    }


    function redrawHistoryCharts() {
        const p =
            state.historySeries;


        drawChart(
            "historyEnvironmentChart",

            p,

            [
                {
                    color:
                        palette.air,

                    value:
                        (x) =>
                            valueOrNull(
                                x.measured
                                    ?.airTemperatureC
                            )
                },

                {
                    color:
                        palette.water,

                    value:
                        (x) =>
                            valueOrNull(
                                x.measured
                                    ?.waterTemperatureC
                            )
                },

                {
                    color:
                        palette.humidity,

                    value:
                        (x) =>
                            valueOrNull(
                                x.measured
                                    ?.humidityPct
                            )
                }
            ],

            "historyEnvironmentEmpty",

            {
                longTime:
                    state.historyHours >
                    24
            }
        );


        drawChart(
            "historyReservoirChart",

            p,

            [
                {
                    color:
                        palette.level,

                    value:
                        (x) =>
                            valueOrNull(
                                x.measured
                                    ?.waterLevelPct
                            )
                },

                {
                    color:
                        palette.tds,

                    value:
                        (x) =>
                            valueOrNull(
                                x.estimated
                                    ?.tdsPpm
                            )
                },

                {
                    color:
                        palette.vpd,

                    value:
                        (x) =>
                            valueOrNull(
                                x.derived
                                    ?.vpdKPa
                            )
                }
            ],

            "historyReservoirEmpty",

            {
                longTime:
                    state.historyHours >
                    24
            }
        );
    }


    // ============================================================
    // ML CONTROL HELPERS
    // ============================================================

    function mlInputValue(id) {
        const raw =
            $(id)
                ?.value
                ?.trim();


        if (!raw) {
            return null;
        }


        const value =
            Number(raw);


        return Number.isFinite(
            value
        )
            ? value
            : null;
    }


    function setInputValue(
        id,
        value
    ) {
        const el =
            $(id);


        if (el) {
            el.value =
                finite(value)
                    ? String(value)
                    : "";
        }
    }


    function mlSelectedFeatures() {
        return Array.from(
            document.querySelectorAll(
                '#mlFeatureGrid input[type="checkbox"]:checked'
            )
        )
            .map(
                (input) =>
                    input.value
            );
    }


    // ============================================================
    // ML FEATURE SELECTOR
    // ============================================================

    function renderMLFeatureSelector(
        catalog,
        selected
    ) {
        const grid =
            $("mlFeatureGrid");


        if (!grid) {
            return;
        }


        const selectedSet =
            new Set(
                selected ||
                []
            );


        grid.innerHTML =
            (
                catalog ||
                []
            )
                .map(
                    (feature) => `
                        <label class="ml-feature-option">

                            <input
                                type="checkbox"
                                value="${escapeHtml(feature.key)}"
                                ${selectedSet.has(feature.key) ? "checked" : ""}
                            >

                            <span>
                                <strong>${escapeHtml(feature.label)}</strong>

                                <small>
                                    ${escapeHtml(feature.key)}
                                    ${feature.unit ? ` · ${escapeHtml(feature.unit)}` : ""}
                                    ${feature.geometryRequired ? " · geometry" : ""}
                                </small>
                            </span>

                        </label>
                    `
                )
                .join("");
    }


    // ============================================================
    // RESERVOIR FORM
    // ============================================================

    function updateMLGeometryFields() {
        const shape =
            $("mlReservoirShape")
                ?.value ||
            "UNCONFIGURED";


        document
            .querySelectorAll(
                ".ml-rect-field"
            )
            .forEach(
                (el) =>
                    el.classList
                        .toggle(
                            "hidden",

                            shape !==
                            "RECTANGULAR"
                        )
            );


        document
            .querySelectorAll(
                ".ml-cylinder-field"
            )
            .forEach(
                (el) =>
                    el.classList
                        .toggle(
                            "hidden",

                            shape !==
                            "VERTICAL_CYLINDER"
                        )
            );
    }


    function renderMLConfigForm(
        force = false
    ) {
        const data =
            state.mlControl;


        if (
            !data?.config ||
            !state.selectedDeviceId
        ) {
            return;
        }


        if (
            !force &&
            state.mlFormDeviceId ===
                state.selectedDeviceId
        ) {
            return;
        }


        state.mlFormDeviceId =
            state.selectedDeviceId;


        const reservoir =
            data.config.reservoir ||
            {};


        const baseline =
            data.config.baseline ||
            {};


        const isolationForest =
            data.config.isolationForest ||
            {};


        if (
            $("mlReservoirShape")
        ) {
            $("mlReservoirShape").value =
                reservoir.shape ||
                "UNCONFIGURED";
        }


        setInputValue(
            "mlReservoirHeight",
            reservoir.heightCm
        );


        setInputValue(
            "mlReservoirLength",
            reservoir.lengthCm
        );


        setInputValue(
            "mlReservoirWidth",
            reservoir.widthCm
        );


        setInputValue(
            "mlReservoirDiameter",
            reservoir.diameterCm
        );


        setInputValue(
            "mlFullDistance",
            reservoir.fullDistanceCm
        );


        setInputValue(
            "mlEmptyDistance",
            reservoir.emptyDistanceCm
        );


        setInputValue(
            "mlTargetHours",
            baseline.targetDurationHours
        );


        setInputValue(
            "mlMinCoverage",
            baseline.minimumCoveragePct
        );


        setInputValue(
            "mlGapMultiplier",
            baseline.maximumGapMultiplier
        );


        setInputValue(
            "mlRobustZ",
            baseline.robustZThreshold
        );


        setInputValue(
            "mlStrangeSamples",
            baseline.sustainedStrangeSamples
        );


        setInputValue(
            "mlMissingSeconds",
            baseline.missingWarningSeconds
        );


        setInputValue(
            "mlUnchangedSeconds",
            baseline.unchangedWarningSeconds
        );


        setInputValue(
            "mlCalibrationSamples",
            baseline.minimumCalibrationSamplesPerClass
        );


        setInputValue(
            "mlTrees",
            isolationForest.nEstimators
        );


        setInputValue(
            "mlTrainFraction",
            isolationForest.trainFraction
        );


        setInputValue(
            "mlRandomState",
            isolationForest.randomState
        );


        renderMLFeatureSelector(
            data.featureCatalog,

            baseline.features
        );


        updateMLGeometryFields();
    }


    // ============================================================
    // GEOMETRY PREVIEW
    // ============================================================

    function geometryPreview() {
        const config =
            state.mlControl
                ?.config
                ?.reservoir ||
            {};


        const measured =
            state.latestPayload
                ?.measured ||

            state.mlLive
                ?.livePacket
                ?.measured ||

            {};


        const distance =
            measured.stableDistanceValid ===
                true &&
            finite(
                measured.stableDistanceCm
            )
                ? measured.stableDistanceCm
                : null;


        setText(
            "mlCurrentDistance",

            finite(distance)
                ? `${distance.toFixed(2)} cm`
                : "--"
        );


        if (
            !finite(distance) ||

            !finite(
                config.fullDistanceCm
            ) ||

            !finite(
                config.emptyDistanceCm
            ) ||

            config.emptyDistanceCm <=
                config.fullDistanceCm
        ) {
            setText(
                "mlGeometryLevel",
                "--"
            );


            setText(
                "mlGeometryVolume",
                "--"
            );


            return;
        }


        const fraction =
            (
                config.emptyDistanceCm -
                distance
            ) /
            (
                config.emptyDistanceCm -
                config.fullDistanceCm
            );


        setText(
            "mlGeometryLevel",

            `${
                (
                    fraction *
                    100
                ).toFixed(1)
            }%${
                fraction < 0 ||
                fraction > 1
                    ? " · OUTSIDE CAL"
                    : ""
            }`
        );


        const bounded =
            Math.max(
                0,

                Math.min(
                    1,
                    fraction
                )
            );


        if (
            !finite(
                config.heightCm
            ) ||

            config.heightCm <=
                0
        ) {
            setText(
                "mlGeometryVolume",
                "--"
            );


            return;
        }


        const waterHeight =
            bounded *
            config.heightCm;


        let litres =
            null;


        if (
            config.shape ===
                "RECTANGULAR" &&

            finite(
                config.lengthCm
            ) &&

            finite(
                config.widthCm
            )
        ) {
            litres =
                config.lengthCm *
                config.widthCm *
                waterHeight /
                1000;
        }

        else if (
            config.shape ===
                "VERTICAL_CYLINDER" &&

            finite(
                config.diameterCm
            )
        ) {
            litres =
                Math.PI *
                Math.pow(
                    config.diameterCm /
                    2,

                    2
                ) *
                waterHeight /
                1000;
        }


        setText(
            "mlGeometryVolume",

            finite(litres)
                ? `${litres.toFixed(2)} L`
                : "--"
        );
    }


    // ============================================================
    // FORMAT LIVE / STORED SAMPLE
    // ============================================================

    function formatSample(
        payload,
        timestamp,
        prefix
    ) {
        if (!payload) {
            return `${prefix}: no data available.`;
        }


        const measured =
            payload.measured ||
            {};


        const estimated =
            payload.estimated ||
            {};


        const parts = [
            `Time ${
                timestamp
                    ? dateTime(
                        timestamp
                    )
                    : "--"
            }`,

            `Air ${
                finite(
                    measured.airTemperatureC
                )
                    ? measured.airTemperatureC.toFixed(2) +
                        " °C"
                    : "--"
            }`,

            `Humidity ${
                finite(
                    measured.humidityPct
                )
                    ? measured.humidityPct.toFixed(2) +
                        "%"
                    : "--"
            }`,

            `Water ${
                finite(
                    measured.waterTemperatureC
                )
                    ? measured.waterTemperatureC.toFixed(2) +
                        " °C"
                    : "--"
            }`,

            `Distance ${
                finite(
                    measured.stableDistanceCm
                )
                    ? measured.stableDistanceCm.toFixed(2) +
                        " cm"
                    : "--"
            }`,

            `Level ${
                finite(
                    measured.waterLevelPct
                )
                    ? measured.waterLevelPct.toFixed(2) +
                        "%"
                    : "--"
            }`,

            `TDS ${
                finite(
                    estimated.tdsPpm
                )
                    ? estimated.tdsPpm.toFixed(2) +
                        " ppm"
                    : "--"
            }`
        ];


        return parts.join(
            " · "
        );
    }


    // ============================================================
    // BASELINE STATUS
    // ============================================================

    function renderMLBaseline() {
        const baseline =
            state.mlControl
                ?.baseline ||
            {};


        const session =
            baseline.session ||
            null;


        const baselineLabel =
            !session
                ? "BASELINE NOT STARTED"

                : session.status ===
                    "ACTIVE"
                    ? "COLLECTING"

                    : session.status;


        setPill(
            "mlBaselineState",

            session?.status ===
                "ACTIVE"
                ? "warn"

                : baseline.trainingEligible
                    ? "good"

                    : session
                        ? "warn"

                        : "unknown",

            baselineLabel
        );


        setText(
            "mlStoredSamples",

            finite(
                baseline.storedSamples
            )
                ? integer(
                    baseline.storedSamples
                )
                : "0"
        );


        setText(
            "mlCompleteSamples",

            finite(
                baseline.completeSamples
            )
                ? integer(
                    baseline.completeSamples
                )
                : "0"
        );


        setText(
            "mlRequiredSamples",

            finite(
                baseline.requiredSamples
            )
                ? integer(
                    baseline.requiredSamples
                )
                : "--"
        );


        setText(
            "mlCoverage",

            finite(
                baseline.coveragePct
            )
                ? `${baseline.coveragePct.toFixed(1)}%`
                : "--"
        );


        setText(
            "mlCoverageDetail",

            finite(
                baseline.minimumCoveragePct
            )
                ? `minimum ${baseline.minimumCoveragePct.toFixed(1)}%`
                : "minimum not configured"
        );


        setText(
            "mlElapsed",

            finite(
                baseline.elapsedHours
            )
                ? `${baseline.elapsedHours.toFixed(2)} h`
                : "--"
        );


        setText(
            "mlDurationDetail",

            finite(
                baseline.targetHours
            )
                ? `target ${baseline.targetHours.toFixed(2)} h`
                : "target not configured"
        );


        setText(
            "mlCadence",

            finite(
                baseline.observedMedianCadenceSeconds
            )
                ? `${baseline.observedMedianCadenceSeconds.toFixed(1)} s`
                : "--"
        );


        const durationPct =
            finite(
                baseline.elapsedHours
            ) &&

            finite(
                baseline.targetHours
            ) &&

            baseline.targetHours >
                0
                ? baseline.elapsedHours /
                    baseline.targetHours *
                    100

                : 0;


        if (
            $("mlBaselineProgress")
        ) {
            $("mlBaselineProgress").style.width =
                `${
                    Math.min(
                        100,

                        Math.max(
                            0,
                            durationPct
                        )
                    )
                }%`;
        }


        const stored =
            state.mlControl
                ?.historicalStorage;


        setPill(
            "mlStorageState",

            stored
                ? "good"
                : "unknown",

            stored
                ? `MONGODB · ${
                    stored.collection ||
                    "STORING"
                }`
                : "NO STORED SAMPLE"
        );


        setText(
            "mlStoredSample",

            stored
                ? formatSample(
                    stored,

                    stored.timestamp,

                    "MongoDB"
                )

                : "No historical telemetry found for the selected device."
        );
    }


    // ============================================================
    // ML WARNINGS
    // ============================================================

    function renderMLWarnings() {
        const baseline =
            state.mlControl
                ?.baseline ||
            {};


        const liveWarnings =
            state.mlLive
                ?.warnings ||

            state.mlControl
                ?.realtimeML
                ?.warnings ||

            [];


        const rows = [
            ...(
                baseline.blockers ||
                []
            ),

            ...(
                baseline.warnings ||
                []
            ),

            ...liveWarnings
        ];


        const list =
            $("mlWarningList");


        if (!list) {
            return;
        }


        setPill(
            "mlWarningCount",

            rows.some(
                (x) =>
                    x.severity ===
                    "BLOCKER"
            )
                ? "bad"

                : rows.length
                    ? "warn"

                    : "good",

            rows.length
                ? `${rows.length} ACTIVE`
                : "NO ACTIVE WARNINGS"
        );


        if (
            !rows.length
        ) {
            list.innerHTML =
                '<div class="ml-warning-item good"><strong>No active warning</strong><span>The currently evaluated baseline and live integrity checks report no active issue.</span></div>';

            return;
        }


        list.innerHTML =
            rows
                .map(
                    (item) => `
                        <div class="ml-warning-item ${
                            item.severity ===
                            "BLOCKER"
                                ? "bad"
                                : "warn"
                        }">

                            <div>
                                <strong>${
                                    escapeHtml(
                                        item.title ||
                                        item.code ||
                                        item.severity
                                    )
                                }</strong>

                                <small>${
                                    escapeHtml(
                                        item.feature ||
                                        item.code ||
                                        "system"
                                    )
                                }</small>
                            </div>

                            <span>${
                                escapeHtml(
                                    item.message ||
                                    ""
                                )
                            }</span>

                        </div>
                    `
                )
                .join("");
    }


    // ============================================================
    // BASELINE FEATURE AUDIT
    // ============================================================

    function renderMLFeatureAudit() {
        const body =
            $("mlFeatureAuditBody");


        if (!body) {
            return;
        }


        const analysis =
            state.mlControl
                ?.baseline
                ?.featureAudit ||
            {};


        const catalog =
            new Map(
                (
                    state.mlControl
                        ?.featureCatalog ||
                    []
                )
                    .map(
                        (item) => [
                            item.key,
                            item
                        ]
                    )
            );


        const entries =
            Object.entries(
                analysis
            );


        if (
            !entries.length
        ) {
            body.innerHTML =
                '<tr><td colspan="6" class="empty-table">No baseline analysis yet.</td></tr>';

            return;
        }


        body.innerHTML =
            entries
                .map(
                    ([
                        key,
                        item
                    ]) => `
                        <tr>

                            <td>
                                <strong>${
                                    escapeHtml(
                                        catalog.get(key)
                                            ?.label ||
                                        key
                                    )
                                }</strong>

                                <br>

                                <span class="panel-caption">
                                    ${escapeHtml(key)}
                                </span>
                            </td>


                            <td>
                                ${integer(item.present)} /
                                ${integer(item.total)}

                                ${
                                    finite(item.missing) &&
                                    item.missing > 0
                                        ? `<br><span class="panel-caption">${integer(item.missing)} missing</span>`
                                        : ""
                                }
                            </td>


                            <td>
                                ${
                                    item.zeroVariance
                                        ? '1 <span class="inline-bad">ZERO VARIANCE</span>'
                                        : "--"
                                }
                            </td>


                            <td>
                                ${
                                    finite(item.minimum) &&
                                    finite(item.maximum)
                                        ? `${item.minimum.toFixed(4)} → ${item.maximum.toFixed(4)}`
                                        : "--"
                                }
                            </td>


                            <td>
                                ${integer(item.robustOutlierCount)}
                                · max run
                                ${integer(item.longestRobustOutlierRun)}
                            </td>


                            <td>
                                ${integer(item.longestExactRepeatRun)}
                            </td>

                        </tr>
                    `
                )
                .join("");
    }


    // ============================================================
    // EXPERIMENT COUNTS
    // ============================================================

    function experimentCounts() {
        const experiments =
            state.mlControl
                ?.experiments ||
            [];


        let healthy =
            0;


        let fault =
            0;


        for (
            const experiment
            of experiments
        ) {
            const count =
                finite(
                    experiment.scoredSampleCount
                )
                    ? experiment.scoredSampleCount
                    : 0;


            if (
                experiment.label ===
                "HEALTHY_CONTROL"
            ) {
                healthy +=
                    count;
            }

            else if (
                experiment.isFault ===
                true
            ) {
                fault +=
                    count;
            }
        }


        return {
            healthy,
            fault
        };
    }


    // ============================================================
    // ML MODEL STATUS
    // ============================================================

    function renderMLModels() {
        const model =
            state.mlControl
                ?.model ||
            null;


        const training =
            state.mlControl
                ?.training ||
            null;


        const live =
            state.mlLive ||

            state.mlControl
                ?.realtimeML ||

            null;


        const iso =
            live?.isolationForest ||
            {};


        const trainingActive =
            [
                "STARTING",
                "RUNNING"
            ].includes(
                training?.status
            );


        const modelActive =
            model?.active ===
                true &&

            model?.status ===
                "READY";


        setPill(
            "mlIsolationState",

            modelActive
                ? "good"

                : trainingActive
                    ? "warn"

                    : model?.status ===
                        "FAILED"
                        ? "bad"

                        : "unknown",

            modelActive
                ? "ACTIVE"

                : trainingActive
                    ? "TRAINING"

                    : model?.status ||
                        "NOT TRAINED"
        );


        setPill(
            "mlModelState",

            modelActive
                ? "good"

                : trainingActive
                    ? "warn"

                    : "unknown",

            modelActive
                ? "ISOLATION FOREST ACTIVE"

                : trainingActive
                    ? "TRAINING"

                    : "MODEL NOT ACTIVE"
        );


        setText(
            "mlDecisionScore",

            finite(
                iso.decisionFunction
            )
                ? iso.decisionFunction
                    .toFixed(6)
                : "--"
        );


        setText(
            "mlModelVersion",

            model?.versionLabel ||
            "--"
        );


        setText(
            "mlModelSamples",

            finite(
                model?.sampleCounts
                    ?.training
            ) ||

            finite(
                model?.sampleCounts
                    ?.validation
            )
                ? `${
                    integer(
                        model?.sampleCounts
                            ?.training
                    )
                } / ${
                    integer(
                        model?.sampleCounts
                            ?.validation
                    )
                } train/val`

                : "--"
        );


        const cutoff =
            model?.threshold
                ?.decisionFunctionThreshold;


        setText(
            "mlThresholdState",

            model?.threshold
                ?.calibrated ===
                true &&

            finite(
                cutoff
            )
                ? `${
                    cutoff.toFixed(6)
                } · CALIBRATED`

                : "NOT CALIBRATED"
        );


        setText(
            "mlClassification",

            iso.classification ||
            "--"
        );


        setPill(
            "mlCalibrationState",

            model?.threshold
                ?.calibrated ===
                true
                ? "good"

                : "warn",

            model?.threshold
                ?.calibrated ===
                true
                ? "CALIBRATED"

                : "NOT CALIBRATED"
        );


        const trainButton =
            $("mlTrainButton");


        if (trainButton) {
            trainButton.disabled =
                state.mlControl
                    ?.baseline
                    ?.trainingEligible !==
                    true ||

                trainingActive;
        }


        const counts =
            experimentCounts();


        const minimum =
            state.mlControl
                ?.config
                ?.baseline
                ?.minimumCalibrationSamplesPerClass;


        setText(
            "mlHealthyExperimentSamples",

            integer(
                counts.healthy
            )
        );


        setText(
            "mlFaultExperimentSamples",

            integer(
                counts.fault
            )
        );


        setText(
            "mlCalibrationMinimum",

            finite(
                minimum
            )
                ? integer(
                    minimum
                )
                : "--"
        );


        const calibrate =
            $("mlCalibrateButton");


        if (calibrate) {
            calibrate.disabled =
                !(
                    modelActive &&

                    finite(
                        minimum
                    ) &&

                    counts.healthy >=
                        minimum &&

                    counts.fault >=
                        minimum
                );
        }


        const activeExperiment =
            live?.activeExperiment ||

            (
                state.mlControl
                    ?.experiments ||
                []
            )
                .find(
                    (experiment) =>
                        experiment.status ===
                        "ACTIVE"
                ) ||

            null;


        setPill(
            "mlExperimentState",

            activeExperiment
                ? "warn"
                : "unknown",

            activeExperiment
                ? `${activeExperiment.label} ACTIVE`
                : "NO ACTIVE EXPERIMENT"
        );
    }


    // ============================================================
    // TRAINING STATUS
    // ============================================================

    function renderMLTraining() {
        const training =
            state.mlControl
                ?.training;


        const el =
            $("mlTrainingStatus");


        if (!el) {
            return;
        }


        if (!training) {
            el.textContent =
                state.mlControl
                    ?.baseline
                    ?.trainingEligible

                    ? "Baseline is eligible. Training will use only complete selected features from this baseline session."

                    : "Training is locked until the baseline passes every configured eligibility check.";


            return;
        }


        const parts = [
            training.stage ||
            training.status ||
            "TRAINING"
        ];


        if (
            finite(
                training.progressPct
            )
        ) {
            parts.push(
                `${training.progressPct.toFixed(0)}%`
            );
        }


        if (
            training.message
        ) {
            parts.push(
                training.message
            );
        }


        if (
            training.error
        ) {
            parts.push(
                `ERROR: ${training.error}`
            );
        }


        el.textContent =
            parts.join(
                " · "
            );
    }


    // ============================================================
    // MODEL REGISTRY TABLE
    // ============================================================

    function renderMLModelTable() {
        const body =
            $("mlModelTableBody");


        if (!body) {
            return;
        }


        const models =
            state.mlModels ||
            [];


        if (
            !models.length
        ) {
            body.innerHTML =
                '<tr><td colspan="7" class="empty-table">No Isolation Forest model artifacts yet.</td></tr>';

            return;
        }


        body.innerHTML =
            models
                .map(
                    (model) => {
                        const threshold =
                            model
                                ?.threshold
                                ?.decisionFunctionThreshold;


                        const thresholdText =
                            model
                                ?.threshold
                                ?.calibrated ===
                                true &&

                            finite(
                                threshold
                            )
                                ? threshold.toFixed(6)

                                : "NOT CALIBRATED";


                        const statusText =
                            model.active ===
                                true
                                ? "ACTIVE"

                                : model.status ||
                                    "--";


                        let action =
                            "--";


                        if (
                            model.active ===
                            true
                        ) {
                            action =
                                "ACTIVE";
                        }

                        else if (
                            model.status ===
                            "READY"
                        ) {
                            action =
                                `<button class="ghost-button ml-activate-model" data-model-id="${escapeHtml(model._id)}">Activate</button>`;
                        }


                        return `
                            <tr>

                                <td>
                                    <strong>${escapeHtml(model.versionLabel || "--")}</strong>
                                </td>

                                <td>
                                    ${escapeHtml(statusText)}
                                </td>

                                <td>
                                    ${escapeHtml((model.features || []).join(", "))}
                                </td>

                                <td>
                                    ${integer(model?.sampleCounts?.training)}
                                    /
                                    ${integer(model?.sampleCounts?.validation)}
                                </td>

                                <td>
                                    ${escapeHtml(thresholdText)}
                                </td>

                                <td>
                                    ${escapeHtml(dateTime(model.createdAt))}
                                </td>

                                <td>
                                    ${action}
                                </td>

                            </tr>
                        `;
                    }
                )
                .join("");


        body.querySelectorAll(
            ".ml-activate-model"
        )
            .forEach(
                (button) => {
                    button.addEventListener(
                        "click",

                        async () => {
                            try {
                                await sendJson(
                                    `/api/v1/ml-control/models/${encodeURIComponent(button.dataset.modelId)}/activate`,

                                    "POST",

                                    {
                                        deviceId:
                                            state.selectedDeviceId
                                    }
                                );


                                showToast(
                                    "Model activated."
                                );


                                await loadMLControl({
                                    preserveForm:
                                        true
                                });
                            }

                            catch (error) {
                                showToast(
                                    error.message
                                );
                            }
                        }
                    );
                }
            );
    }


    // ============================================================
    // LIVE ML STATE
    // ============================================================

    function renderMLLive() {
        const payload =
            state.latestPayload;


        const live =
            state.mlLive ||

            state.mlControl
                ?.realtimeML ||

            null;


        const tiny =
            payload?.estimated ||

            live?.livePacket
                ?.estimated ||

            {};


        const anomaly =
            payload?.anomaly ||

            live?.edgeAnomaly ||

            {};


        // --------------------------------------------------------
        // TINYML
        // --------------------------------------------------------

        setPill(
            "mlTinymlState",

            tiny.valid ===
                true
                ? "good"

                : tiny.valid ===
                    false
                    ? "bad"

                    : "unknown",

            tiny.valid ===
                true
                ? "VALID"

                : tiny.valid ===
                    false
                    ? "UNAVAILABLE"

                    : "WAITING"
        );


        setText(
            "mlTinymlTds",

            tiny.valid ===
                true &&

            finite(
                tiny.tdsPpm
            )
                ? tiny.tdsPpm
                    .toFixed(1)

                : "--"
        );


        setText(
            "mlTinymlName",

            tiny.model ||
            "--"
        );


        setText(
            "mlTinymlLatency",

            finite(
                tiny.inferenceMicros
            )
                ? `${
                    integer(
                        tiny.inferenceMicros
                    )
                } µs`

                : "--"
        );


        // --------------------------------------------------------
        // EDGE ANOMALY
        // --------------------------------------------------------

        const condition =
            anomaly?.overallCondition ||
            "WAITING";


        setPill(
            "mlEdgeAnomalyState",

            condition ===
                "NORMAL SENSOR BEHAVIOUR"
                ? "good"

                : condition.includes(
                    "WARNING"
                )
                    ? "bad"

                    : condition ===
                        "WAITING"
                        ? "unknown"

                        : "warn",

            condition
        );


        setText(
            "mlEdgeEvidence",

            finite(
                anomaly?.overallAnomalyEvidenceIndexPct
            )
                ? anomaly
                    .overallAnomalyEvidenceIndexPct
                    .toFixed(1)

                : "--"
        );


        setText(
            "mlEdgeBaseline",

            anomaly?.baselineReady ===
                true
                ? "READY"

                : anomaly?.baselineReady ===
                    false
                    ? "LEARNING"

                    : "--"
        );


        setText(
            "mlEdgeCondition",

            condition
        );


        // --------------------------------------------------------
        // REALTIME SAMPLE
        // --------------------------------------------------------

        setText(
            "mlLiveSample",

            payload
                ? formatSample(
                    payload,

                    payload.serverTimestamp ||
                    new Date()
                        .toISOString(),

                    "Realtime"
                )

                : "Waiting for S3 realtime data."
        );


        geometryPreview();

        renderMLWarnings();

        renderMLModels();
    }


    // ============================================================
    // COMPLETE ML CONTROL RENDER
    // ============================================================

    function renderMLControl(
        {
            forceForm = false
        } = {}
    ) {
        if (
            !state.mlControl
        ) {
            return;
        }


        renderMLConfigForm(
            forceForm
        );


        renderMLBaseline();

        renderMLWarnings();

        renderMLFeatureAudit();

        renderMLModels();

        renderMLTraining();

        renderMLModelTable();

        renderMLLive();


        const shape =
            state.mlControl
                .config
                ?.reservoir
                ?.shape ||

            "UNCONFIGURED";


        setPill(
            "mlGeometryState",

            shape ===
                "UNCONFIGURED"
                ? "unknown"

                : "good",

            shape.replaceAll(
                "_",
                " "
            )
        );
    }


    // ============================================================
    // LOAD ML CONTROL
    // ============================================================

    async function loadMLControl(
        {
            preserveForm = false
        } = {}
    ) {
        if (
            !state.selectedDeviceId
        ) {
            return;
        }


        try {
            const deviceId =
                encodeURIComponent(
                    state.selectedDeviceId
                );


            const [
                statusResult,
                modelResult
            ] =
                await Promise.all([
                    fetchJson(
                        `/api/v1/ml-control/status?deviceId=${deviceId}`
                    ),

                    fetchJson(
                        `/api/v1/ml-control/models?deviceId=${deviceId}`
                    )
                ]);


            state.mlControl =
                statusResult.data ||
                null;


            state.mlModels =
                Array.isArray(
                    modelResult.data
                )
                    ? modelResult.data
                    : [];


            if (
                state.mlControl
                    ?.realtimeML
            ) {
                state.mlLive =
                    state.mlControl
                        .realtimeML;
            }


            renderMLControl({
                forceForm:
                    !preserveForm
            });
        }

        catch (error) {
            showToast(
                `ML Control: ${error.message}`
            );
        }
    }


    // ============================================================
    // SAVE ML CONFIGURATION
    // ============================================================

    async function saveMLConfig() {
        const body = {
            deviceId:
                state.selectedDeviceId,


            reservoir: {
                shape:
                    $("mlReservoirShape")
                        ?.value ||
                    "UNCONFIGURED",

                heightCm:
                    mlInputValue(
                        "mlReservoirHeight"
                    ),

                lengthCm:
                    mlInputValue(
                        "mlReservoirLength"
                    ),

                widthCm:
                    mlInputValue(
                        "mlReservoirWidth"
                    ),

                diameterCm:
                    mlInputValue(
                        "mlReservoirDiameter"
                    ),

                fullDistanceCm:
                    mlInputValue(
                        "mlFullDistance"
                    ),

                emptyDistanceCm:
                    mlInputValue(
                        "mlEmptyDistance"
                    )
            },


            baseline: {
                targetDurationHours:
                    mlInputValue(
                        "mlTargetHours"
                    ),

                minimumCoveragePct:
                    mlInputValue(
                        "mlMinCoverage"
                    ),

                maximumGapMultiplier:
                    mlInputValue(
                        "mlGapMultiplier"
                    ),

                robustZThreshold:
                    mlInputValue(
                        "mlRobustZ"
                    ),

                sustainedStrangeSamples:
                    mlInputValue(
                        "mlStrangeSamples"
                    ),

                missingWarningSeconds:
                    mlInputValue(
                        "mlMissingSeconds"
                    ),

                unchangedWarningSeconds:
                    mlInputValue(
                        "mlUnchangedSeconds"
                    ),

                minimumCalibrationSamplesPerClass:
                    mlInputValue(
                        "mlCalibrationSamples"
                    ),

                features:
                    mlSelectedFeatures()
            },


            isolationForest: {
                nEstimators:
                    mlInputValue(
                        "mlTrees"
                    ),

                trainFraction:
                    mlInputValue(
                        "mlTrainFraction"
                    ),

                randomState:
                    mlInputValue(
                        "mlRandomState"
                    )
            }
        };


        const result =
            await sendJson(
                "/api/v1/ml-control/config",

                "PATCH",

                body
            );


        showToast(
            result.message ||
            "ML configuration saved."
        );


        state.mlFormDeviceId =
            "";


        await loadMLControl();
    }


    // ============================================================
    // GENERIC ML ACTION
    // ============================================================

    async function mlAction(
        url,
        body,
        successMessage
    ) {
        const result =
            await sendJson(
                url,

                "POST",

                {
                    deviceId:
                        state.selectedDeviceId,

                    ...body
                }
            );


        showToast(
            result.message ||
            successMessage
        );


        await loadMLControl({
            preserveForm:
                true
        });
    }


    // ============================================================
    // CHART REDRAW
    // ============================================================

    const redrawAllCharts =
        () => {
            redrawRealtimeCharts();

            redrawHistoryCharts();
        };


    function debounce(
        callback,
        delay
    ) {
        let timer;


        return (
            ...args
        ) => {
            clearTimeout(
                timer
            );


            timer =
                setTimeout(
                    () =>
                        callback(
                            ...args
                        ),

                    delay
                );
        };
    }


    // ============================================================
    // UI EVENTS
    // ============================================================

    function bindEvents() {
        // --------------------------------------------------------
        // DEVICE SELECTOR
        // --------------------------------------------------------

        $("deviceSelect")
            ?.addEventListener(
                "change",

                (event) => {
                    state.selectedDeviceId =
                        event.target.value;


                    state.realtimeSeries =
                        [];


                    state.latestPayload =
                        null;


                    try {
                        if (
                            state.selectedDeviceId
                        ) {
                            localStorage.setItem(
                                "hydrosense:selectedDevice",

                                state.selectedDeviceId
                            );
                        }
                    } catch (_) {
                    }


                    renderSelectedDeviceRegistryState();


                    void loadCurrentLiveState();


                    if (
                        state.currentRoute ===
                        "history"
                    ) {
                        void loadHistory();
                    }


                    if (
                        state.currentRoute ===
                        "trust"
                    ) {
                        void loadTrustEvents();
                    }


                    if (
                        state.currentRoute ===
                        "ml"
                    ) {
                        state.mlControl =
                            null;


                        state.mlModels =
                            [];


                        state.mlLive =
                            null;


                        state.mlFormDeviceId =
                            "";


                        void loadMLControl();
                    }


                    redrawAllCharts();
                }
            );


        // --------------------------------------------------------
        // DEVICES
        // --------------------------------------------------------

        $("refreshDevicesButton")
            ?.addEventListener(
                "click",

                () =>
                    void loadDevices()
            );


        // --------------------------------------------------------
        // SENSOR TRUST EVENTS
        // --------------------------------------------------------

        $("refreshTrustEventsButton")
            ?.addEventListener(
                "click",

                () =>
                    void loadTrustEvents()
            );


        // --------------------------------------------------------
        // RESERVOIR GEOMETRY
        // --------------------------------------------------------

        $("mlReservoirShape")
            ?.addEventListener(
                "change",

                updateMLGeometryFields
            );


        // --------------------------------------------------------
        // REFRESH ML
        // --------------------------------------------------------

        $("mlRefreshButton")
            ?.addEventListener(
                "click",

                () =>
                    void loadMLControl({
                        preserveForm:
                            true
                    })
            );


        // --------------------------------------------------------
        // SAVE ML CONFIG
        // --------------------------------------------------------

        $("mlSaveConfigButton")
            ?.addEventListener(
                "click",

                async () => {
                    try {
                        await saveMLConfig();
                    }

                    catch (error) {
                        showToast(
                            error.message
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // START BASELINE
        // --------------------------------------------------------

        $("mlStartBaselineButton")
            ?.addEventListener(
                "click",

                async () => {
                    try {
                        await mlAction(
                            "/api/v1/ml-control/baseline/start",

                            {},

                            "Baseline started."
                        );
                    }

                    catch (error) {
                        showToast(
                            error.message
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // STOP BASELINE
        // --------------------------------------------------------

        $("mlStopBaselineButton")
            ?.addEventListener(
                "click",

                async () => {
                    try {
                        await mlAction(
                            "/api/v1/ml-control/baseline/stop",

                            {},

                            "Baseline stopped."
                        );
                    }

                    catch (error) {
                        showToast(
                            error.message
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // ABORT BASELINE
        // --------------------------------------------------------

        $("mlAbortBaselineButton")
            ?.addEventListener(
                "click",

                async () => {
                    try {
                        await mlAction(
                            "/api/v1/ml-control/baseline/abort",

                            {},

                            "Baseline aborted."
                        );
                    }

                    catch (error) {
                        showToast(
                            error.message
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // TRAIN ISOLATION FOREST
        // --------------------------------------------------------

        $("mlTrainButton")
            ?.addEventListener(
                "click",

                async () => {
                    try {
                        const result =
                            await sendJson(
                                "/api/v1/ml-control/train",

                                "POST",

                                {
                                    deviceId:
                                        state.selectedDeviceId
                                }
                            );


                        showToast(
                            result.message ||
                            "Training started."
                        );


                        await loadMLControl({
                            preserveForm:
                                true
                        });
                    }

                    catch (error) {
                        showToast(
                            error.message
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // START EXPERIMENT
        // --------------------------------------------------------

        $("mlStartExperimentButton")
            ?.addEventListener(
                "click",

                async () => {
                    try {
                        await mlAction(
                            "/api/v1/ml-control/experiments/start",

                            {
                                label:
                                    $("mlExperimentLabel")
                                        ?.value,

                                operatorNote:
                                    $("mlExperimentNote")
                                        ?.value ||
                                    ""
                            },

                            "Experiment started."
                        );
                    }

                    catch (error) {
                        showToast(
                            error.message
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // STOP EXPERIMENT
        // --------------------------------------------------------

        $("mlStopExperimentButton")
            ?.addEventListener(
                "click",

                async () => {
                    try {
                        await mlAction(
                            "/api/v1/ml-control/experiments/stop",

                            {},

                            "Experiment stopped."
                        );
                    }

                    catch (error) {
                        showToast(
                            error.message
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // CALIBRATE THRESHOLD
        // --------------------------------------------------------

        $("mlCalibrateButton")
            ?.addEventListener(
                "click",

                async () => {
                    try {
                        await mlAction(
                            "/api/v1/ml-control/threshold/calibrate",

                            {},

                            "Threshold calibrated."
                        );
                    }

                    catch (error) {
                        showToast(
                            error.message
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // COPY RAW PAYLOAD
        // --------------------------------------------------------

        $("copyPayloadButton")
            ?.addEventListener(
                "click",

                async () => {
                    if (
                        !state.latestPayload
                    ) {
                        return showToast(
                            "No realtime payload is available to copy."
                        );
                    }


                    try {
                        await navigator.clipboard
                            .writeText(
                                JSON.stringify(
                                    state.latestPayload,
                                    null,
                                    2
                                )
                            );


                        showToast(
                            "Latest realtime payload copied."
                        );
                    }

                    catch (error) {
                        showToast(
                            `Copy failed: ${error.message}`
                        );
                    }
                }
            );


        // --------------------------------------------------------
        // CHART RESIZE
        // --------------------------------------------------------

        window.addEventListener(
            "resize",

            debounce(
                redrawAllCharts,
                120
            )
        );
    }


    // ============================================================
    // CLOCK
    // ============================================================

    function updateClock() {
        setText(
            "clock",

            new Date()
                .toLocaleTimeString(
                    [],
                    {
                        hour:
                            "2-digit",

                        minute:
                            "2-digit",

                        second:
                            "2-digit"
                    }
                )
        );
    }


    // ============================================================
    // INITIALISE CONTROL ROOM
    // ============================================================

    async function initialise() {
        bindNavigation();

        bindHistoryRanges();

        bindEvents();


        const initialRoute =
            routeFromLocation();


        const canonical =
            routeUrl(
                initialRoute
            );


        if (
            location.pathname !==
            canonical
        ) {
            history.replaceState(
                {
                    route:
                        initialRoute
                },

                "",

                canonical
            );
        }


        renderRoute(
            initialRoute,

            {
                loadData:
                    false,

                scroll:
                    false
            }
        );


        updateClock();


        setInterval(
            updateClock,
            1000
        );


        setPill(
            "liveSocketStatus",
            "unknown",
            "CONNECTING"
        );


        setDot(
            "healthSocketDot",
            "unknown"
        );


        setText(
            "healthSocketText",
            "CONNECTING"
        );


        connectSocket();


        await loadBackendHealth();


        await loadDevices();


        if (
            initialRoute ===
            "history"
        ) {
            await loadHistory();
        }


        if (
            initialRoute ===
            "trust"
        ) {
            await loadTrustEvents();
        }


        if (
            initialRoute ===
            "ml"
        ) {
            await loadMLControl();
        }


        // --------------------------------------------------------
        // BACKEND HEALTH REFRESH
        // --------------------------------------------------------

        setInterval(
            loadBackendHealth,
            10000
        );


        // --------------------------------------------------------
        // DEVICE REGISTRY REFRESH
        // --------------------------------------------------------

        setInterval(
            () =>
                void loadDevices(),

            30000
        );


        // --------------------------------------------------------
        // FALLBACK LIVE POLLING WHEN SOCKET IS DOWN
        // --------------------------------------------------------

        setInterval(
            () => {
                if (
                    !state.socketConnected &&
                    state.selectedDeviceId
                ) {
                    void loadCurrentLiveState();
                }
            },

            5000
        );


        // --------------------------------------------------------
        // ML CONTROL REFRESH
        // --------------------------------------------------------

        setInterval(
            () => {
                if (
                    state.currentRoute ===
                        "ml" &&

                    state.selectedDeviceId
                ) {
                    void loadMLControl({
                        preserveForm:
                            true
                    });
                }
            },

            10000
        );


        setTimeout(
            redrawAllCharts,
            120
        );
    }


    // ============================================================
    // START AFTER DOM READY
    // ============================================================

    document.addEventListener(
        "DOMContentLoaded",
        initialise
    );
})();