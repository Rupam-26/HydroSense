#include <Arduino.h>
#include <WiFi.h>

#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_system.h>

#include <string>

#include <cstring>
#include <cmath>
#include <climits>

#include "models/S3SensorSnapshot.h"
#include "models/S3FeatureData.h"
#include "models/S3TDSResult.h"
#include "models/S3AnomalyResult.h"
#include "models/S3SensorTrustResult.h"
#include "models/S3CloudStatus.h"
#include "features/S3FeatureBuilder.h"
#include "tinyml/S3TinyMLTDS.h"
#include "anomaly/S3AnomalyEngine.h"
#include "trust/S3SensorTrustEngine.h"
#include "ui/S3LCDManager.h"
#include "cloud/S3CloudUploader.h"
#include "config/S3Secrets.h"

// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

// ============================================================
// HYDROSENSE ESP32-S3
//
// COMPLETE RUNTIME PIPELINE
//
// C3 sensors
//      ↓
// BLE packet
//      ↓
// packet validation
//      ↓
// S3SensorSnapshot
//      ↓
// S3FeatureBuilder
//      ↓
// S3FeatureData
//      ↓
// FP32 TinyML TDS soft sensor
//      ├── Serial
//      ├── LCD
//      ├── Real-time API -> Socket.IO -> Website
//      └── Historical API -> MongoDB Atlas
// ============================================================

// ============================================================
// WIFI
// ============================================================

static const char *WIFI_SSID =
    HydroSenseConfig::WIFI_SSID;

static const char *WIFI_PASSWORD =
    HydroSenseConfig::WIFI_PASSWORD;

static constexpr uint32_t SERIAL_BAUD =
    115200UL;

// Wi-Fi connection is intentionally non-blocking so local BLE/LCD
// operation never waits for the access point or backend.
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS =
    5000UL;

// ============================================================
// CLOUD BACKEND
// ============================================================

static const char *CLOUD_TELEMETRY_URL =
    HydroSenseConfig::CLOUD_TELEMETRY_URL;

static const char *CLOUD_REALTIME_URL =
    HydroSenseConfig::CLOUD_REALTIME_URL;

static const char *CLOUD_DEVICE_ID =
    HydroSenseConfig::CLOUD_DEVICE_ID;

static constexpr uint32_t CLOUD_UPLOAD_INTERVAL_MS =
    300000UL;

// Realtime keeps one latest pending frame and retries it while
// Wi-Fi/backend connectivity recovers.
static constexpr uint32_t CLOUD_REALTIME_RETRY_INTERVAL_MS =
    3000UL;

// A failed historical upload is retried only with a NEW eligible
// sensor frame after this delay. Old samples are never replayed.
static constexpr uint32_t CLOUD_HISTORICAL_FAILURE_RETRY_MS =
    30000UL;

// Allows the cloud task to re-evaluate Wi-Fi/retry state even when
// no new C3 frame enters the one-item queue.
static constexpr uint32_t CLOUD_TASK_POLL_MS =
    100UL;

// ============================================================
// BLE
// ============================================================

static const char *BLE_DEVICE_NAME =
    "HydroSense_S3";

static const char *SERVICE_UUID =
    "7b2c1000-6e9f-4b5a-9f8e-4a7c33b10001";

static const char *TELEMETRY_CHARACTERISTIC_UUID =
    "7b2c1001-6e9f-4b5a-9f8e-4a7c33b10001";

// BLE recovery is executed by a background FreeRTOS task.
static constexpr uint32_t BLE_RETRY_INTERVAL_MS =
    3000UL;

static constexpr uint32_t BLE_SCAN_TIME_SECONDS =
    2;

// C3 normally reports every ~3 s.  Older samples are retained
// internally for diagnostics but never presented as live.
static constexpr uint32_t C3_PACKET_STALE_MS =
    8000UL;

// If no genuine CRC-valid C3 packet arrives for this long,
// the BLE manager treats the link as wedged even if the BLE
// stack still reports CONNECTED.  C3 normally sends every ~3 s.
static constexpr uint32_t C3_LINK_RECOVERY_MS =
    12000UL;

// A newly-established link must produce its first genuine C3
// packet within this window or it is recycled and reconnected.
static constexpr uint32_t C3_FIRST_PACKET_TIMEOUT_MS =
    12000UL;

// ============================================================
// PROTOCOL
// ============================================================

static constexpr uint16_t HYDROSENSE_MAGIC =
    0x4853;

static constexpr uint8_t HYDROSENSE_PROTOCOL_VERSION =
    1;

// ============================================================
// VALIDITY MASK
// ============================================================

enum C3ValidityBits : uint8_t
{
    C3_VALID_DHT22 =
        1 << 0,

    C3_VALID_DS18B20 =
        1 << 1,

    C3_VALID_ULTRASONIC =
        1 << 2,

    C3_VALID_RESERVOIR =
        1 << 3,

    C3_VALID_CALIBRATION =
        1 << 4
};

// ============================================================
// EXACT 20 BYTE C3 PACKET
// ============================================================

#pragma pack(push, 1)

struct C3BlePacket
{
    uint16_t magic;

    uint8_t version;

    uint8_t sequence;

    int16_t airTemperature_cC;

    uint16_t humidity_cPct;

    int16_t waterTemperature_cC;

    uint16_t stableDistance_mm;

    uint16_t waterLevel_cPct;

    uint8_t qualityPct;

    uint16_t qualityFlags;

    uint8_t validMask;

    uint16_t crc16;
};

#pragma pack(pop)

static_assert(
    sizeof(C3BlePacket) == 20,
    "C3BlePacket must remain 20 bytes");

// ============================================================
// BLE RECEIVE QUEUE
// ============================================================

struct BleReceivedFrame
{
    // Preserve the original received length even when the frame is
    // malformed and larger than the 20-byte HydroSense payload.
    uint16_t length;

    // BLE connection epoch captured at ingress.  This prevents a
    // delayed frame from an older connection being accepted after a
    // fast reconnect.
    uint32_t connectionGeneration;

    uint8_t data[20];
};

static QueueHandle_t bleRxQueue =
    nullptr;

static constexpr uint8_t BLE_QUEUE_DEPTH =
    8;

// ============================================================
// BLE OBJECTS
// ============================================================

BLEClient *bleClient =
    nullptr;

BLERemoteCharacteristic *telemetryCharacteristic =
    nullptr;

BLEAddress *c3Address =
    nullptr;

volatile bool c3Connected =
    false;

volatile bool c3Found =
    false;

// Incremented on every physical BLE connection.  Frames carry this
// generation through the RX queue so an old queued frame can never
// overwrite data from a newer connection.
volatile uint32_t c3ConnectionGeneration =
    0;

volatile uint32_t c3ConnectedAtMs =
    0;

// ============================================================
// FEATURE PIPELINE
// ============================================================

S3SensorSnapshot latestSnapshot;

S3FeatureData latestFeatures;

S3FeatureBuilder featureBuilder;

S3TinyMLTDS tdsModel;

S3TDSResult latestTdsResult;

S3AnomalyEngine anomalyEngine;

S3AnomalyResult latestAnomalyResult;

S3SensorTrustEngine sensorTrustEngine;

S3SensorTrustResult latestSensorTrustResult;

S3LCDManager lcdManager;

S3CloudUploader cloudUploader;

S3CloudRuntimeStatus cloudRuntimeStatus;

static portMUX_TYPE cloudStatusMux =
    portMUX_INITIALIZER_UNLOCKED;

// ============================================================
// NON-BLOCKING RUNTIME TASKS
//
// UI/data path remains local and deterministic.
// HTTP and BLE scanning/reconnect are allowed to block only
// their own background tasks, never the LCD/button loop.
// ============================================================

struct CloudTxFrame
{
    S3SensorSnapshot snapshot;
    S3FeatureData features;
    S3TDSResult tdsResult;
    S3AnomalyResult anomalyResult;
    S3SensorTrustResult sensorTrustResult;

    uint32_t packetReceivedMs = 0;

    uint32_t crcFailures = 0;
    uint32_t wrongLengths = 0;
    uint32_t badHeaders = 0;
    uint32_t badVersions = 0;
    uint32_t queueDrops = 0;

    bool bleConnected = false;
    bool haveData = false;
};

static QueueHandle_t cloudTxQueue =
    nullptr;

static constexpr uint8_t CLOUD_TX_QUEUE_DEPTH =
    1;

static TaskHandle_t cloudTaskHandle =
    nullptr;

static TaskHandle_t bleManagerTaskHandle =
    nullptr;

static constexpr uint32_t CLOUD_TASK_STACK_SIZE =
    8192UL;

static constexpr uint32_t BLE_TASK_STACK_SIZE =
    8192UL;

static constexpr UBaseType_t BACKGROUND_TASK_PRIORITY =
    1;

// Set by the BLE callback, consumed by loop().
volatile bool c3DisconnectEventPending =
    false;

// ============================================================
// COUNTERS
// ============================================================

uint32_t totalNotifications =
    0;

uint32_t validSensorPackets =
    0;

uint32_t crcFailures =
    0;

uint32_t badHeaders =
    0;

uint32_t badVersions =
    0;

uint32_t wrongLengths =
    0;

uint32_t queueDrops =
    0;

uint32_t reconnectCount =
    0;

// CRC-valid packets that were intentionally not promoted into the
// measurement pipeline because they were not a new C3 measurement.
uint32_t duplicatePackets =
    0;

uint32_t outOfOrderPackets =
    0;

uint32_t obsoleteConnectionFrames =
    0;

// ============================================================
// ACCEPTED C3 SOURCE-SEQUENCE STATE
// ============================================================

bool haveAcceptedPacketSequence =
    false;

uint8_t lastAcceptedPacketSequence =
    0;

uint32_t acceptedSequenceConnectionGeneration =
    0;

// ============================================================
// STATE
// ============================================================

volatile bool haveSensorPacket =
    false;

volatile uint32_t latestPacketReceivedMs =
    0;

// ============================================================
// TIMERS
// ============================================================

uint32_t lastStatusMs =
    0;

uint32_t lastBleAttemptMs =
    0;

uint32_t lastWiFiAttemptMs =
    0;

static constexpr uint32_t STATUS_INTERVAL_MS =
    5000UL;

// ============================================================
// THREAD-SAFE CLOUD STATUS HANDOFF
// ============================================================

S3CloudRuntimeStatus readCloudRuntimeStatus()
{
    S3CloudRuntimeStatus copy;

    portENTER_CRITICAL(
        &cloudStatusMux);

    copy =
        cloudRuntimeStatus;

    portEXIT_CRITICAL(
        &cloudStatusMux);

    return copy;
}

void writeCloudRuntimeStatus(
    const S3CloudRuntimeStatus &status)
{
    portENTER_CRITICAL(
        &cloudStatusMux);

    cloudRuntimeStatus =
        status;

    portEXIT_CRITICAL(
        &cloudStatusMux);
}

const char *cloudChannelStateText(
    S3CloudChannelState state)
{
    switch (state)
    {
    case S3CloudChannelState::NotConfigured:
        return "NOT CONFIGURED";

    case S3CloudChannelState::WaitingForWiFi:
        return "WAITING FOR WIFI";

    case S3CloudChannelState::WaitingForData:
        return "WAITING FOR DATA";

    case S3CloudChannelState::Online:
        return "ONLINE";

    case S3CloudChannelState::RetryPending:
        return "RETRY PENDING";

    default:
        return "UNKNOWN";
    }
}

// ============================================================
// CRC16 CCITT
// ============================================================

uint16_t calculateCRC16(
    const uint8_t *data,
    size_t length)
{
    uint16_t crc =
        0xFFFF;

    for (
        size_t i = 0;
        i < length;
        ++i)
    {
        crc ^=
            static_cast<uint16_t>(
                data[i])
            << 8;

        for (
            uint8_t bit = 0;
            bit < 8;
            ++bit)
        {
            if (
                crc &
                0x8000)
            {
                crc =
                    static_cast<uint16_t>(
                        (crc << 1) ^
                        0x1021);
            }
            else
            {
                crc =
                    static_cast<uint16_t>(
                        crc << 1);
            }
        }
    }

    return crc;
}

// ============================================================
// DECODERS
// ============================================================

float decodeSignedCenti(
    int16_t value)
{
    if (
        value ==
        INT16_MIN)
    {
        return NAN;
    }

    return static_cast<float>(
               value) /
           100.0f;
}

float decodeUnsignedCenti(
    uint16_t value)
{
    if (
        value ==
        UINT16_MAX)
    {
        return NAN;
    }

    return static_cast<float>(
               value) /
           100.0f;
}

float decodeDistanceCm(
    uint16_t value)
{
    if (
        value ==
        UINT16_MAX)
    {
        return NAN;
    }

    return static_cast<float>(
               value) /
           10.0f;
}

// ============================================================
// C3 SOURCE-SEQUENCE ORDERING
//
// The C3 sequence identifies generated sensor snapshots.  BLE can
// legitimately deliver the same snapshot twice around reconnect
// because the S3 performs an immediate characteristic READ after
// subscribing and the C3 may also NOTIFY that same sequence.
//
// Only genuinely new source sequences are promoted into features,
// TinyML, anomaly history, Sensor Trust and cloud publication.
// ============================================================

enum class PacketSequenceDecision : uint8_t
{
    ACCEPT = 0,
    DUPLICATE,
    OUT_OF_ORDER
};

static PacketSequenceDecision classifyPacketSequence(
    uint8_t candidateSequence,
    uint32_t frameConnectionGeneration)
{
    if (
        !haveAcceptedPacketSequence)
    {
        return PacketSequenceDecision::ACCEPT;
    }

    const uint8_t sequenceDelta =
        static_cast<uint8_t>(
            candidateSequence -
            lastAcceptedPacketSequence);

    if (
        sequenceDelta == 0U)
    {
        return PacketSequenceDecision::DUPLICATE;
    }

    // The first UNIQUE packet of a newer BLE connection is accepted
    // regardless of modulo distance.  C3 continues generating packets
    // while S3 is disconnected, so a long outage can legitimately
    // advance the uint8 sequence by more than half its range.
    if (
        frameConnectionGeneration !=
        acceptedSequenceConnectionGeneration)
    {
        return PacketSequenceDecision::ACCEPT;
    }

    // Within one live connection BLE notifications are ordered.  The
    // uint8 half-range rule distinguishes a forward sequence from a
    // delayed older frame while still handling 255 -> 0 normally.
    if (
        sequenceDelta >= 128U)
    {
        return PacketSequenceDecision::OUT_OF_ORDER;
    }

    return PacketSequenceDecision::ACCEPT;
}

static void markPacketSequenceAccepted(
    uint8_t sequence,
    uint32_t frameConnectionGeneration)
{
    haveAcceptedPacketSequence =
        true;

    lastAcceptedPacketSequence =
        sequence;

    acceptedSequenceConnectionGeneration =
        frameConnectionGeneration;
}

// ============================================================
// BLE RECEIVE QUEUE HELPER
//
// Used both for notifications and for the one-time GATT read
// immediately after connection.  Only actual bytes supplied by
// the C3 characteristic are queued.
// ============================================================

static bool queueBlePayload(
    const uint8_t *data,
    size_t length)
{
    if (
        bleRxQueue == nullptr ||
        data == nullptr)
    {
        return false;
    }

    BleReceivedFrame frame{};

    frame.length =
        static_cast<uint16_t>(
            length > 0xFFFFU
                ? 0xFFFFU
                : length);

    frame.connectionGeneration =
        c3ConnectionGeneration;

    // For a malformed oversized notification we only need the real
    // length for diagnostics.  Never copy beyond the fixed packet
    // buffer.  The main-loop validator will count/reject the frame.
    if (
        length <=
        sizeof(frame.data))
    {
        memcpy(
            frame.data,
            data,
            length);
    }

    if (
        xQueueSend(
            bleRxQueue,
            &frame,
            0) !=
        pdTRUE)
    {
        queueDrops++;

        return false;
    }

    return true;
}

// ============================================================
// BLE NOTIFICATION CALLBACK
//
// No serial, feature processing, LCD work or network work here.
// ============================================================

static void notificationCallback(
    BLERemoteCharacteristic *characteristic,
    uint8_t *data,
    size_t length,
    bool isNotify)
{
    (void)characteristic;
    (void)isNotify;

    queueBlePayload(
        data,
        length);
}

// ============================================================
// BLE CLIENT CALLBACKS
// ============================================================

class HydroSenseClientCallbacks : public BLEClientCallbacks
{
public:
    void onConnect(
        BLEClient *client) override
    {
        (void)client;

        uint32_t nextGeneration =
            c3ConnectionGeneration +
            1U;

        // Keep zero reserved for the pre-connection state.
        if (
            nextGeneration == 0U)
        {
            nextGeneration =
                1U;
        }

        c3ConnectionGeneration =
            nextGeneration;

        // Do not publish c3Connected yet.  The application-level link
        // becomes ready only after the HydroSense service and telemetry
        // characteristic are successfully discovered below.
    }

    void onDisconnect(
        BLEClient *client) override
    {
        (void)client;

        c3Connected =
            false;

        c3ConnectedAtMs =
            0;

        telemetryCharacteristic =
            nullptr;

        c3DisconnectEventPending =
            true;
    }
};

// ============================================================
// BLE SCANNER
// ============================================================

class HydroSenseScanCallbacks : public BLEAdvertisedDeviceCallbacks
{
public:
    void onResult(
        BLEAdvertisedDevice advertisedDevice) override
    {
        if (
            !advertisedDevice.haveServiceUUID())
        {
            return;
        }

        if (
            !advertisedDevice.isAdvertisingService(
                BLEUUID(
                    SERVICE_UUID)))
        {
            return;
        }

        if (
            c3Address !=
            nullptr)
        {
            delete c3Address;

            c3Address =
                nullptr;
        }

        c3Address =
            new BLEAddress(
                advertisedDevice.getAddress());

        c3Found =
            true;

        BLEDevice ::getScan()
            ->stop();
    }
};

static HydroSenseScanCallbacks hydroSenseScanCallbacks;

// ============================================================
// WIFI
// ============================================================

void startWiFi()
{
    Serial.println();

    Serial.println(
        "[WIFI] Starting non-blocking connection...");

    WiFi.mode(
        WIFI_STA);

    WiFi.persistent(
        false);

    WiFi.setSleep(
        true);

    WiFi.setAutoReconnect(
        true);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD);

    lastWiFiAttemptMs =
        millis();
}

void maintainWiFi(
    uint32_t nowMs)
{
    static bool previousConnected =
        false;

    static bool stateInitialized =
        false;

    const bool connected =
        WiFi.status() ==
        WL_CONNECTED;

    if (
        !stateInitialized ||
        connected != previousConnected)
    {
        stateInitialized =
            true;

        previousConnected =
            connected;

        if (
            connected)
        {
            Serial.println(
                "[WIFI] ONLINE");

            Serial.print(
                "[WIFI] IP: ");

            Serial.println(
                WiFi.localIP());
        }
        else
        {
            Serial.println(
                "[WIFI] OFFLINE / CONNECTING");
        }
    }

    if (
        connected)
    {
        return;
    }

    if (
        nowMs -
            lastWiFiAttemptMs <
        WIFI_RETRY_INTERVAL_MS)
    {
        return;
    }

    lastWiFiAttemptMs =
        nowMs;

    Serial.println(
        "[WIFI] Background reconnect attempt...");

    if (
        !WiFi.reconnect())
    {
        WiFi.begin(
            WIFI_SSID,
            WIFI_PASSWORD);
    }
}

// ============================================================
// BLE SCAN
// ============================================================

bool scanForC3()
{
    c3Found =
        false;

    BLEScan *scan =
        BLEDevice::getScan();

    if (
        scan ==
        nullptr)
    {
        return false;
    }

    scan->setAdvertisedDeviceCallbacks(
        &hydroSenseScanCallbacks);

    scan->setActiveScan(
        true);

    scan->setInterval(
        160);

    scan->setWindow(
        80);

    Serial.println(
        "[BLE] Scanning for C3...");

    scan->start(
        BLE_SCAN_TIME_SECONDS,
        false);

    scan->clearResults();

    return c3Found;
}

// ============================================================
// BLE CONNECT
// ============================================================

bool connectToC3()
{
    if (
        c3Address ==
        nullptr)
    {
        return false;
    }

    if (
        bleClient ==
        nullptr)
    {
        bleClient =
            BLEDevice::createClient();

        if (
            bleClient ==
            nullptr)
        {
            return false;
        }

        bleClient->setClientCallbacks(
            new HydroSenseClientCallbacks());
    }

    Serial.println(
        "[BLE] Connecting...");

    if (
        !bleClient->connect(
            *c3Address))
    {
        Serial.println(
            "[BLE] Connection FAILED");

        return false;
    }

    BLERemoteService *service =
        bleClient->getService(
            BLEUUID(
                SERVICE_UUID));

    if (
        service ==
        nullptr)
    {
        bleClient->disconnect();

        return false;
    }

    telemetryCharacteristic =
        service->getCharacteristic(
            BLEUUID(
                TELEMETRY_CHARACTERISTIC_UUID));

    if (
        telemetryCharacteristic ==
        nullptr)
    {
        bleClient->disconnect();

        return false;
    }

    if (
        !telemetryCharacteristic->canNotify())
    {
        bleClient->disconnect();

        return false;
    }

    // The service/characteristic contract is now verified.  Mark the
    // application-level C3 link ready before notifications are enabled
    // so any immediately-arriving frame observes the new connection
    // state and reconnect counter.
    c3Connected =
        true;

    c3ConnectedAtMs =
        millis();

    c3DisconnectEventPending =
        false;

    reconnectCount++;

    telemetryCharacteristic
        ->registerForNotify(
            notificationCallback);

    Serial.println(
        "[BLE] C3 CONNECTED");

    // Read the C3 characteristic once immediately after
    // subscribing.  The C3 stores its latest real 20-byte sensor
    // packet in this READ+NOTIFY characteristic, so reconnecting
    // does not have to wait for the next scheduled notification.
    if (
        telemetryCharacteristic->canRead())
    {
        const std::string currentValue =
            telemetryCharacteristic->readValue();

        if (
            currentValue.size() ==
            sizeof(C3BlePacket))
        {
            queueBlePayload(
                reinterpret_cast<const uint8_t *>(
                    currentValue.data()),
                currentValue.size());

            Serial.println(
                "[BLE] Current real C3 packet queued");
        }
        else
        {
            Serial.println(
                "[BLE] Waiting for first real C3 packet");
        }
    }

    return true;
}

// ============================================================
// BLE MAINTENANCE
// ============================================================

void maintainBleConnection(
    uint32_t nowMs)
{
    if (
        c3Connected)
    {
        return;
    }

    if (
        nowMs -
            lastBleAttemptMs <
        BLE_RETRY_INTERVAL_MS)
    {
        return;
    }

    lastBleAttemptMs =
        nowMs;

    Serial.println(
        "[BLE] Recovering connection...");

    if (
        !scanForC3())
    {
        Serial.println(
            "[BLE] C3 not found");

        return;
    }

    if (
        !connectToC3())
    {
        Serial.println(
            "[BLE] C3 connection failed");
    }
}

// ============================================================
// BLE BACKGROUND MANAGER TASK
//
// scanForC3() can block during the scan window.  It is isolated
// here so the LCD/button/data loop remains responsive.
// ============================================================

void bleManagerTaskEntry(
    void *parameter)
{
    (void)parameter;

    for (;;)
    {
        const uint32_t nowMs =
            millis();

        bool forceRecovery =
            false;

        if (
            c3Connected)
        {
            if (
                haveSensorPacket)
            {
                const uint32_t packetAgeMs =
                    nowMs -
                    latestPacketReceivedMs;

                forceRecovery =
                    packetAgeMs >
                    C3_LINK_RECOVERY_MS;
            }
            else if (
                c3ConnectedAtMs !=
                0)
            {
                forceRecovery =
                    nowMs -
                        c3ConnectedAtMs >
                    C3_FIRST_PACKET_TIMEOUT_MS;
            }
        }

        if (
            forceRecovery)
        {
            Serial.println(
                "[BLE] Valid C3 telemetry timeout - forcing recovery");

            // Never fabricate or retain a stale sample as live.
            // Disconnecting triggers the normal reconnect path and
            // the next values shown must come from a new real C3 frame.
            if (
                bleClient !=
                    nullptr &&
                bleClient->isConnected())
            {
                bleClient->disconnect();
            }
            else
            {
                c3Connected =
                    false;

                c3ConnectedAtMs =
                    0;

                telemetryCharacteristic =
                    nullptr;

                c3DisconnectEventPending =
                    true;
            }

            vTaskDelay(
                pdMS_TO_TICKS(
                    100));
        }

        if (
            !c3Connected)
        {
            maintainBleConnection(
                millis());
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                50));
    }
}

// ============================================================
// CREATE SNAPSHOT FROM VALID PACKET
// ============================================================

void buildSnapshot(
    const C3BlePacket &packet,
    uint32_t nowMs)
{
    latestSnapshot.clear();

    latestSnapshot.sequence =
        packet.sequence;

    latestSnapshot.receivedAtMs =
        nowMs;

    latestSnapshot.airTemperatureC =
        decodeSignedCenti(
            packet.airTemperature_cC);

    latestSnapshot.humidityPct =
        decodeUnsignedCenti(
            packet.humidity_cPct);

    latestSnapshot.waterTemperatureC =
        decodeSignedCenti(
            packet.waterTemperature_cC);

    latestSnapshot.stableDistanceCm =
        decodeDistanceCm(
            packet.stableDistance_mm);

    latestSnapshot.waterLevelPct =
        decodeUnsignedCenti(
            packet.waterLevel_cPct);

    latestSnapshot.qualityScorePct =
        static_cast<float>(
            packet.qualityPct);

    latestSnapshot.qualityFlags =
        packet.qualityFlags;

    latestSnapshot.dht22Valid =
        (packet.validMask &
         C3_VALID_DHT22) != 0;

    latestSnapshot.ds18b20Valid =
        (packet.validMask &
         C3_VALID_DS18B20) != 0;

    latestSnapshot.ultrasonicValid =
        (packet.validMask &
         C3_VALID_ULTRASONIC) != 0;

    latestSnapshot.reservoirValid =
        (packet.validMask &
         C3_VALID_RESERVOIR) != 0;

    latestSnapshot.calibrationValid =
        (packet.validMask &
         C3_VALID_CALIBRATION) != 0;

    latestSnapshot.packetValid =
        true;

    // ========================================================
    // DERIVED FEATURE GENERATION
    // ========================================================

    latestFeatures =
        featureBuilder.build(
            latestSnapshot,
            nowMs);

    // ========================================================
    // FP32 TINYML TDS SOFT SENSOR
    // ========================================================

    latestTdsResult =
        tdsModel.predict(
            latestSnapshot);

    // ========================================================
    // REAL-TIME EDGE SENSOR ANOMALY / DEGRADATION ANALYSIS
    //
    // This runs locally on the ESP32-S3 for every genuine,
    // CRC-valid C3 packet. It does not depend on Wi-Fi/cloud.
    // ========================================================

    latestAnomalyResult =
        anomalyEngine.update(
            latestSnapshot,
            latestFeatures,
            latestTdsResult,
            nowMs);

    // ========================================================
    // CROSS-LAYER SENSOR TRUST
    //
    // This is the universal reliability layer. It combines the
    // current measurement validity, C3 data quality, temporal
    // behaviour, learned normal behaviour, and C3-to-S3 transport
    // integrity. The submerged-probe degradation module is applied
    // only as optional domain-specific evidence for water temperature.
    // ========================================================

    latestSensorTrustResult =
        sensorTrustEngine.update(
            latestSnapshot,
            latestAnomalyResult,
            c3Connected,
            validSensorPackets,
            crcFailures,
            wrongLengths,
            badHeaders,
            badVersions,
            queueDrops,
            reconnectCount,
            nowMs);
}

// ============================================================
// SERIAL FEATURE OUTPUT
// ============================================================

void printSnapshotAndFeatures()
{
    Serial.println();

    Serial.println(
        "========================================================");

    Serial.println(
        "          HYDROSENSE S3 FEATURE PIPELINE");

    Serial.println(
        "========================================================");

    Serial.print(
        "Sequence             : ");

    Serial.println(
        latestSnapshot.sequence);

    Serial.println();

    Serial.println(
        "[ DIRECT SENSOR DATA ]");

    Serial.print(
        "Air Temperature      : ");

    Serial.print(
        latestSnapshot.airTemperatureC,
        2);

    Serial.println(
        " C");

    Serial.print(
        "Humidity             : ");

    Serial.print(
        latestSnapshot.humidityPct,
        2);

    Serial.println(
        " %");

    Serial.print(
        "Water Temperature    : ");

    Serial.print(
        latestSnapshot.waterTemperatureC,
        2);

    Serial.println(
        " C");

    Serial.print(
        "Stable Distance      : ");

    if (
        latestSnapshot.distanceAvailable())
    {
        Serial.print(
            latestSnapshot.stableDistanceCm,
            2);

        Serial.println(
            " cm");
    }
    else
    {
        Serial.println(
            "UNAVAILABLE");
    }

    Serial.print(
        "Water Level          : ");

    if (
        latestSnapshot.waterLevelAvailable())
    {
        Serial.print(
            latestSnapshot.waterLevelPct,
            2);

        Serial.println(
            " %");
    }
    else
    {
        Serial.println(
            "UNAVAILABLE");
    }

    Serial.println();

    Serial.println(
        "[ DERIVED FEATURES ]");

    if (
        latestFeatures.environmentalFeaturesValid)
    {
        Serial.print(
            "Dew Point            : ");

        Serial.print(
            latestFeatures.dewPointC,
            2);

        Serial.println(
            " C");

        Serial.print(
            "Sat Vapor Pressure   : ");

        Serial.print(
            latestFeatures.saturationVaporPressureKPa,
            3);

        Serial.println(
            " kPa");

        Serial.print(
            "Actual Vapor Press.  : ");

        Serial.print(
            latestFeatures.actualVaporPressureKPa,
            3);

        Serial.println(
            " kPa");

        Serial.print(
            "VPD                  : ");

        Serial.print(
            latestFeatures.vpdKPa,
            3);

        Serial.println(
            " kPa");

        Serial.print(
            "Absolute Humidity    : ");

        Serial.print(
            latestFeatures.absoluteHumidityGm3,
            2);

        Serial.println(
            " g/m3");
    }
    else
    {
        Serial.println(
            "Environmental derived features unavailable");
    }

    if (
        latestFeatures.thermalFeaturesValid)
    {
        Serial.print(
            "Air-Water Delta      : ");

        Serial.print(
            latestFeatures.airWaterDeltaC,
            2);

        Serial.println(
            " C");

        Serial.print(
            "|Air-Water Delta|    : ");

        Serial.print(
            latestFeatures.absoluteAirWaterDeltaC,
            2);

        Serial.println(
            " C");
    }

    Serial.println();

    Serial.println(
        "[ EDGE AI - TDS SOFT SENSOR ]");

    Serial.println(
        "Model                : Tiny_32_16_8 FP32");

    Serial.print(
        "Estimated TDS        : ");

    if (
        latestTdsResult.valid())
    {
        Serial.print(
            latestTdsResult.estimatedTdsPpm,
            1);

        Serial.println(
            " ppm");
    }
    else
    {
        Serial.println(
            "UNAVAILABLE");
    }

    Serial.print(
        "AI Status            : ");

    Serial.println(
        S3TinyMLTDS::statusText(
            latestTdsResult.status));

    Serial.print(
        "Inference Time       : ");

    Serial.print(
        latestTdsResult.inferenceMicros);

    Serial.println(
        " us");

    Serial.println();

    Serial.println(
        "[ QUALITY ]");

    Serial.print(
        "Quality Score        : ");

    Serial.print(
        latestSnapshot.qualityScorePct,
        0);

    Serial.println(
        " %");

    Serial.print(
        "Quality Flags        : 0x");

    Serial.println(
        latestSnapshot.qualityFlags,
        HEX);

    Serial.println(
        "========================================================");
}

// ============================================================
// CLOUD STATE PUBLICATION
//
// The queue contains only ONE frame.  If the backend is slow,
// a newer real state overwrites an obsolete unsent state.
// ============================================================

void publishCloudState()
{
    if (
        cloudTxQueue ==
        nullptr)
    {
        return;
    }

    CloudTxFrame frame{};

    frame.snapshot =
        latestSnapshot;

    frame.features =
        latestFeatures;

    frame.tdsResult =
        latestTdsResult;

    frame.anomalyResult =
        latestAnomalyResult;

    frame.sensorTrustResult =
        latestSensorTrustResult;

    frame.packetReceivedMs =
        latestPacketReceivedMs;

    frame.crcFailures =
        crcFailures;

    frame.wrongLengths =
        wrongLengths;

    frame.badHeaders =
        badHeaders;

    frame.badVersions =
        badVersions;

    frame.queueDrops =
        queueDrops;

    frame.bleConnected =
        c3Connected;

    frame.haveData =
        true;

    xQueueOverwrite(
        cloudTxQueue,
        &frame);
}

void publishC3DisconnectedState()
{
    if (
        cloudTxQueue ==
        nullptr)
    {
        return;
    }

    CloudTxFrame frame{};

    frame.snapshot.clear();

    frame.features.clear();

    frame.tdsResult.clear();

    frame.anomalyResult.clear();

    frame.sensorTrustResult.clear();

    frame.packetReceivedMs =
        millis();

    frame.crcFailures =
        crcFailures;

    frame.wrongLengths =
        wrongLengths;

    frame.badHeaders =
        badHeaders;

    frame.badVersions =
        badVersions;

    frame.queueDrops =
        queueDrops;

    frame.bleConnected =
        false;

    frame.haveData =
        true;

    xQueueOverwrite(
        cloudTxQueue,
        &frame);
}

bool historicalSampleEligible(
    const CloudTxFrame &frame)
{
    if (
        !frame.haveData ||
        !frame.snapshot.packetValid)
    {
        return false;
    }

    // Historical storage requires semantically valid physical
    // values, not merely true flags from the incoming packet.
    if (
        !frame.snapshot.airEnvironmentAvailable() ||
        !frame.snapshot.waterTemperatureAvailable() ||
        !frame.snapshot.distanceAvailable() ||
        !frame.snapshot.waterLevelAvailable())
    {
        return false;
    }

    if (
        !frame.features.valid ||
        !frame.features.environmentalFeaturesValid ||
        !frame.features.thermalFeaturesValid)
    {
        return false;
    }

    return frame.tdsResult.valid();
}

// ============================================================
// CLOUD BACKGROUND TASK
//
// ALL HTTP occurs here.  HTTP latency or backend failure cannot
// block the LCD, BOOT button, BLE packet validation or TinyML.
// ============================================================

void cloudTaskEntry(
    void *parameter)
{
    (void)parameter;

    CloudTxFrame pendingRealtimeFrame{};

    bool havePendingRealtimeFrame =
        false;

    bool historicalAttempted =
        false;

    bool historicalFailurePending =
        false;

    uint32_t lastRealtimeAttemptMs =
        0;

    uint32_t lastHistoricalSuccessMs =
        0;

    uint32_t lastHistoricalFailureMs =
        0;

    uint32_t lastHistoricalAttemptFrameMs =
        0;

    S3CloudRuntimeStatus status =
        readCloudRuntimeStatus();

    for (;;)
    {
        if (
            cloudTxQueue ==
            nullptr)
        {
            vTaskDelay(
                pdMS_TO_TICKS(
                    CLOUD_TASK_POLL_MS));

            continue;
        }

        CloudTxFrame receivedFrame{};

        const bool receivedNewFrame =
            xQueueReceive(
                cloudTxQueue,
                &receivedFrame,
                pdMS_TO_TICKS(
                    CLOUD_TASK_POLL_MS)) ==
            pdTRUE;

        // Queue length is one. A newer frame always replaces the
        // older pending realtime frame: latest state wins.
        if (
            receivedNewFrame)
        {
            pendingRealtimeFrame =
                receivedFrame;

            havePendingRealtimeFrame =
                true;

            status.realtimeFramePending =
                true;
        }

        const uint32_t nowMs =
            millis();

        const bool wifiConnected =
            WiFi.status() ==
            WL_CONNECTED;

        const bool realtimeConfigured =
            status.realtime !=
            S3CloudChannelState::NotConfigured;

        const bool historicalConfigured =
            status.historical !=
            S3CloudChannelState::NotConfigured;

        if (
            !realtimeConfigured)
        {
            havePendingRealtimeFrame =
                false;

            status.realtimeFramePending =
                false;
        }

        // ----------------------------------------------------
        // WIFI-OFFLINE STATE
        // ----------------------------------------------------

        if (
            !wifiConnected)
        {
            if (
                realtimeConfigured)
            {
                status.realtime =
                    S3CloudChannelState::WaitingForWiFi;
            }

            if (
                historicalConfigured)
            {
                status.historical =
                    S3CloudChannelState::WaitingForWiFi;
            }

            status.realtimeFramePending =
                havePendingRealtimeFrame;

            writeCloudRuntimeStatus(
                status);

            continue;
        }

        // ----------------------------------------------------
        // REALTIME PATH
        //
        // One latest frame remains pending across Wi-Fi/backend
        // failure. It retries every 3 seconds until successful or
        // until a newer frame replaces it.
        // ----------------------------------------------------

        if (
            realtimeConfigured &&
            havePendingRealtimeFrame)
        {
            const bool realtimeAttemptDue =
                status.realtimeAttempts ==
                    0 ||
                nowMs -
                        lastRealtimeAttemptMs >=
                    CLOUD_REALTIME_RETRY_INTERVAL_MS;

            if (
                realtimeAttemptDue)
            {
                lastRealtimeAttemptMs =
                    nowMs;

                status.realtimeAttempts++;

                status.lastRealtimeAttemptMs =
                    nowMs;

                const uint32_t packetAgeMs =
                    nowMs -
                    pendingRealtimeFrame.packetReceivedMs;

                const bool uploaded =
                    cloudUploader.uploadRealtime(
                        pendingRealtimeFrame.snapshot,
                        pendingRealtimeFrame.features,
                        pendingRealtimeFrame.tdsResult,
                        pendingRealtimeFrame.anomalyResult,
                        pendingRealtimeFrame.sensorTrustResult,
                        packetAgeMs,
                        pendingRealtimeFrame.bleConnected,
                        true,
                        pendingRealtimeFrame.crcFailures,
                        pendingRealtimeFrame.wrongLengths,
                        pendingRealtimeFrame.badHeaders,
                        pendingRealtimeFrame.badVersions,
                        pendingRealtimeFrame.queueDrops);

                if (
                    uploaded)
                {
                    status.realtimeSuccesses++;

                    status.lastRealtimeSuccessMs =
                        nowMs;

                    status.realtime =
                        S3CloudChannelState::Online;

                    havePendingRealtimeFrame =
                        false;
                }
                else
                {
                    status.realtimeFailures++;

                    status.realtime =
                        S3CloudChannelState::RetryPending;
                }
            }
        }
        else if (
            realtimeConfigured)
        {
            status.realtime =
                status.realtimeSuccesses > 0
                    ? S3CloudChannelState::Online
                    : S3CloudChannelState::WaitingForData;
        }

        status.realtimeFramePending =
            havePendingRealtimeFrame;

        // ----------------------------------------------------
        // HISTORICAL / MONGODB PATH
        //
        // Normal interval: 5 minutes.
        // Failure retry: after 30 seconds, only on a NEW eligible
        // frame. An old sample is never replayed into MongoDB.
        // ----------------------------------------------------

        const uint32_t historicalNowMs =
            millis();

        if (
            historicalConfigured &&
            cloudUploader.ready() &&
            receivedNewFrame &&
            historicalSampleEligible(
                receivedFrame))
        {
            const bool normalUploadDue =
                !historicalFailurePending &&
                (!historicalAttempted ||
                 historicalNowMs -
                         lastHistoricalSuccessMs >=
                     CLOUD_UPLOAD_INTERVAL_MS);

            const bool failureRetryDue =
                historicalFailurePending &&
                historicalNowMs -
                        lastHistoricalFailureMs >=
                    CLOUD_HISTORICAL_FAILURE_RETRY_MS &&
                receivedFrame.packetReceivedMs !=
                    lastHistoricalAttemptFrameMs;

            if (
                normalUploadDue ||
                failureRetryDue)
            {
                historicalAttempted =
                    true;

                lastHistoricalAttemptFrameMs =
                    receivedFrame.packetReceivedMs;

                status.historicalAttempts++;

                status.lastHistoricalAttemptMs =
                    historicalNowMs;

                const uint32_t packetAgeMs =
                    historicalNowMs -
                    receivedFrame.packetReceivedMs;

                const bool uploaded =
                    cloudUploader.upload(
                        receivedFrame.snapshot,
                        receivedFrame.features,
                        receivedFrame.tdsResult,
                        packetAgeMs,
                        receivedFrame.bleConnected,
                        true,
                        receivedFrame.crcFailures,
                        receivedFrame.wrongLengths,
                        receivedFrame.badHeaders,
                        receivedFrame.badVersions,
                        receivedFrame.queueDrops);

                if (
                    uploaded)
                {
                    lastHistoricalSuccessMs =
                        historicalNowMs;

                    historicalFailurePending =
                        false;

                    status.historicalSuccesses++;

                    status.lastHistoricalSuccessMs =
                        historicalNowMs;

                    status.historical =
                        S3CloudChannelState::Online;

                    Serial.println(
                        "[CLOUD] Historical telemetry SUCCESS");
                }
                else
                {
                    lastHistoricalFailureMs =
                        historicalNowMs;

                    historicalFailurePending =
                        true;

                    status.historicalFailures++;

                    status.historical =
                        S3CloudChannelState::RetryPending;

                    Serial.print(
                        "[CLOUD] Historical telemetry FAILED | HTTP ");

                    Serial.println(
                        cloudUploader.lastHttpCode());
                }
            }
        }

        if (
            historicalConfigured)
        {
            if (
                historicalFailurePending)
            {
                status.historical =
                    S3CloudChannelState::RetryPending;
            }
            else if (
                status.historicalSuccesses >
                0)
            {
                status.historical =
                    S3CloudChannelState::Online;
            }
            else if (
                !historicalAttempted)
            {
                status.historical =
                    S3CloudChannelState::WaitingForData;
            }
        }

        writeCloudRuntimeStatus(
            status);
    }
}

// ============================================================
// PROCESS BLE FRAME
// ============================================================

void processBleFrame(
    const BleReceivedFrame &frame)
{
    totalNotifications++;

    if (
        frame.length !=
        sizeof(C3BlePacket))
    {
        wrongLengths++;
        return;
    }

    C3BlePacket packet{};

    memcpy(
        &packet,
        frame.data,
        sizeof(packet));

    if (
        packet.magic !=
        HYDROSENSE_MAGIC)
    {
        badHeaders++;
        return;
    }

    if (
        packet.version !=
        HYDROSENSE_PROTOCOL_VERSION)
    {
        badVersions++;
        return;
    }

    const uint16_t calculatedCRC =
        calculateCRC16(
            reinterpret_cast<const uint8_t *>(
                &packet),
            sizeof(C3BlePacket) -
                sizeof(packet.crc16));

    if (
        calculatedCRC !=
        packet.crc16)
    {
        crcFailures++;

        Serial.println(
            "[BLE] CRC FAILURE");

        return;
    }

    // A frame queued by an older BLE connection must never replace
    // state from the newest connection, even if its bytes and CRC are
    // perfectly valid.
    if (
        frame.connectionGeneration !=
        c3ConnectionGeneration)
    {
        obsoleteConnectionFrames++;
        return;
    }

    const PacketSequenceDecision sequenceDecision =
        classifyPacketSequence(
            packet.sequence,
            frame.connectionGeneration);

    if (
        sequenceDecision ==
        PacketSequenceDecision::DUPLICATE)
    {
        duplicatePackets++;
        return;
    }

    if (
        sequenceDecision ==
        PacketSequenceDecision::OUT_OF_ORDER)
    {
        outOfOrderPackets++;
        return;
    }

    // ========================================================
    // UNIQUE VALID PACKET
    // ========================================================

    markPacketSequenceAccepted(
        packet.sequence,
        frame.connectionGeneration);

    const uint32_t nowMs =
        millis();

    latestPacketReceivedMs =
        nowMs;

    haveSensorPacket =
        true;

    validSensorPackets++;

    buildSnapshot(
        packet,
        nowMs);

    // Publish a coherent copy to the background network task.
    // The local packet/LCD path never waits for HTTP.
    publishCloudState();

    // Keep the hot receive path short.  The previous full
    // multi-line dump was several kilobytes and could delay the
    // first LCD refresh at 115200 baud.
    Serial.print(
        "[BLE RX] seq=");

    Serial.print(
        packet.sequence);

    Serial.print(
        " | quality=");

    Serial.print(
        packet.qualityPct);

    Serial.print(
        "% | validMask=0x");

    Serial.println(
        packet.validMask,
        HEX);
}

// ============================================================
// PROCESS QUEUE
// ============================================================

void processBleQueue()
{
    if (
        bleRxQueue ==
        nullptr)
    {
        return;
    }

    BleReceivedFrame frame{};

    while (
        xQueueReceive(
            bleRxQueue,
            &frame,
            0) ==
        pdTRUE)
    {
        processBleFrame(
            frame);
    }
}

// ============================================================
// LCD UPDATE
// ============================================================

void updateLCD(
    uint32_t nowMs)
{
    if (
        !lcdManager.ready())
    {
        return;
    }

    uint32_t packetAgeMs =
        0;

    if (
        haveSensorPacket)
    {
        packetAgeMs =
            nowMs -
            latestPacketReceivedMs;
    }

    const bool haveFreshSensorData =
        haveSensorPacket &&
        c3Connected &&
        packetAgeMs <=
            C3_PACKET_STALE_MS;

    S3SensorSnapshot displaySnapshot =
        latestSnapshot;

    S3FeatureData displayFeatures =
        latestFeatures;

    S3TDSResult displayTdsResult =
        latestTdsResult;

    // Never leave an old value on-screen and call it live.
    if (
        !haveFreshSensorData)
    {
        displaySnapshot.clear();

        displayFeatures.clear();

        displayTdsResult.clear();
    }

    const S3CloudRuntimeStatus cloudStatus =
        readCloudRuntimeStatus();

    lcdManager.render(
        displaySnapshot,
        displayFeatures,
        displayTdsResult,
        haveFreshSensorData,
        c3Connected,
        WiFi.status() == WL_CONNECTED,
        cloudStatus,
        tdsModel.ready(),
        validSensorPackets,
        crcFailures,
        packetAgeMs);
}

// ============================================================
// HUMAN-READABLE SENSOR RELIABILITY DETAILS
// ============================================================

void printSensorReliabilityDetails(
    const char *label,
    const S3SensorAnomalyAnalysis &analysis)
{
    Serial.print(
        label);

    Serial.print(
        " : ");

    Serial.println(
        S3AnomalyEngine::sensorConditionText(
            analysis.condition));

    if (
        !analysis.available ||
        !analysis.baselineReady)
    {
        return;
    }

    Serial.print(
        "  Evidence Index      : ");

    Serial.print(
        analysis.anomalyScorePct,
        1);

    Serial.println(
        " / 100 (engineering evidence, not failure probability)");

    if (
        analysis.stuckOutputEvidence)
    {
        Serial.println(
            "  Observation         : Output stayed nearly unchanged even while a meaningful external change was present.");

        Serial.println(
            "  Interpretation      : Possible stuck output or unusually weak sensor response.");
    }

    if (
        analysis.excessiveNoiseEvidence)
    {
        Serial.println(
            "  Observation         : Short-term measurement variability is persistently much higher than the learned normal behaviour.");

        Serial.println(
            "  Interpretation      : Possible measurement instability, interference, poor sensing conditions, or sensor degradation.");
    }

    if (
        analysis.rapidChangeEvidence)
    {
        Serial.println(
            "  Observation         : A sudden measurement change was detected.");

        Serial.println(
            "  Interpretation      : This is logged as unusual, but one sudden change alone is not called a sensor failure.");
    }

    if (
        analysis.suppressedDynamicsEvidence)
    {
        Serial.println(
            "  Observation         : The sensor is responding less dynamically than its learned normal behaviour while a response would be expected.");
    }

    if (
        analysis.amplifiedDynamicsEvidence)
    {
        Serial.println(
            "  Observation         : The sensor is varying much more strongly than its learned normal behaviour.");
    }
}

// ============================================================
// HUMAN-READABLE CROSS-LAYER SENSOR TRUST DETAILS
// ============================================================

void printTrustComponent(
    const char *label,
    const S3TrustComponent &component)
{
    Serial.print(
        "    ");
    Serial.print(
        label);
    Serial.print(
        " : ");

    if (
        component.available &&
        std::isfinite(
            component.scorePct))
    {
        Serial.print(
            component.scorePct,
            1);
        Serial.println(
            " / 100");
    }
    else
    {
        Serial.println(
            "NOT AVAILABLE FOR THIS ASSESSMENT");
    }
}

void printSensorTrustDetails(
    const char *label,
    const S3SensorTrustAnalysis &trust)
{
    Serial.println();
    Serial.print(
        label);
    Serial.print(
        " : ");
    Serial.println(
        S3SensorTrustEngine::trustStateText(
            trust.state));

    if (!trust.measurementAvailable)
    {
        Serial.print(
            "  Explanation          : ");
        Serial.println(
            S3SensorTrustEngine::trustReasonText(
                trust.primaryReason));
        return;
    }

    Serial.print(
        trust.baselineReady
            ? "  Sensor Trust         : "
            : "  Provisional Trust     : ");
    Serial.print(
        trust.trustScorePct,
        1);
    Serial.println(
        " / 100 (engineering trust index, not probability)");

    Serial.print(
        "  Evidence Coverage     : ");
    Serial.print(
        trust.evidenceCoveragePct,
        1);
    Serial.println(
        " / 100");

    Serial.print(
        "  Main Interpretation   : ");
    Serial.println(
        S3SensorTrustEngine::trustReasonText(
            trust.primaryReason));

    Serial.println(
        "  Evidence Layers:");

    printTrustComponent(
        "Measurement Integrity       ",
        trust.measurementIntegrity);
    printTrustComponent(
        "Upstream Data Quality       ",
        trust.dataQuality);
    printTrustComponent(
        "Temporal Reliability        ",
        trust.temporalReliability);
    printTrustComponent(
        "Learned Behaviour Consistency",
        trust.learnedBehaviourConsistency);
    printTrustComponent(
        "C3-to-S3 Communication      ",
        trust.communicationIntegrity);

    if (trust.domainSpecificEvidenceApplied)
    {
        printTrustComponent(
            "Domain Degradation Integrity ",
            trust.domainSpecificDegradation);
    }
}

// ============================================================
// STATUS
// ============================================================

void printGatewayStatus(
    uint32_t nowMs)
{
    const S3CloudRuntimeStatus cloudStatus =
        readCloudRuntimeStatus();

    Serial.println();

    Serial.println(
        "================ S3 GATEWAY STATUS ================");

    Serial.print(
        "WiFi                 : ");

    Serial.println(
        WiFi.status() ==
                WL_CONNECTED
            ? "ONLINE"
            : "OFFLINE");

    Serial.print(
        "C3 BLE               : ");

    Serial.println(
        c3Connected
            ? "CONNECTED"
            : "DISCONNECTED");

    Serial.print(
        "Realtime Cloud       : ");

    Serial.println(
        cloudChannelStateText(
            cloudStatus.realtime));

    Serial.print(
        "Realtime Pending     : ");

    Serial.println(
        cloudStatus.realtimeFramePending
            ? "YES"
            : "NO");

    Serial.print(
        "Realtime OK/Fail     : ");

    Serial.print(
        cloudStatus.realtimeSuccesses);

    Serial.print(
        "/");

    Serial.println(
        cloudStatus.realtimeFailures);

    Serial.print(
        "MongoDB Historical   : ");

    Serial.println(
        cloudChannelStateText(
            cloudStatus.historical));

    Serial.print(
        "MongoDB OK/Fail      : ");

    Serial.print(
        cloudStatus.historicalSuccesses);

    Serial.print(
        "/");

    Serial.println(
        cloudStatus.historicalFailures);

    Serial.print(
        "C3 Live Data         : ");

    const bool liveData =
        haveSensorPacket &&
        c3Connected &&
        (nowMs - latestPacketReceivedMs) <=
            C3_PACKET_STALE_MS;

    Serial.println(
        liveData
            ? "FRESH"
            : "WAITING/STALE");

    Serial.print(
        "Sensor Packets       : ");

    Serial.println(
        validSensorPackets);

    Serial.print(
        "CRC Failures         : ");

    Serial.println(
        crcFailures);

    Serial.print(
        "Wrong Lengths        : ");

    Serial.println(
        wrongLengths);

    Serial.print(
        "Bad Headers          : ");

    Serial.println(
        badHeaders);

    Serial.print(
        "Bad Versions         : ");

    Serial.println(
        badVersions);

    Serial.print(
        "Queue Drops          : ");

    Serial.println(
        queueDrops);

    Serial.print(
        "Duplicate Packets    : ");

    Serial.println(
        duplicatePackets);

    Serial.print(
        "Out-of-Order Packets : ");

    Serial.println(
        outOfOrderPackets);

    Serial.print(
        "Obsolete BLE Frames  : ");

    Serial.println(
        obsoleteConnectionFrames);

    Serial.print(
        "Feature Data         : ");

    Serial.println(
        latestFeatures.valid
            ? "AVAILABLE"
            : "WAITING");

    Serial.print(
        "TinyML TDS           : ");

    Serial.println(
        tdsModel.ready()
            ? "READY"
            : "FAILED");

    Serial.print(
        "TDS Estimate         : ");

    if (
        latestTdsResult.valid())
    {
        Serial.print(
            latestTdsResult.estimatedTdsPpm,
            1);

        Serial.println(
            " ppm");
    }
    else
    {
        Serial.println(
            S3TinyMLTDS::statusText(
                latestTdsResult.status));
    }

    Serial.println();

    Serial.println(
        "[ CROSS-LAYER SENSOR TRUST ]");

    if (!liveData)
    {
        Serial.println(
            "Current Assessment   : UNAVAILABLE - C3 DATA IS NOT FRESH");
        Serial.println(
            "Explanation          : HydroSense will not present an old trust score as a current live assessment.");
    }
    else
    {
        Serial.print(
            "Overall Trust State  : ");
        Serial.println(
            S3SensorTrustEngine::trustStateText(
                latestSensorTrustResult.overallState));

        Serial.print(
            latestSensorTrustResult.baselineReady
                ? "Overall Sensor Trust: "
                : "Provisional Trust    : ");
        Serial.print(
            latestSensorTrustResult.overallTrustScorePct,
            1);
        Serial.println(
            " / 100 (engineering trust index, not probability)");

        Serial.print(
            "Evidence Coverage    : ");
        Serial.print(
            latestSensorTrustResult.overallEvidenceCoveragePct,
            1);
        Serial.println(
            " / 100");

        Serial.print(
            "Communication Trust  : ");
        Serial.print(
            latestSensorTrustResult.communicationIntegrityPct,
            1);
        Serial.println(
            " / 100");

        Serial.print(
            "Transport Fault Events: ");
        Serial.println(
            latestSensorTrustResult.transportFaultEventsObserved);

        Serial.print(
            "Sequence-Gap Events  : ");
        Serial.println(
            latestSensorTrustResult.sequenceGapEventsObserved);

        Serial.print(
            "Reconnect Events     : ");
        Serial.println(
            latestSensorTrustResult.reconnectEventsObserved);

        printSensorTrustDetails(
            "Air Temperature Sensor",
            latestSensorTrustResult.airTemperature);

        printSensorTrustDetails(
            "Humidity Sensor",
            latestSensorTrustResult.humidity);

        printSensorTrustDetails(
            "Water Temperature Sensor",
            latestSensorTrustResult.waterTemperature);

        printSensorTrustDetails(
            "Reservoir Distance Sensor",
            latestSensorTrustResult.reservoirDistance);
    }

    Serial.println();

    Serial.println(
        "[ SUPPORTING SENSOR ANOMALY EVIDENCE ]");

    Serial.print(
        "Overall Condition    : ");

    Serial.println(
        S3AnomalyEngine::sensorConditionText(
            latestAnomalyResult.overallCondition));

    Serial.print(
        "Baseline Progress    : ");

    Serial.print(
        latestAnomalyResult.generalBaselineSamples);

    Serial.print(
        " / ");

    Serial.println(
        latestAnomalyResult.requiredGeneralBaselineSamples);

    Serial.println();

    printSensorReliabilityDetails(
        "Air Temperature Sensor",
        latestAnomalyResult.airTemperature);

    printSensorReliabilityDetails(
        "Humidity Sensor",
        latestAnomalyResult.humidity);

    printSensorReliabilityDetails(
        "Water Temperature Sensor",
        latestAnomalyResult.waterTemperature);

    printSensorReliabilityDetails(
        "Reservoir Distance Sensor",
        latestAnomalyResult.reservoirDistance);

    Serial.println();

    Serial.println(
        "[ SUBMERGED WATER-PROBE SURFACE-FOULING ASSESSMENT ]");

    Serial.print(
        "Assessment State     : ");

    Serial.println(
        S3AnomalyEngine::surfaceFoulingStateText(
            latestAnomalyResult.surfaceFoulingState));

    Serial.print(
        "Clean Response Events: ");

    Serial.print(
        latestAnomalyResult.thermalResponseBaselineEvents);

    Serial.print(
        " / ");

    Serial.println(
        latestAnomalyResult.requiredThermalResponseBaselineEvents);

    Serial.print(
        "Comparable Test Events: ");

    Serial.println(
        latestAnomalyResult.thermalResponseAssessmentEvents);

    Serial.print(
        "Rejected Test Events : ");

    Serial.println(
        latestAnomalyResult.rejectedThermalEvents);

    if (
        latestAnomalyResult.surfaceFoulingAssessmentAvailable)
    {
        Serial.print(
            "Fouling Evidence Index: ");

        Serial.print(
            latestAnomalyResult.surfaceFoulingRiskIndexPct,
            1);

        Serial.println(
            " / 100 (NOT a probability of algae)");

        Serial.print(
            "Assessment Confidence : ");

        Serial.print(
            latestAnomalyResult.surfaceFoulingAssessmentConfidencePct,
            1);

        Serial.println(
            " / 100");

        if (
            std::isfinite(
                latestAnomalyResult.cleanThermalResponseRatio))
        {
            Serial.print(
                "Clean Response Ratio  : ");

            Serial.println(
                latestAnomalyResult.cleanThermalResponseRatio,
                3);
        }

        if (
            std::isfinite(
                latestAnomalyResult.currentThermalResponseRatio))
        {
            Serial.print(
                "Current Response Ratio: ");

            Serial.println(
                latestAnomalyResult.currentThermalResponseRatio,
                3);
        }

        if (
            std::isfinite(
                latestAnomalyResult.responseRatioRelativeToCleanPct))
        {
            Serial.print(
                "Response vs Clean     : ");

            Serial.print(
                latestAnomalyResult.responseRatioRelativeToCleanPct,
                1);

            Serial.println(
                " %");
        }

        Serial.print(
            "Repeated Attenuation  : ");

        Serial.println(
            latestAnomalyResult.responseAttenuationEvidence
                ? "YES - repeated comparable events show reduced response"
                : "NO");

        Serial.print(
            "Response Suppression  : ");

        Serial.println(
            latestAnomalyResult.waterResponseSuppressionEvidence
                ? "YES - water-probe dynamics are lower than the clean baseline"
                : "NO");

        Serial.print(
            "Thermal Relation Shift: ");

        Serial.println(
            latestAnomalyResult.thermalRelationshipShiftEvidence
                ? "YES - supporting evidence only"
                : "NO");
    }
    else
    {
        if (
            latestAnomalyResult.competingWaterSensorFaultEvidence)
        {
            Serial.println(
                "Reason               : Fouling inference is paused because the water-temperature signal itself is currently unreliable.");
        }
        else if (
            !latestAnomalyResult.thermalExcitationStrongEnough)
        {
            Serial.println(
                "Reason               : Waiting for a meaningful natural temperature-change event before judging probe response.");
        }
        else if (
            !latestAnomalyResult.reservoirThermallyStable)
        {
            Serial.println(
                "Reason               : Reservoir level changed during the thermal event, so the event is rejected to avoid a false fouling conclusion.");
        }
        else if (
            !latestAnomalyResult.thermalResponseDirectionCoherent)
        {
            Serial.println(
                "Reason               : Air and water temperature did not show a comparable same-direction thermal response, so the event is not used.");
        }
        else if (
            !latestAnomalyResult.thermalEventComparableToBaseline &&
            latestAnomalyResult.thermalResponseBaselineEvents >=
                latestAnomalyResult.requiredThermalResponseBaselineEvents)
        {
            Serial.println(
                "Reason               : Thermal event differs too much from the learned clean conditions to make a fair comparison.");
        }
    }

    Serial.println(
        "Interpretation       : HydroSense cannot directly see algae. It detects response degradation compatible with surface coating, biological fouling, contamination, or probe ageing.");
    Serial.println();

    Serial.print(
        "LCD                  : ");

    Serial.println(
        lcdManager.ready()
            ? "READY"
            : "FAILED");

    if (
        haveSensorPacket)
    {
        Serial.print(
            "Packet Age           : ");

        Serial.print(
            nowMs -
            latestPacketReceivedMs);

        Serial.println(
            " ms");
    }

    Serial.println(
        "===================================================");
}

// ============================================================
// RESET DIAGNOSTICS
//
// This never changes sensor values or runtime decisions. It only
// reports why the ESP32-S3 booted, which makes a true power/reset
// cycle distinguishable from an LCD redraw.
// ============================================================

const char *resetReasonText(
    esp_reset_reason_t reason)
{
    switch (
        reason)
    {
    case ESP_RST_POWERON:
        return "POWER_ON";

    case ESP_RST_EXT:
        return "EXTERNAL_RESET";

    case ESP_RST_SW:
        return "SOFTWARE_RESET";

    case ESP_RST_PANIC:
        return "PANIC";

    case ESP_RST_INT_WDT:
        return "INTERRUPT_WATCHDOG";

    case ESP_RST_TASK_WDT:
        return "TASK_WATCHDOG";

    case ESP_RST_WDT:
        return "OTHER_WATCHDOG";

    case ESP_RST_DEEPSLEEP:
        return "DEEP_SLEEP";

    case ESP_RST_BROWNOUT:
        return "BROWNOUT";

    case ESP_RST_SDIO:
        return "SDIO";

    default:
        return "UNKNOWN";
    }
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(
        SERIAL_BAUD);

    // USB serial must not delay local sensing/display startup.
    delay(
        100);

    Serial.println();

    const esp_reset_reason_t resetReason =
        esp_reset_reason();

    Serial.print(
        "[BOOT] Reset reason       : ");

    Serial.println(
        resetReasonText(
            resetReason));

    Serial.print(
        "[BOOT] Free heap          : ");

    Serial.println(
        ESP.getFreeHeap());

    Serial.println(
        "========================================================");

    Serial.println(
        "               HYDROSENSE ESP32-S3");

    Serial.println(
        " BLE + TINYML + SENSOR TRUST + LCD + CLOUD");

    Serial.println(
        "========================================================");

    latestSnapshot.clear();

    latestFeatures.clear();

    latestTdsResult.clear();

    latestAnomalyResult.clear();

    latestSensorTrustResult.clear();

    cloudRuntimeStatus.clear();

    // ========================================================
    // EDGE ANOMALY ENGINE
    //
    // Start with a clean in-memory baseline. For the first test,
    // power the S3 with known-clean, correctly functioning sensors.
    // ========================================================

    Serial.print(
        "Anomaly engine       : ");

    if (
        anomalyEngine.begin())
    {
        Serial.println(
            "READY - LEARNING CLEAN BASELINE");
    }
    else
    {
        Serial.println(
            "FAILED");
    }

    Serial.print(
        "Sensor trust engine   : ");

    if (
        sensorTrustEngine.begin())
    {
        Serial.println(
            "READY - EVIDENCE-GATED CROSS-LAYER TRUST ACTIVE");
    }
    else
    {
        Serial.println(
            "FAILED");
    }

    // ========================================================
    // LCD FIRST
    // ========================================================

    Serial.print(
        "LCD initialization   : ");

    if (
        lcdManager.begin())
    {
        Serial.println(
            "SUCCESS");
    }
    else
    {
        Serial.println(
            "FAILED");
    }

    // ========================================================
    // FP32 TINYML
    // ========================================================

    Serial.print(
        "TinyML initialization: ");

    if (
        tdsModel.begin())
    {
        Serial.println(
            "SUCCESS");

        Serial.println(
            "TinyML model        : Tiny_32_16_8 FP32");
    }
    else
    {
        Serial.println(
            "FAILED");
    }

    // ========================================================
    // BLE RX QUEUE
    // ========================================================

    bleRxQueue =
        xQueueCreate(
            BLE_QUEUE_DEPTH,
            sizeof(BleReceivedFrame));

    if (
        bleRxQueue ==
        nullptr)
    {
        Serial.println(
            "BLE queue            : FAILED");

        return;
    }

    Serial.println(
        "BLE queue            : READY");

    // ========================================================
    // CLOUD TX QUEUE
    // ========================================================

    cloudTxQueue =
        xQueueCreate(
            CLOUD_TX_QUEUE_DEPTH,
            sizeof(CloudTxFrame));

    if (
        cloudTxQueue ==
        nullptr)
    {
        Serial.println(
            "Cloud TX queue       : FAILED");

        return;
    }

    Serial.println(
        "Cloud TX queue       : READY");

    // ========================================================
    // CLOUD CONFIGURATION
    //
    // These calls configure URLs/device ID only.  They do not
    // wait for Wi-Fi or the backend.
    // ========================================================

    Serial.print(
        "Cloud uploader       : ");

    const bool historicalUploaderReady =
        cloudUploader.begin(
            CLOUD_TELEMETRY_URL,
            CLOUD_DEVICE_ID);

    if (
        historicalUploaderReady)
    {
        Serial.println(
            "READY");
    }
    else
    {
        Serial.println(
            "FAILED");
    }

    Serial.print(
        "Realtime uploader    : ");

    const bool realtimeUploaderReady =
        cloudUploader.beginRealtime(
            CLOUD_REALTIME_URL);

    if (
        realtimeUploaderReady)
    {
        Serial.println(
            "READY");
    }
    else
    {
        Serial.println(
            "FAILED");
    }

    S3CloudRuntimeStatus configuredCloudStatus;

    configuredCloudStatus.clear();

    configuredCloudStatus.realtime =
        realtimeUploaderReady
            ? S3CloudChannelState::WaitingForWiFi
            : S3CloudChannelState::NotConfigured;

    configuredCloudStatus.historical =
        historicalUploaderReady
            ? S3CloudChannelState::WaitingForWiFi
            : S3CloudChannelState::NotConfigured;

    writeCloudRuntimeStatus(
        configuredCloudStatus);

    // ========================================================
    // BLE BEFORE WIFI
    //
    // The LCD's C3 data path must not depend on cloud/network.
    // ========================================================

    BLEDevice::init(
        BLE_DEVICE_NAME);

    Serial.println(
        "BLE stack            : READY");

    lastBleAttemptMs =
        millis() -
        BLE_RETRY_INTERVAL_MS;

    const BaseType_t bleTaskCreated =
        xTaskCreate(
            bleManagerTaskEntry,
            "HydroBLE",
            BLE_TASK_STACK_SIZE,
            nullptr,
            BACKGROUND_TASK_PRIORITY,
            &bleManagerTaskHandle);

    Serial.print(
        "BLE manager task     : ");

    Serial.println(
        bleTaskCreated ==
                pdPASS
            ? "READY"
            : "FAILED");

    if (
        bleTaskCreated !=
        pdPASS)
    {
        bleManagerTaskHandle =
            nullptr;
    }

    const BaseType_t cloudTaskCreated =
        xTaskCreate(
            cloudTaskEntry,
            "HydroCloud",
            CLOUD_TASK_STACK_SIZE,
            nullptr,
            BACKGROUND_TASK_PRIORITY,
            &cloudTaskHandle);

    Serial.print(
        "Cloud async task     : ");

    Serial.println(
        cloudTaskCreated ==
                pdPASS
            ? "READY"
            : "FAILED");

    if (
        cloudTaskCreated !=
        pdPASS)
    {
        cloudTaskHandle =
            nullptr;

        S3CloudRuntimeStatus failedCloudStatus =
            readCloudRuntimeStatus();

        if (
            failedCloudStatus.realtime !=
            S3CloudChannelState::NotConfigured)
        {
            failedCloudStatus.realtime =
                S3CloudChannelState::RetryPending;
        }

        if (
            failedCloudStatus.historical !=
            S3CloudChannelState::NotConfigured)
        {
            failedCloudStatus.historical =
                S3CloudChannelState::RetryPending;
        }

        writeCloudRuntimeStatus(
            failedCloudStatus);
    }

    // Start Wi-Fi last and do not wait for association.
    startWiFi();

    Serial.println();

    Serial.println(
        "HydroSense realtime runtime started.");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    // Process genuine C3 packets before sampling the loop time.
    // processBleFrame() timestamps a new packet with millis().
    // Sampling nowMs first can make latestPacketReceivedMs newer
    // than nowMs for this loop pass, causing unsigned age
    // subtraction to wrap and momentarily classify fresh data
    // as stale.
    processBleQueue();

    const uint32_t nowMs =
        millis();

    // BLE callback reported a link loss.
    if (
        c3DisconnectEventPending)
    {
        c3DisconnectEventPending =
            false;

        // A disconnect callback can be followed by a very fast
        // reconnect.  Only clear live state if the link is still
        // actually down when the main loop handles the event.
        if (
            !c3Connected)
        {
            haveSensorPacket =
                false;

            publishC3DisconnectedState();
        }
    }

    // Keep local controls/display ahead of Wi-Fi maintenance.
    // Wi-Fi remains non-blocking, but the local C3 -> LCD path
    // should always get first priority in the main loop.
    lcdManager.updateControls(
        nowMs);

    updateLCD(
        nowMs);

    // Non-blocking Wi-Fi maintenance.
    maintainWiFi(
        nowMs);

    if (
        nowMs -
            lastStatusMs >=
        STATUS_INTERVAL_MS)
    {
        lastStatusMs =
            nowMs;

        printGatewayStatus(
            nowMs);
    }

    delay(
        2);
}
