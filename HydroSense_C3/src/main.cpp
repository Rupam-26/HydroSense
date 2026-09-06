#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <climits>
#include <cstring>
#include <ctype.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "pins.h"

#include "sensors/DHT22Sensor.h"
#include "sensors/DS18B20Sensor.h"
#include "sensors/UltrasonicSensor.h"
#include "filters/MedianFilter.h"
#include "physics/UltrasonicPhysics.h"
#include "reservoir/ReservoirCalibration.h"
#include "analytics/C3AnalyticsEngine.h"
#include "diagnostics/SensorHealthManager.h"
#include "diagnostics/DataQualityEngine.h"
#include "models/C3AnalyticsData.h"
#include "models/SensorHealthData.h"
#include "models/DataQualityData.h"
#include "models/C3DisplayData.h"
#include "input/ButtonManager.h"
#include "ui/OLEDManager.h"
#include "ui/C3DisplayController.h"

// ============================================================
// HYDROSENSE ESP32-C3
//
// Real sensors -> physics -> robust sonar -> reservoir model
// -> analytics -> health / quality -> local OLED -> 20-byte BLE
//
// The BLE protocol remains exactly compatible with the S3.
// ============================================================

namespace
{
    // ------------------------------------------------------------
    // Runtime
    // ------------------------------------------------------------
    constexpr uint32_t SERIAL_BAUD = 115200UL;

    // Restore the clean first-testing-phase ownership model:
    // each physical sensor is serviced at its own appropriate cadence.
    // BLE/analytics publication remains on the proven current cadence.
    // Minimum interval between COMPLETED genuine DHT22 acquisitions.
    // This is deliberately not used as a blind timer for cached reads.
    constexpr uint32_t DHT_INTERVAL_MS = 2000UL;
    // DHT22 datasheets recommend allowing the device to stabilize after
    // power-up. Honor that without blocking the rest of the C3 runtime.
    constexpr uint32_t DHT_STARTUP_WARMUP_MS = 2000UL;
    constexpr uint32_t DS18_REQUEST_INTERVAL_MS = 2000UL;
    constexpr uint32_t TELEMETRY_INTERVAL_MS = 3000UL;
    constexpr uint32_t SONAR_INTERVAL_MS = 250UL;

    // Sensor-health timing restored from the first confirmed testing
    // implementation. These are freshness policy timings, not sensor
    // readings or fabricated values.
    constexpr SensorHealthPolicy DHT22_HEALTH_POLICY{5000UL, 15000UL};
    constexpr SensorHealthPolicy DS18B20_HEALTH_POLICY{5000UL, 15000UL};
    constexpr SensorHealthPolicy ULTRASONIC_HEALTH_POLICY{1500UL, 5000UL};

    // Sonar is sampled four times per second. A short run of missed
    // echoes may hold the last trusted value, but a sustained loss is
    // invalidated quickly rather than being displayed for many seconds.
    constexpr uint32_t SONAR_HOLD_MAX_MS = 2000UL;
    constexpr uint8_t SONAR_FAILURES_BEFORE_INVALID = 6;

    // Seven recent genuine corrected measurements provide enough
    // samples for robust averaging while keeping response time short.
    constexpr uint8_t DISTANCE_FILTER_WINDOW = 7;
    constexpr uint8_t DISTANCE_MIN_SAMPLES = 5;

    // The S3 BLE contract carries distance at 0.1 cm resolution.
    // Changes smaller than one transmitted step refresh freshness but
    // do not force the stable output to chatter around a boundary.
    constexpr float DISTANCE_STABILITY_DEADBAND_CM = 0.10f;

    constexpr uint8_t ANALYTICS_TREND_WINDOW = 10;
    constexpr uint32_t BUTTON_DEBOUNCE_MS = 35UL;
    constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;

    // SH1106 full-frame I2C transfer is approximately 25 ms at the
    // Adafruit driver's 400-kHz active transfer rate. Never begin an
    // OLED refresh immediately before the next 250-ms sonar deadline.
    constexpr uint32_t OLED_SONAR_GUARD_MS = 40UL;

    // Long Serial command/status output is deliberately launched only
    // just after a real sonar acquisition, leaving almost a complete
    // 250-ms acquisition period before the next HC-SR04 deadline.
    constexpr uint32_t POST_SONAR_WORK_WINDOW_MS = 30UL;

    constexpr float NOMINAL_SOUND_SPEED_MPS = 343.0f;
    constexpr float PI_F = 3.14159265358979323846f;

    // ------------------------------------------------------------
    // BLE - DO NOT CHANGE; S3 depends on this contract.
    // ------------------------------------------------------------
    const char *BLE_DEVICE_NAME = "HydroSense_C3";
    const char *SERVICE_UUID = "7b2c1000-6e9f-4b5a-9f8e-4a7c33b10001";
    const char *TELEMETRY_CHARACTERISTIC_UUID =
        "7b2c1001-6e9f-4b5a-9f8e-4a7c33b10001";

    constexpr uint16_t HYDROSENSE_MAGIC = 0x4853;
    constexpr uint8_t HYDROSENSE_PROTOCOL_VERSION = 1;

    // Preserve the proven 100-ms stack-settle period after a BLE
    // disconnect, but never sleep inside a BLE callback. Advertising
    // restart is deferred to loop() and serviced just after a sonar
    // acquisition so the 250-ms HC-SR04 schedule keeps priority.
    constexpr uint32_t BLE_ADVERTISING_RESTART_DELAY_MS = 100UL;
    constexpr uint32_t BLE_RESTART_AFTER_SONAR_WINDOW_MS = 60UL;

    // ------------------------------------------------------------
    // Reservoir profile NVS. Full/empty remain in the existing
    // ReservoirCalibration class; geometry + HALF live here.
    // ------------------------------------------------------------
    const char *PROFILE_NAMESPACE = "hydro-res";
    const char *KEY_TYPE = "type";
    const char *KEY_LENGTH = "len_cm";
    const char *KEY_WIDTH = "wid_cm";
    const char *KEY_HEIGHT = "hei_cm";
    const char *KEY_DIAMETER = "dia_cm";
    const char *KEY_HALF = "half_cm";
} // namespace

// ============================================================
// BLE PACKET - EXACTLY 20 BYTES
// ============================================================

enum C3ValidityBits : uint8_t
{
    C3_VALID_DHT22 = 1 << 0,
    C3_VALID_DS18B20 = 1 << 1,
    C3_VALID_ULTRASONIC = 1 << 2,
    C3_VALID_RESERVOIR = 1 << 3,
    C3_VALID_CALIBRATION = 1 << 4
};

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

static_assert(sizeof(C3BlePacket) == 20,
              "C3BlePacket must be exactly 20 bytes");

// ============================================================
// HARDWARE / PROCESSING OBJECTS
// ============================================================

DHT22Sensor airSensor(DHT_PIN);
DS18B20Sensor waterSensor(DS18B20_PIN);
UltrasonicSensor ultrasonic(TRIG_PIN, ECHO_PIN);

MedianFilter distanceFilter(DISTANCE_FILTER_WINDOW);
ReservoirCalibration calibration;
C3AnalyticsEngine analytics(ANALYTICS_TREND_WINDOW);
SensorHealthManager healthManager(
    DHT22_HEALTH_POLICY,
    DS18B20_HEALTH_POLICY,
    ULTRASONIC_HEALTH_POLICY);
DataQualityEngine dataQualityEngine;

ButtonManager button(BTN_PIN, BUTTON_DEBOUNCE_MS);
OLEDManager oled(I2C_SDA, I2C_SCL, OLED_I2C_ADDRESS);
C3DisplayController displayController(oled);

Preferences reservoirProfilePreferences;
bool reservoirProfileStorageReady = false;

// ============================================================
// RESERVOIR PROFILE
// ============================================================

struct ReservoirProfile
{
    C3ReservoirType type = C3ReservoirType::UNCONFIGURED;

    float lengthCm = NAN;
    float widthCm = NAN;
    float heightCm = NAN;
    float diameterCm = NAN;

    bool halfAvailable = false;
    float halfDistanceCm = NAN;
};

ReservoirProfile reservoirProfile{};

// ============================================================
// TERMINAL SETUP STATE
// ============================================================

enum class ReservoirSetupState : uint8_t
{
    IDLE = 0,
    WAIT_TYPE,
    WAIT_RECT_LENGTH,
    WAIT_RECT_WIDTH,
    WAIT_RECT_HEIGHT,
    WAIT_DRUM_DIAMETER,
    WAIT_DRUM_HEIGHT
};

ReservoirSetupState reservoirSetupState = ReservoirSetupState::IDLE;
String terminalLine;
String pendingTerminalLine;
bool terminalCommandPending = false;

// Periodic Serial telemetry is presentation only.
// It can be paused during reservoir setup without stopping:
// - sensor acquisition
// - OLED updates
// - BLE notifications to the S3
// - analytics / health / quality processing
bool serialLiveOutputEnabled = true;

C3ReservoirType pendingReservoirType = C3ReservoirType::UNCONFIGURED;
float pendingLengthCm = NAN;
float pendingWidthCm = NAN;
float pendingHeightCm = NAN;
float pendingDiameterCm = NAN;

// ============================================================
// BLE STATE
// ============================================================

BLEServer *bleServer = nullptr;
BLECharacteristic *telemetryCharacteristic = nullptr;

volatile bool s3Connected = false;

// BLE callbacks stay intentionally tiny. They only publish state/event
// flags; Serial output and advertising restart are handled by loop().
volatile bool bleConnectEventPending = false;
volatile bool bleDisconnectEventPending = false;
volatile bool bleAdvertisingRestartPending = false;
volatile uint32_t bleDisconnectedAtMs = 0;

uint8_t packetSequence = 0;

// ============================================================
// RUNTIME STATE
// ============================================================

uint32_t measurementNumber = 0;

// Independent DHT22 scheduler.
uint32_t lastDHTMeasurementMs = 0;
uint32_t dhtInitializedAtMs = 0;
bool firstDHTMeasurement = true;
bool dhtAttemptSeen = false;

// Independent asynchronous DS18B20 scheduler.
uint32_t lastDS18RequestMs = 0;
bool firstDS18Request = true;
bool ds18AttemptSeen = false;

// Analytics / quality / BLE publication scheduler.
uint32_t lastTelemetryMs = 0;
bool firstTelemetryMeasurement = true;
bool telemetrySerialReportPending = false;

// Runtime presentation is strictly deferred to the post-sonar window.
// Sensor/transport callbacks and page-button handling only queue compact
// diagnostic events; they never print directly in the timing path.
bool bleConnectedSerialReportPending = false;
bool bleDisconnectedSerialReportPending = false;
bool bleAdvertisingSerialReportPending = false;
bool buttonSerialReportPending = false;
bool bleTxSerialReportPending = false;

uint32_t pendingButtonPressCount = 0;
uint8_t pendingButtonPageNumber = 1;
uint8_t pendingBleTxSequence = 0;
uint8_t pendingBleTxQualityPct = 0;
uint8_t pendingBleTxValidMask = 0;
uint16_t pendingBleTxCrc16 = 0;

// Ensures at most one potentially blocking Serial presentation job is
// started for each completed HC-SR04 acquisition period.
uint32_t serialPresentationSonarCompletionMs = UINT32_MAX;

uint32_t lastSonarMeasurementMs = 0;
uint32_t lastSonarCompletedMs = 0;
bool firstSonarMeasurement = true;

bool currentUltrasonicPingValid = false;
bool distanceHeld = false;

bool haveStableDistance = false;
float latestStableDistanceCm = NAN;
uint32_t latestStableDistanceTimestampMs = 0;

uint32_t latestGoodEchoTimeUs = 0;
float latestGoodRawDistanceCm = NAN;
float latestGoodCorrectedDistanceCm = NAN;
float latestSpeedOfSoundMps = NAN;

bool latestReservoirValid = false;
float latestWaterLevelPct = NAN;
float latestWaterDepthCm = NAN;
float latestUsableDepthCm = NAN;
float latestVolumeL = NAN;

C3AnalyticsData latestAnalyticsData{};
NodeHealthData latestHealthData{};
DataQualityData latestQualityData{};
C3DisplayData latestDisplayData{};

uint32_t displaySnapshotRevision = 0;

// ============================================================
// BASIC HELPERS
// ============================================================

float clampFloat(float value, float low, float high)
{
    if (value < low)
        return low;

    if (value > high)
        return high;

    return value;
}

bool positiveFinite(float value)
{
    return isfinite(value) && value > 0.0f;
}

uint32_t stableDistanceAgeMs(uint32_t nowMs)
{
    if (!haveStableDistance)
    {
        return UINT32_MAX;
    }

    return nowMs - latestStableDistanceTimestampMs;
}

bool stableDistanceFresh(uint32_t nowMs)
{
    return haveStableDistance &&
           isfinite(latestStableDistanceCm) &&
           stableDistanceAgeMs(nowMs) <= SONAR_HOLD_MAX_MS;
}

// True only in the short window immediately after a completed sonar
// acquisition. Timing-sensitive or potentially longer secondary work
// (DHT22 acquisition, terminal/status output) is launched here so it
// cannot pre-empt the next ping.
bool postSonarWorkWindowOpen(uint32_t nowMs)
{
    if (firstSonarMeasurement)
    {
        return false;
    }

    return (nowMs - lastSonarCompletedMs) <=
           POST_SONAR_WORK_WINDOW_MS;
}

const char *reservoirTypeLongText(C3ReservoirType type)
{
    switch (type)
    {
    case C3ReservoirType::RECTANGULAR:
        return "RECTANGULAR";

    case C3ReservoirType::VERTICAL_CYLINDER:
        return "VERTICAL CYLINDER / DRUM";

    default:
        return "NOT CONFIGURED";
    }
}

// ============================================================
// RESERVOIR PROFILE STORAGE / GEOMETRY
// ============================================================

bool geometryConfigured()
{
    if (reservoirProfile.type == C3ReservoirType::RECTANGULAR)
    {
        return positiveFinite(reservoirProfile.lengthCm) &&
               positiveFinite(reservoirProfile.widthCm) &&
               positiveFinite(reservoirProfile.heightCm);
    }

    if (reservoirProfile.type == C3ReservoirType::VERTICAL_CYLINDER)
    {
        return positiveFinite(reservoirProfile.diameterCm) &&
               positiveFinite(reservoirProfile.heightCm);
    }

    return false;
}

bool calibrationDepthFitsReservoirHeight()
{
    if (!calibration.isValid() ||
        !geometryConfigured())
    {
        return false;
    }

    const float usableDepthCm =
        calibration.usableDepthCm();

    return positiveFinite(usableDepthCm) &&
           usableDepthCm <=
               reservoirProfile.heightCm;
}

float reservoirCrossSectionAreaCm2()
{
    if (!geometryConfigured())
    {
        return NAN;
    }

    if (reservoirProfile.type == C3ReservoirType::RECTANGULAR)
    {
        // Straight-sided rectangular tank:
        // horizontal cross-sectional area = length x width.
        return reservoirProfile.lengthCm *
               reservoirProfile.widthCm;
    }

    // Straight-sided vertical cylinder / drum:
    // horizontal cross-sectional area = pi r^2.
    const float radiusCm =
        reservoirProfile.diameterCm / 2.0f;

    return PI_F * radiusCm * radiusCm;
}

float reservoirCapacityL()
{
    const float areaCm2 =
        reservoirCrossSectionAreaCm2();

    if (!isfinite(areaCm2) ||
        !positiveFinite(reservoirProfile.heightCm))
    {
        return NAN;
    }

    // Total geometric tank capacity.
    // 1000 cm^3 = 1 litre.
    return (areaCm2 * reservoirProfile.heightCm) /
           1000.0f;
}

float calibratedFullVolumeL()
{
    if (!calibrationDepthFitsReservoirHeight())
    {
        return NAN;
    }

    const float areaCm2 =
        reservoirCrossSectionAreaCm2();

    const float usableDepthCm =
        calibration.usableDepthCm();

    if (!isfinite(areaCm2) ||
        !positiveFinite(usableDepthCm))
    {
        return NAN;
    }

    // FULL and EMPTY ultrasonic calibration points define the
    // actual operating water-depth range. This is intentionally
    // not assumed to be the same as the tank's total height.
    return (areaCm2 * usableDepthCm) /
           1000.0f;
}

bool halfCalibrationValid()
{
    if (!reservoirProfile.halfAvailable ||
        !isfinite(reservoirProfile.halfDistanceCm) ||
        !calibration.hasFullDistance() ||
        !calibration.hasEmptyDistance())
    {
        return false;
    }

    return calibration.fullDistanceCm() <
               reservoirProfile.halfDistanceCm &&
           reservoirProfile.halfDistanceCm <
               calibration.emptyDistanceCm();
}

void resetReservoirProfileMemory()
{
    reservoirProfile = ReservoirProfile{};
}

bool loadReservoirProfile()
{
    reservoirProfileStorageReady =
        reservoirProfilePreferences.begin(
            PROFILE_NAMESPACE,
            false);

    if (!reservoirProfileStorageReady)
    {
        resetReservoirProfileMemory();
        return false;
    }

    const uint8_t storedType =
        reservoirProfilePreferences.getUChar(
            KEY_TYPE,
            0);

    if (storedType ==
        static_cast<uint8_t>(
            C3ReservoirType::RECTANGULAR))
    {
        reservoirProfile.type =
            C3ReservoirType::RECTANGULAR;
    }
    else if (storedType ==
             static_cast<uint8_t>(
                 C3ReservoirType::VERTICAL_CYLINDER))
    {
        reservoirProfile.type =
            C3ReservoirType::VERTICAL_CYLINDER;
    }

    if (reservoirProfilePreferences.isKey(KEY_LENGTH))
    {
        reservoirProfile.lengthCm =
            reservoirProfilePreferences.getFloat(
                KEY_LENGTH,
                NAN);
    }

    if (reservoirProfilePreferences.isKey(KEY_WIDTH))
    {
        reservoirProfile.widthCm =
            reservoirProfilePreferences.getFloat(
                KEY_WIDTH,
                NAN);
    }

    if (reservoirProfilePreferences.isKey(KEY_HEIGHT))
    {
        reservoirProfile.heightCm =
            reservoirProfilePreferences.getFloat(
                KEY_HEIGHT,
                NAN);
    }

    if (reservoirProfilePreferences.isKey(KEY_DIAMETER))
    {
        reservoirProfile.diameterCm =
            reservoirProfilePreferences.getFloat(
                KEY_DIAMETER,
                NAN);
    }

    if (reservoirProfilePreferences.isKey(KEY_HALF))
    {
        const float half =
            reservoirProfilePreferences.getFloat(
                KEY_HALF,
                NAN);

        if (positiveFinite(half))
        {
            reservoirProfile.halfAvailable = true;
            reservoirProfile.halfDistanceCm = half;
        }
    }

    return true;
}

bool saveReservoirGeometry()
{
    if (!reservoirProfileStorageReady ||
        !geometryConfigured())
    {
        return false;
    }

    bool ok = true;

    ok =
        reservoirProfilePreferences.putUChar(
            KEY_TYPE,
            static_cast<uint8_t>(
                reservoirProfile.type)) >
            0 &&
        ok;

    if (reservoirProfile.type ==
        C3ReservoirType::RECTANGULAR)
    {
        ok =
            reservoirProfilePreferences.putFloat(
                KEY_LENGTH,
                reservoirProfile.lengthCm) >
                0 &&
            ok;

        ok =
            reservoirProfilePreferences.putFloat(
                KEY_WIDTH,
                reservoirProfile.widthCm) >
                0 &&
            ok;

        ok =
            reservoirProfilePreferences.putFloat(
                KEY_HEIGHT,
                reservoirProfile.heightCm) >
                0 &&
            ok;

        reservoirProfilePreferences.remove(
            KEY_DIAMETER);
    }
    else
    {
        ok =
            reservoirProfilePreferences.putFloat(
                KEY_DIAMETER,
                reservoirProfile.diameterCm) >
                0 &&
            ok;

        ok =
            reservoirProfilePreferences.putFloat(
                KEY_HEIGHT,
                reservoirProfile.heightCm) >
                0 &&
            ok;

        reservoirProfilePreferences.remove(
            KEY_LENGTH);

        reservoirProfilePreferences.remove(
            KEY_WIDTH);
    }

    return ok;
}

bool saveHalfDistance(float distanceCm)
{
    if (!reservoirProfileStorageReady ||
        !positiveFinite(distanceCm))
    {
        return false;
    }

    if (reservoirProfilePreferences.putFloat(
            KEY_HALF,
            distanceCm) == 0)
    {
        return false;
    }

    reservoirProfile.halfAvailable = true;
    reservoirProfile.halfDistanceCm = distanceCm;

    return true;
}

// ============================================================
// THREE-POINT RESERVOIR CALCULATION
//
// FULL + HALF + EMPTY are used when all three are physically
// ordered. If HALF has not yet been captured, the existing
// FULL/EMPTY linear calibration remains valid.
// ============================================================

bool calculateReservoirFromDistance(float distanceCm)
{
    latestReservoirValid = false;
    latestWaterLevelPct = NAN;
    latestWaterDepthCm = NAN;
    latestUsableDepthCm = NAN;
    latestVolumeL = NAN;

    if (!calibration.isValid() ||
        !isfinite(distanceCm))
    {
        return false;
    }

    const float full =
        calibration.fullDistanceCm();

    const float empty =
        calibration.emptyDistanceCm();

    const float usable =
        empty - full;

    if (!positiveFinite(usable))
    {
        return false;
    }

    float levelPct = NAN;

    if (halfCalibrationValid())
    {
        const float half =
            reservoirProfile.halfDistanceCm;

        if (distanceCm <= full)
        {
            levelPct = 100.0f;
        }
        else if (distanceCm >= empty)
        {
            levelPct = 0.0f;
        }
        else if (distanceCm <= half)
        {
            const float fraction =
                (distanceCm - full) /
                (half - full);

            levelPct =
                100.0f -
                (50.0f * fraction);
        }
        else
        {
            const float fraction =
                (distanceCm - half) /
                (empty - half);

            levelPct =
                50.0f -
                (50.0f * fraction);
        }
    }
    else
    {
        levelPct =
            100.0f *
            (empty - distanceCm) /
            usable;
    }

    levelPct =
        clampFloat(
            levelPct,
            0.0f,
            100.0f);

    latestWaterLevelPct =
        levelPct;

    latestUsableDepthCm =
        usable;

    latestWaterDepthCm =
        usable *
        (levelPct / 100.0f);

    const float areaCm2 =
        reservoirCrossSectionAreaCm2();

    if (isfinite(areaCm2) &&
        calibrationDepthFitsReservoirHeight())
    {
        // For both supported shapes the horizontal cross-section
        // is constant, so physical liquid volume is exactly:
        //
        // V = cross-sectional area x water depth
        //
        // This is more accurate than multiplying the tank's total
        // geometric capacity by level percent when the configured
        // FULL operating limit is below the physical tank rim.
        latestVolumeL =
            (areaCm2 *
             latestWaterDepthCm) /
            1000.0f;
    }

    latestReservoirValid = true;

    return true;
}

// ============================================================
// CRC + ENCODING
// ============================================================

uint16_t calculateCRC16(
    const uint8_t *data,
    size_t length)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0;
         i < length;
         ++i)
    {
        crc ^=
            static_cast<uint16_t>(
                data[i])
            << 8;

        for (uint8_t bit = 0;
             bit < 8;
             ++bit)
        {
            crc =
                (crc & 0x8000)
                    ? static_cast<uint16_t>(
                          (crc << 1) ^
                          0x1021)
                    : static_cast<uint16_t>(
                          crc << 1);
        }
    }

    return crc;
}

int16_t encodeSignedCenti(float value)
{
    if (!isfinite(value))
    {
        return INT16_MIN;
    }

    float scaled =
        clampFloat(
            value * 100.0f,
            -32767.0f,
            32767.0f);

    return static_cast<int16_t>(
        lroundf(scaled));
}

uint16_t encodeUnsignedCenti(float value)
{
    if (!isfinite(value))
    {
        return UINT16_MAX;
    }

    float scaled =
        clampFloat(
            value * 100.0f,
            0.0f,
            65534.0f);

    return static_cast<uint16_t>(
        lroundf(scaled));
}

uint16_t encodeDistanceMm(float distanceCm)
{
    if (!isfinite(distanceCm))
    {
        return UINT16_MAX;
    }

    float millimetres =
        clampFloat(
            distanceCm * 10.0f,
            0.0f,
            65534.0f);

    return static_cast<uint16_t>(
        lroundf(millimetres));
}

uint8_t encodeQualityScore(float qualityScore)
{
    if (!isfinite(qualityScore))
    {
        return 0;
    }

    return static_cast<uint8_t>(
        lroundf(
            clampFloat(
                qualityScore,
                0.0f,
                100.0f)));
}

// ============================================================
// BLE SERVER
// ============================================================

class HydroSenseServerCallbacks
    : public BLEServerCallbacks
{
public:
    void onConnect(
        BLEServer *server) override
    {
        (void)server;

        // Callback context must remain short and non-blocking.
        s3Connected = true;

        bleAdvertisingRestartPending = false;
        bleDisconnectEventPending = false;
        bleConnectEventPending = true;
    }

    void onDisconnect(
        BLEServer *server) override
    {
        (void)server;

        // Do not delay(), print large messages, or restart advertising
        // from BLE callback context. Record the real event and let the
        // main scheduler perform the recovery safely.
        s3Connected = false;

        bleDisconnectedAtMs =
            millis();

        bleConnectEventPending = false;
        bleDisconnectEventPending = true;
        bleAdvertisingRestartPending = true;
    }
};

bool initializeBLE()
{
    s3Connected = false;
    bleConnectEventPending = false;
    bleDisconnectEventPending = false;
    bleAdvertisingRestartPending = false;
    bleDisconnectedAtMs = 0;

    Serial.println();
    Serial.println(
        "Initializing BLE...");

    BLEDevice::init(
        BLE_DEVICE_NAME);

    bleServer =
        BLEDevice::createServer();

    if (bleServer == nullptr)
    {
        return false;
    }

    bleServer->setCallbacks(
        new HydroSenseServerCallbacks());

    BLEService *service =
        bleServer->createService(
            SERVICE_UUID);

    if (service == nullptr)
    {
        return false;
    }

    telemetryCharacteristic =
        service->createCharacteristic(
            TELEMETRY_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ |
                BLECharacteristic::PROPERTY_NOTIFY);

    if (telemetryCharacteristic == nullptr)
    {
        return false;
    }

    telemetryCharacteristic->addDescriptor(
        new BLE2902());

    telemetryCharacteristic->setValue(
        "HYDROSENSE_READY");

    service->start();

    BLEAdvertising *advertising =
        BLEDevice::getAdvertising();

    if (advertising == nullptr)
    {
        return false;
    }

    advertising->addServiceUUID(
        SERVICE_UUID);

    advertising->setScanResponse(
        true);

    BLEDevice::startAdvertising();

    Serial.println(
        "BLE Server          : READY");

    Serial.println(
        "Telemetry           : READY");

    Serial.println(
        "Advertising         : ACTIVE");

    return true;
}

// ============================================================
// BLE CONNECTION EVENT SERVICE
//
// BLE callbacks only set flags. This routine runs in the ordinary C3
// scheduler so a disconnect never sleeps inside the BLE host callback.
//
// Advertising restart retains the proven 100-ms settle interval and is
// additionally aligned just after HC-SR04 service. This keeps transport
// recovery from competing with the 250-ms real sonar acquisition.
// ============================================================

void serviceBLEConnectionEvents(
    uint32_t nowMs)
{
    if (bleConnectEventPending)
    {
        bleConnectEventPending =
            false;

        bleConnectedSerialReportPending =
            true;
    }

    if (bleDisconnectEventPending)
    {
        bleDisconnectEventPending =
            false;

        bleDisconnectedSerialReportPending =
            true;
    }

    if (!bleAdvertisingRestartPending ||
        s3Connected)
    {
        return;
    }

    if (nowMs -
            bleDisconnectedAtMs <
        BLE_ADVERTISING_RESTART_DELAY_MS)
    {
        return;
    }

    // On an established runtime, restart advertising only in the short
    // window immediately following a sonar acquisition. During startup,
    // before the first sonar sample, no such guard is necessary.
    bool restartWindowSafe =
        firstSonarMeasurement;

    if (!firstSonarMeasurement)
    {
        const uint32_t sinceLastSonarCompletedMs =
            nowMs -
            lastSonarCompletedMs;

        restartWindowSafe =
            sinceLastSonarCompletedMs <=
            BLE_RESTART_AFTER_SONAR_WINDOW_MS;
    }

    if (!restartWindowSafe)
    {
        return;
    }

    BLEDevice::startAdvertising();

    bleAdvertisingRestartPending =
        false;

    bleAdvertisingSerialReportPending =
        true;
}

// ============================================================
// DISPLAY SNAPSHOT
// ============================================================

void buildDisplaySnapshot(
    uint32_t nowMs)
{
    latestDisplayData.measurementNumber =
        measurementNumber;

    latestDisplayData.uptimeMs =
        nowMs;

    latestDisplayData.analytics =
        latestAnalyticsData;

    latestDisplayData.health =
        latestHealthData;

    latestDisplayData.quality =
        latestQualityData;

    // DHT22 and DS18B20 now run independently of the slower telemetry
    // publication cycle. Overlay their latest genuinely valid direct
    // readings so the local OLED reflects the newest physical sensor
    // state without waiting for the next BLE/analytics publication.
    if (latestHealthData.dht22.valid &&
        isfinite(airSensor.temperatureC()) &&
        isfinite(airSensor.humidityPct()))
    {
        latestDisplayData.analytics.dhtValid =
            true;

        latestDisplayData.analytics.airTemperatureC =
            airSensor.temperatureC();

        latestDisplayData.analytics.humidityPct =
            airSensor.humidityPct();
    }
    else
    {
        latestDisplayData.analytics.dhtValid =
            false;

        latestDisplayData.analytics.airTemperatureC =
            NAN;

        latestDisplayData.analytics.humidityPct =
            NAN;
    }

    if (latestHealthData.ds18b20.valid &&
        isfinite(waterSensor.temperatureC()))
    {
        latestDisplayData.analytics.ds18b20Valid =
            true;

        latestDisplayData.analytics.waterTemperatureC =
            waterSensor.temperatureC();
    }
    else
    {
        latestDisplayData.analytics.ds18b20Valid =
            false;

        latestDisplayData.analytics.waterTemperatureC =
            NAN;
    }

    // The analytics object is updated on the normal telemetry cadence,
    // but sonar now runs independently every 250 ms. Overlay the latest
    // real sonar/reservoir state so the local OLED reflects the newest
    // trusted physical measurement rather than waiting for the next
    // 3-second telemetry cycle.
    if (stableDistanceFresh(nowMs))
    {
        latestDisplayData.analytics.echoTimeUs =
            latestGoodEchoTimeUs;

        latestDisplayData.analytics.rawDistanceCm =
            latestGoodRawDistanceCm;

        latestDisplayData.analytics.correctedDistanceCm =
            latestGoodCorrectedDistanceCm;

        latestDisplayData.analytics.stableDistanceCm =
            latestStableDistanceCm;
    }
    else
    {
        latestDisplayData.analytics.echoTimeUs =
            0;

        latestDisplayData.analytics.rawDistanceCm =
            NAN;

        latestDisplayData.analytics.correctedDistanceCm =
            NAN;

        latestDisplayData.analytics.stableDistanceCm =
            NAN;
    }

    if (latestReservoirValid)
    {
        latestDisplayData.analytics.waterDepthCm =
            latestWaterDepthCm;

        latestDisplayData.analytics.waterLevelPct =
            latestWaterLevelPct;

        latestDisplayData.analytics.usableDepthCm =
            latestUsableDepthCm;
    }
    else
    {
        latestDisplayData.analytics.waterDepthCm =
            NAN;

        latestDisplayData.analytics.waterLevelPct =
            NAN;

        latestDisplayData.analytics.usableDepthCm =
            NAN;
    }

    latestDisplayData.currentSonarPingValid =
        currentUltrasonicPingValid;

    latestDisplayData.distanceHeld =
        distanceHeld;

    latestDisplayData.distanceAgeMs =
        haveStableDistance
            ? stableDistanceAgeMs(nowMs)
            : UINT32_MAX;

    latestDisplayData.currentStableDistanceCm =
        stableDistanceFresh(nowMs)
            ? latestStableDistanceCm
            : NAN;

    latestDisplayData.speedOfSoundMps =
        latestSpeedOfSoundMps;

    latestDisplayData.fullDistanceAvailable =
        calibration.hasFullDistance();

    latestDisplayData.halfDistanceAvailable =
        reservoirProfile.halfAvailable;

    latestDisplayData.emptyDistanceAvailable =
        calibration.hasEmptyDistance();

    latestDisplayData.fullDistanceCm =
        calibration.hasFullDistance()
            ? calibration.fullDistanceCm()
            : NAN;

    latestDisplayData.halfDistanceCm =
        reservoirProfile.halfAvailable
            ? reservoirProfile.halfDistanceCm
            : NAN;

    latestDisplayData.emptyDistanceCm =
        calibration.hasEmptyDistance()
            ? calibration.emptyDistanceCm()
            : NAN;

    latestDisplayData.calibrationRangeCm =
        calibration.isValid()
            ? calibration.usableDepthCm()
            : NAN;

    latestDisplayData.reservoirType =
        reservoirProfile.type;

    latestDisplayData.geometryConfigured =
        geometryConfigured();

    latestDisplayData.reservoirLengthCm =
        reservoirProfile.lengthCm;

    latestDisplayData.reservoirWidthCm =
        reservoirProfile.widthCm;

    latestDisplayData.reservoirHeightCm =
        reservoirProfile.heightCm;

    latestDisplayData.reservoirDiameterCm =
        reservoirProfile.diameterCm;

    latestDisplayData.reservoirCapacityL =
        reservoirCapacityL();

    latestDisplayData.currentVolumeL =
        latestReservoirValid
            ? latestVolumeL
            : NAN;

    // Every call records a new real-state snapshot revision. The OLED
    // controller separately hashes only the active page at its rendered
    // precision, so fast sensor refresh does not imply a full I2C frame.
    ++displaySnapshotRevision;

    // Keep zero reserved for the unrendered/default snapshot state.
    if (displaySnapshotRevision == 0)
    {
        ++displaySnapshotRevision;
    }

    latestDisplayData.revision =
        displaySnapshotRevision;
}

// ============================================================
// BLE PACKET TRANSMISSION
// ============================================================

void sendSensorPacket(
    uint32_t nowMs)
{
    if (telemetryCharacteristic == nullptr)
    {
        return;
    }

    C3BlePacket packet{};

    packet.magic =
        HYDROSENSE_MAGIC;

    packet.version =
        HYDROSENSE_PROTOCOL_VERSION;

    packet.sequence =
        packetSequence++;

    packet.airTemperature_cC =
        encodeSignedCenti(
            latestAnalyticsData.airTemperatureC);

    packet.humidity_cPct =
        encodeUnsignedCenti(
            latestAnalyticsData.humidityPct);

    packet.waterTemperature_cC =
        encodeSignedCenti(
            latestAnalyticsData.waterTemperatureC);

    const bool sonarUsable =
        stableDistanceFresh(nowMs);

    packet.stableDistance_mm =
        encodeDistanceMm(
            sonarUsable
                ? latestStableDistanceCm
                : NAN);

    packet.waterLevel_cPct =
        encodeUnsignedCenti(
            latestReservoirValid
                ? latestWaterLevelPct
                : NAN);

    packet.qualityPct =
        encodeQualityScore(
            latestQualityData.scorePct);

    packet.qualityFlags =
        latestQualityData.flags;

    packet.validMask = 0;

    if (latestHealthData.dht22.valid)
    {
        packet.validMask |=
            C3_VALID_DHT22;
    }

    if (latestHealthData.ds18b20.valid)
    {
        packet.validMask |=
            C3_VALID_DS18B20;
    }

    if (latestHealthData.ultrasonic.valid &&
        sonarUsable)
    {
        packet.validMask |=
            C3_VALID_ULTRASONIC;
    }

    if (calibration.isValid())
    {
        packet.validMask |=
            C3_VALID_CALIBRATION;
    }

    if (latestReservoirValid)
    {
        packet.validMask |=
            C3_VALID_RESERVOIR;
    }

    packet.crc16 =
        calculateCRC16(
            reinterpret_cast<const uint8_t *>(
                &packet),
            sizeof(C3BlePacket) -
                sizeof(packet.crc16));

    telemetryCharacteristic->setValue(
        reinterpret_cast<uint8_t *>(
            &packet),
        sizeof(packet));

    if (s3Connected)
    {
        // BLE transmission ALWAYS continues, even while
        // Serial output is paused for reservoir
        // setup/calibration.
        telemetryCharacteristic->notify();

        if (serialLiveOutputEnabled)
        {
            // Diagnostic presentation is queued; BLE delivery itself has
            // already completed and never waits for USB Serial output.
            pendingBleTxSequence =
                packet.sequence;

            pendingBleTxQualityPct =
                packet.qualityPct;

            pendingBleTxValidMask =
                packet.validMask;

            pendingBleTxCrc16 =
                packet.crc16;

            bleTxSerialReportPending =
                true;
        }
    }
}

// ============================================================
// SERIAL PRESENTATION CONTROL
//
// IMPORTANT:
// This only controls what is printed to the USB Serial Monitor.
// It never pauses sensors, OLED, BLE, S3 transmission,
// analytics, health, quality, or reservoir calculations.
// ============================================================

void pauseSerialLiveOutput()
{
    if (!serialLiveOutputEnabled)
    {
        return;
    }

    serialLiveOutputEnabled =
        false;

    // Do not emit an already-queued periodic report after the operator
    // explicitly paused live presentation.
    telemetrySerialReportPending =
        false;

    Serial.println();

    Serial.println(
        "========================================================");

    Serial.println(
        " LIVE TERMINAL OUTPUT : PAUSED");

    Serial.println(
        "========================================================");

    Serial.println(
        "Sensor acquisition, OLED and BLE -> S3 continue normally.");

    Serial.println(
        "Only periodic Serial Monitor status is hidden.");

    Serial.println(
        "When setup/calibration is finished, type STATUS and press Enter.");

    Serial.println(
        "========================================================");
}

void resumeSerialLiveOutput()
{
    serialLiveOutputEnabled =
        true;

    Serial.println();

    Serial.println(
        "========================================================");

    Serial.println(
        " LIVE TERMINAL OUTPUT : RESUMED");

    Serial.println(
        "========================================================");

    Serial.println(
        "Periodic C3 sensor/status output is visible again.");

    Serial.println(
        "========================================================");
}

// ============================================================
// RESERVOIR TERMINAL
// ============================================================

void printReservoirHelp()
{
    Serial.println();

    Serial.println(
        "========================================================");

    Serial.println(
        " HYDROSENSE RESERVOIR SETUP");

    Serial.println(
        "========================================================");

    Serial.println(
        "R = Configure reservoir type + physical dimensions");

    Serial.println(
        "F = Save current stable distance as FULL / 100%");

    Serial.println(
        "H = Save current stable distance as HALF / 50%");

    Serial.println(
        "E = Save current stable distance as EMPTY / 0%");

    Serial.println(
        "STATUS = Show reservoir status + RESUME live terminal output");

    Serial.println(
        "S      = Same as STATUS");

    Serial.println(
        "C      = Clear reservoir geometry + F/H/E calibration");

    Serial.println(
        "?      = Show this help");

    Serial.println();

    Serial.println(
        "All physical dimensions and ultrasonic distances are entered in cm.");

    Serial.println(
        "Type only the number, for example: 60 or 49.52");

    Serial.println(
        "During R setup, type X to cancel.");

    Serial.println(
        "R / F / H / E / C pause only periodic Serial output.");

    Serial.println(
        "Sensors, OLED and BLE -> S3 continue in real time.");

    Serial.println(
        "Type STATUS when finished to resume live terminal output.");

    Serial.println(
        "All accepted configuration values are stored in NVS.");

    Serial.println(
        "========================================================");
}

void printDistanceLine(
    const char *label,
    bool available,
    float value)
{
    Serial.print(label);

    if (available &&
        isfinite(value))
    {
        Serial.print(
            value,
            2);

        Serial.println(
            " cm");
    }
    else
    {
        Serial.println(
            "NOT SET");
    }
}

void printReservoirStatus()
{
    Serial.println();

    Serial.println(
        "[ RESERVOIR STATUS ]");

    Serial.print(
        "Type                : ");

    Serial.println(
        reservoirTypeLongText(
            reservoirProfile.type));

    if (reservoirProfile.type ==
        C3ReservoirType::RECTANGULAR)
    {
        Serial.print(
            "Length              : ");

        Serial.print(
            reservoirProfile.lengthCm,
            2);

        Serial.println(
            " cm");

        Serial.print(
            "Width               : ");

        Serial.print(
            reservoirProfile.widthCm,
            2);

        Serial.println(
            " cm");

        Serial.print(
            "Height              : ");

        Serial.print(
            reservoirProfile.heightCm,
            2);

        Serial.println(
            " cm");
    }
    else if (
        reservoirProfile.type ==
        C3ReservoirType::VERTICAL_CYLINDER)
    {
        Serial.print(
            "Diameter            : ");

        Serial.print(
            reservoirProfile.diameterCm,
            2);

        Serial.println(
            " cm");

        Serial.print(
            "Height              : ");

        Serial.print(
            reservoirProfile.heightCm,
            2);

        Serial.println(
            " cm");
    }

    Serial.print(
        "Geometry            : ");

    Serial.println(
        geometryConfigured()
            ? "VALID"
            : "NOT CONFIGURED");

    Serial.print(
        "Tank capacity       : ");

    const float capacity =
        reservoirCapacityL();

    if (isfinite(capacity))
    {
        Serial.print(
            capacity,
            2);

        Serial.println(
            " L");
    }
    else
    {
        Serial.println(
            "UNAVAILABLE");
    }

    Serial.print(
        "Calibrated FULL vol : ");

    const float fullVolume =
        calibratedFullVolumeL();

    if (isfinite(fullVolume))
    {
        Serial.print(
            fullVolume,
            2);

        Serial.println(
            " L");
    }
    else
    {
        Serial.println(
            "UNAVAILABLE");
    }

    if (calibration.isValid() &&
        geometryConfigured() &&
        !calibrationDepthFitsReservoirHeight())
    {
        Serial.println(
            "WARNING: FULL-to-EMPTY ultrasonic depth exceeds entered tank height.");

        Serial.println(
            "         Recheck tank height and F/E calibration; litres are disabled.");
    }

    printDistanceLine(
        "FULL distance       : ",
        calibration.hasFullDistance(),
        calibration.fullDistanceCm());

    printDistanceLine(
        "HALF distance       : ",
        reservoirProfile.halfAvailable,
        reservoirProfile.halfDistanceCm);

    printDistanceLine(
        "EMPTY distance      : ",
        calibration.hasEmptyDistance(),
        calibration.emptyDistanceCm());

    Serial.print(
        "F/E calibration     : ");

    Serial.println(
        calibration.isValid()
            ? "VALID"
            : "NOT VALID");

    Serial.print(
        "3-point calibration : ");

    Serial.println(
        halfCalibrationValid()
            ? "ACTIVE"
            : "NOT READY");

    Serial.print(
        "Current distance    : ");

    if (stableDistanceFresh(
            millis()))
    {
        Serial.print(
            latestStableDistanceCm,
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
        "Current level       : ");

    if (latestReservoirValid)
    {
        Serial.print(
            latestWaterLevelPct,
            2);

        Serial.println(
            " %");
    }
    else
    {
        Serial.println(
            "UNAVAILABLE");
    }

    Serial.print(
        "Current volume      : ");

    if (isfinite(
            latestVolumeL))
    {
        Serial.print(
            latestVolumeL,
            2);

        Serial.println(
            " L");
    }
    else
    {
        Serial.println(
            "UNAVAILABLE");
    }

    Serial.println();
}

bool parsePositiveFloat(
    const String &text,
    float &value)
{
    const char *raw =
        text.c_str();

    char *end =
        nullptr;

    const float parsed =
        strtof(
            raw,
            &end);

    if (end == raw)
    {
        return false;
    }

    while (*end != '\0')
    {
        if (!isspace(
                static_cast<unsigned char>(
                    *end)))
        {
            return false;
        }

        ++end;
    }

    if (!positiveFinite(
            parsed))
    {
        return false;
    }

    value = parsed;

    return true;
}

void cancelReservoirSetup()
{
    reservoirSetupState =
        ReservoirSetupState::IDLE;

    pendingReservoirType =
        C3ReservoirType::UNCONFIGURED;

    pendingLengthCm =
        NAN;

    pendingWidthCm =
        NAN;

    pendingHeightCm =
        NAN;

    pendingDiameterCm =
        NAN;

    Serial.println(
        "Reservoir geometry setup cancelled.");

    Serial.println(
        "Live terminal output remains PAUSED.");

    Serial.println(
        "Type STATUS and press Enter to resume live output.");
}

void startReservoirSetup()
{
    pauseSerialLiveOutput();

    pendingReservoirType =
        C3ReservoirType::UNCONFIGURED;

    pendingLengthCm =
        NAN;

    pendingWidthCm =
        NAN;

    pendingHeightCm =
        NAN;

    pendingDiameterCm =
        NAN;

    reservoirSetupState =
        ReservoirSetupState::WAIT_TYPE;

    Serial.println();

    Serial.println(
        "[ RESERVOIR GEOMETRY SETUP ]");

    Serial.println(
        "UNIT: centimetres (cm). Enter numbers only; do not type 'cm'.");

    Serial.println(
        "1 = RECTANGULAR TANK");

    Serial.println(
        "2 = VERTICAL CYLINDER / DRUM");

    Serial.println(
        "X = CANCEL");

    Serial.print(
        "Select type: ");
}

void finishGeometrySetup()
{
    reservoirProfile.type =
        pendingReservoirType;

    reservoirProfile.lengthCm =
        pendingLengthCm;

    reservoirProfile.widthCm =
        pendingWidthCm;

    reservoirProfile.heightCm =
        pendingHeightCm;

    reservoirProfile.diameterCm =
        pendingDiameterCm;

    if (saveReservoirGeometry())
    {
        Serial.println();

        Serial.println(
            "Reservoir geometry saved to NVS.");
    }
    else
    {
        Serial.println();

        Serial.println(
            "ERROR: Reservoir geometry could not be saved.");
    }

    reservoirSetupState =
        ReservoirSetupState::IDLE;

    buildDisplaySnapshot(
        millis());

    Serial.println();

    Serial.println(
        "Geometry setup complete.");

    Serial.println(
        "Live terminal output is still PAUSED.");

    Serial.println(
        "You may now record F, H and E calibration points.");

    Serial.println(
        "When completely finished, type STATUS and press Enter.");
}

void processReservoirSetupValue(
    const String &line)
{
    String valueText =
        line;

    valueText.trim();

    if (valueText.equalsIgnoreCase(
            "X"))
    {
        cancelReservoirSetup();
        return;
    }

    if (reservoirSetupState ==
        ReservoirSetupState::WAIT_TYPE)
    {
        if (valueText == "1")
        {
            pendingReservoirType =
                C3ReservoirType::RECTANGULAR;

            reservoirSetupState =
                ReservoirSetupState::WAIT_RECT_LENGTH;

            Serial.print(
                "Internal length (cm): ");

            return;
        }

        if (valueText == "2")
        {
            pendingReservoirType =
                C3ReservoirType::VERTICAL_CYLINDER;

            reservoirSetupState =
                ReservoirSetupState::WAIT_DRUM_DIAMETER;

            Serial.print(
                "Internal diameter (cm): ");

            return;
        }

        Serial.print(
            "Enter 1, 2, or X: ");

        return;
    }

    float value =
        NAN;

    if (!parsePositiveFloat(
            valueText,
            value))
    {
        Serial.print(
            "Enter a positive number, or X to cancel: ");

        return;
    }

    switch (reservoirSetupState)
    {
    case ReservoirSetupState::WAIT_RECT_LENGTH:
        pendingLengthCm =
            value;

        reservoirSetupState =
            ReservoirSetupState::WAIT_RECT_WIDTH;

        Serial.print(
            "Internal width (cm): ");

        break;

    case ReservoirSetupState::WAIT_RECT_WIDTH:
        pendingWidthCm =
            value;

        reservoirSetupState =
            ReservoirSetupState::WAIT_RECT_HEIGHT;

        Serial.print(
            "Internal height (cm): ");

        break;

    case ReservoirSetupState::WAIT_RECT_HEIGHT:
        pendingHeightCm =
            value;

        finishGeometrySetup();

        break;

    case ReservoirSetupState::WAIT_DRUM_DIAMETER:
        pendingDiameterCm =
            value;

        reservoirSetupState =
            ReservoirSetupState::WAIT_DRUM_HEIGHT;

        Serial.print(
            "Internal height (cm): ");

        break;

    case ReservoirSetupState::WAIT_DRUM_HEIGHT:
        pendingHeightCm =
            value;

        finishGeometrySetup();

        break;

    default:
        reservoirSetupState =
            ReservoirSetupState::IDLE;

        break;
    }
}

float currentCalibrationDistance()
{
    return stableDistanceFresh(
               millis())
               ? latestStableDistanceCm
               : NAN;
}

void handleIdleTerminalCommand(
    const String &line)
{
    if (line.length() == 0)
    {
        return;
    }

    String normalized =
        line;

    normalized.trim();
    normalized.toUpperCase();

    // STATUS is deliberately a full-word command because it is
    // the operator's explicit signal that terminal
    // setup/calibration work is finished and periodic live
    // Serial output may resume.
    if (normalized == "STATUS" ||
        normalized == "S")
    {
        resumeSerialLiveOutput();

        printReservoirStatus();

        return;
    }

    if (normalized == "PAUSE")
    {
        pauseSerialLiveOutput();

        return;
    }

    if (normalized == "R")
    {
        startReservoirSetup();

        return;
    }

    if (normalized == "?")
    {
        printReservoirHelp();

        return;
    }

    if (normalized == "C")
    {
        pauseSerialLiveOutput();

        const bool calibrationCleared =
            calibration.clear();

        bool profileCleared =
            false;

        if (reservoirProfileStorageReady)
        {
            profileCleared =
                reservoirProfilePreferences.clear();
        }

        resetReservoirProfileMemory();

        latestReservoirValid =
            false;

        latestWaterLevelPct =
            NAN;

        latestWaterDepthCm =
            NAN;

        latestUsableDepthCm =
            NAN;

        latestVolumeL =
            NAN;

        Serial.println();

        Serial.print(
            "Calibration clear    : ");

        Serial.println(
            calibrationCleared
                ? "OK"
                : "FAILED");

        Serial.print(
            "Geometry clear       : ");

        Serial.println(
            profileCleared
                ? "OK"
                : "FAILED");

        buildDisplaySnapshot(
            millis());

        Serial.println(
            "Live terminal output remains PAUSED.");

        Serial.println(
            "Continue setup, then type STATUS to resume.");

        return;
    }

    if (normalized == "F" ||
        normalized == "H" ||
        normalized == "E")
    {
        const char command =
            normalized[0];

        pauseSerialLiveOutput();

        const float distance =
            currentCalibrationDistance();

        if (!isfinite(distance))
        {
            Serial.println(
                "ERROR: No fresh stable ultrasonic distance is available.");

            return;
        }

        bool ok =
            false;

        if (command == 'F')
        {
            ok =
                calibration.setFullDistance(
                    distance);
        }
        else if (command == 'H')
        {
            ok =
                saveHalfDistance(
                    distance);
        }
        else
        {
            ok =
                calibration.setEmptyDistance(
                    distance);
        }

        Serial.println();

        Serial.print(
            command == 'F'
                ? "FULL"
            : command == 'H'
                ? "HALF"
                : "EMPTY");

        Serial.print(
            " distance saved: ");

        if (ok)
        {
            Serial.print(
                distance,
                2);

            Serial.println(
                " cm");
        }
        else
        {
            Serial.println(
                "FAILED");
        }

        if (stableDistanceFresh(
                millis()))
        {
            calculateReservoirFromDistance(
                latestStableDistanceCm);
        }

        buildDisplaySnapshot(
            millis());

        Serial.println(
            "Live terminal output remains PAUSED.");

        Serial.println(
            "Continue calibration as needed, then type STATUS to resume.");

        return;
    }

    Serial.print(
        "Unknown command: ");

    Serial.println(
        line);

    Serial.println(
        "Type ? and press Enter for help.");
}

void processTerminalLine(
    String line)
{
    line.trim();

    if (line.length() == 0)
    {
        return;
    }

    if (reservoirSetupState ==
        ReservoirSetupState::IDLE)
    {
        handleIdleTerminalCommand(
            line);
    }
    else
    {
        processReservoirSetupValue(
            line);
    }
}

void updateTerminal()
{
    // If one complete command is already waiting for its sonar-safe
    // execution window, leave subsequent bytes in the hardware/USB RX
    // buffer rather than executing multiple long commands back-to-back.
    if (terminalCommandPending)
    {
        return;
    }

    while (Serial.available() > 0)
    {
        const char c =
            static_cast<char>(
                Serial.read());

        if (c == '\r')
        {
            continue;
        }

        if (c == '\n')
        {
            if (terminalLine.length() > 0)
            {
                pendingTerminalLine =
                    terminalLine;

                terminalCommandPending =
                    true;
            }

            terminalLine = "";

            return;
        }

        if (terminalLine.length() < 48)
        {
            terminalLine += c;
        }
    }
}

bool servicePendingTerminalCommand(
    uint32_t nowMs)
{
    if (!terminalCommandPending ||
        !postSonarWorkWindowOpen(nowMs))
    {
        return false;
    }

    const String command =
        pendingTerminalLine;

    pendingTerminalLine = "";
    terminalCommandPending = false;

    processTerminalLine(
        command);

    return true;
}

// ============================================================
// SENSOR ACQUISITION
// ============================================================

void clearReservoirRuntime()
{
    latestReservoirValid = false;
    latestWaterLevelPct = NAN;
    latestWaterDepthCm = NAN;
    latestUsableDepthCm = NAN;
    latestVolumeL = NAN;
}

void clearStableSonarRuntime(
    bool resetFilter)
{
    haveStableDistance = false;
    latestStableDistanceCm = NAN;
    latestStableDistanceTimestampMs = 0;
    distanceHeld = false;

    clearReservoirRuntime();

    if (resetFilter)
    {
        // Do not blend measurements captured before a sustained
        // ultrasonic outage with measurements captured after recovery.
        distanceFilter.reset();
    }
}

// ------------------------------------------------------------
// Rebuild the public sonar health + reservoir state from the
// latest physical ping and the latest trusted filtered distance.
//
// A short run of misses may hold the last trusted distance. A
// sustained failure invalidates the held value and resets the
// rolling filter so recovery starts only from fresh echoes.
// ------------------------------------------------------------

void refreshSonarDerivedState(
    uint32_t nowMs)
{
    latestHealthData =
        healthManager.snapshot(
            nowMs,
            calibration.isValid(),
            latestReservoirValid);

    const bool sonarFresh =
        stableDistanceFresh(
            nowMs);

    const uint32_t consecutiveFailures =
        latestHealthData
            .ultrasonic
            .consecutiveFailures;

    const bool transientSonarHold =
        sonarFresh &&
        !currentUltrasonicPingValid &&
        consecutiveFailures <
            SONAR_FAILURES_BEFORE_INVALID;

    distanceHeld =
        transientSonarHold;

    if (transientSonarHold)
    {
        latestHealthData.ultrasonic.valid =
            true;

        latestHealthData.ultrasonic.state =
            SensorState::LIVE;
    }

    const bool sonarTrusted =
        sonarFresh &&
        (currentUltrasonicPingValid ||
         transientSonarHold);

    if (!sonarTrusted)
    {
        // Warm-up with valid pings is not a hardware fault, but the
        // stable distance is not yet publishable. A sustained miss
        // after a previously trusted value additionally clears the
        // historical filter window.
        const bool sustainedFailure =
            consecutiveFailures >=
            SONAR_FAILURES_BEFORE_INVALID;

        if (haveStableDistance &&
            (!sonarFresh || sustainedFailure))
        {
            clearStableSonarRuntime(
                true);
        }
        else
        {
            clearReservoirRuntime();
            distanceHeld = false;
        }

        latestHealthData.ultrasonic.valid =
            false;

        if (sustainedFailure &&
            latestHealthData.ultrasonic.state ==
                SensorState::LIVE)
        {
            latestHealthData.ultrasonic.state =
                SensorState::STALE;
        }
    }
    else
    {
        latestReservoirValid =
            calculateReservoirFromDistance(
                latestStableDistanceCm);
    }

    latestHealthData.calibrationValid =
        calibration.isValid();

    latestHealthData.reservoirValid =
        latestReservoirValid;

    latestHealthData.allCriticalSensorsValid =
        latestHealthData.dht22.valid &&
        latestHealthData.ds18b20.valid &&
        latestHealthData.ultrasonic.valid;

    latestHealthData.nodeHealthy =
        latestHealthData.allCriticalSensorsValid;
}

// ------------------------------------------------------------
// SUPER-REAL-TIME ANALYTICS + DATA QUALITY REFRESH
//
// The current sensor/physics/quality snapshot may be refreshed as
// often as physical state changes. Trend history is different: it
// must only advance on the deliberate analytics sampling cadence and
// only for a genuinely new source measurement.
//
// captureTrendSamples = false
//     Fast path. Refresh current values/environment/quality only.
//
// captureTrendSamples = true
//     Telemetry analytics path. Capture new trend samples, with the
//     analytics engine rejecting any already-consumed source timestamp.
//
// This separation prevents a retained last-good DHT22/DS18B20 value
// from being counted repeatedly as if it were a new measurement.
// ------------------------------------------------------------

void refreshAnalyticsAndQuality(
    uint32_t nowMs,
    bool captureTrendSamples)
{
    const bool dhtOK =
        latestHealthData.dht22.valid &&
        isfinite(airSensor.temperatureC()) &&
        isfinite(airSensor.humidityPct());

    const float airTemperatureC =
        dhtOK
            ? airSensor.temperatureC()
            : NAN;

    const float humidityPct =
        dhtOK
            ? airSensor.humidityPct()
            : NAN;

    const bool ds18OK =
        latestHealthData.ds18b20.valid &&
        isfinite(waterSensor.temperatureC());

    const float waterTemperatureC =
        ds18OK
            ? waterSensor.temperatureC()
            : NAN;

    const bool effectiveUltrasonicValid =
        latestHealthData.ultrasonic.valid &&
        stableDistanceFresh(nowMs);

    C3AnalyticsSourceTimestamps sourceTimestamps{};

    sourceTimestamps.dht22Ms =
        latestHealthData.dht22.lastValidTimestampMs;

    sourceTimestamps.ds18b20Ms =
        latestHealthData.ds18b20.lastValidTimestampMs;

    sourceTimestamps.reservoirMs =
        latestStableDistanceTimestampMs;

    analytics.update(
        nowMs,
        dhtOK,
        airTemperatureC,
        humidityPct,
        ds18OK,
        waterTemperatureC,
        effectiveUltrasonicValid,
        effectiveUltrasonicValid
            ? latestGoodEchoTimeUs
            : 0,
        effectiveUltrasonicValid
            ? latestGoodRawDistanceCm
            : NAN,
        effectiveUltrasonicValid
            ? latestGoodCorrectedDistanceCm
            : NAN,
        effectiveUltrasonicValid
            ? latestStableDistanceCm
            : NAN,
        calibration.isValid(),
        latestReservoirValid,
        latestReservoirValid
            ? latestWaterDepthCm
            : NAN,
        latestReservoirValid
            ? latestWaterLevelPct
            : NAN,
        latestReservoirValid
            ? latestUsableDepthCm
            : NAN,
        sourceTimestamps,
        captureTrendSamples);

    latestAnalyticsData =
        analytics.data();

    latestQualityData =
        dataQualityEngine.evaluate(
            latestAnalyticsData,
            latestHealthData);
}

// ------------------------------------------------------------
// Independent HC-SR04 service.
//
// This restores the clean first-testing-phase scheduling model:
// one genuine physical ping every ~250 ms, independent of the
// slower DHT22/DS18B20/BLE telemetry cycle.
// ------------------------------------------------------------

void performUltrasonicMeasurement(
    uint32_t schedulerTimeMs)
{
    currentUltrasonicPingValid =
        ultrasonic.read();

    uint32_t currentEchoTimeUs =
        0;

    float currentRawDistanceCm =
        NAN;

    float currentCorrectedDistanceCm =
        NAN;

    if (currentUltrasonicPingValid)
    {
        currentEchoTimeUs =
            ultrasonic.echoTimeUs();

        currentRawDistanceCm =
            ultrasonic.rawDistanceCm();

        // Temperature correction uses the latest genuine DHT22
        // temperature. If DHT22 is temporarily unavailable, the
        // genuine HC-SR04 echo is still usable at nominal sound speed.
        const float airTemperatureC =
            airSensor.temperatureC();

        if (latestHealthData.dht22.valid &&
            UltrasonicPhysics::validAirTemperature(
                airTemperatureC))
        {
            latestSpeedOfSoundMps =
                UltrasonicPhysics::speedOfSoundMps(
                    airTemperatureC);

            currentCorrectedDistanceCm =
                UltrasonicPhysics::correctedDistanceCm(
                    currentEchoTimeUs,
                    airTemperatureC);

            // A physics-corrected value outside the HC-SR04 range is
            // rejected by UltrasonicPhysics. Preserve the genuine raw
            // echo as the fallback rather than fabricating a value.
            if (!isfinite(
                    currentCorrectedDistanceCm))
            {
                currentCorrectedDistanceCm =
                    currentRawDistanceCm;
            }
        }
        else
        {
            latestSpeedOfSoundMps =
                NOMINAL_SOUND_SPEED_MPS;

            currentCorrectedDistanceCm =
                currentRawDistanceCm;
        }
    }
    else if (
        latestHealthData.dht22.valid &&
        UltrasonicPhysics::validAirTemperature(
            airSensor.temperatureC()))
    {
        latestSpeedOfSoundMps =
            UltrasonicPhysics::speedOfSoundMps(
                airSensor.temperatureC());
    }

    const uint32_t sampleTimeMs =
        millis();

    healthManager.updateUltrasonic(
        currentUltrasonicPingValid,
        sampleTimeMs);

    // Only genuine, finite physical measurements enter the rolling
    // window. Missed echoes never become zeroes or synthetic samples.
    if (currentUltrasonicPingValid &&
        isfinite(
            currentCorrectedDistanceCm))
    {
        distanceFilter.addSample(
            currentCorrectedDistanceCm);

        latestGoodEchoTimeUs =
            currentEchoTimeUs;

        latestGoodRawDistanceCm =
            currentRawDistanceCm;

        latestGoodCorrectedDistanceCm =
            currentCorrectedDistanceCm;

        if (distanceFilter.sampleCount() >=
            DISTANCE_MIN_SAMPLES)
        {
            const float candidateDistance =
                distanceFilter.trimmedMean();

            if (isfinite(
                    candidateDistance))
            {
                const bool firstStableValue =
                    !haveStableDistance ||
                    !isfinite(
                        latestStableDistanceCm);

                const bool meaningfulChange =
                    firstStableValue ||
                    fabsf(
                        candidateDistance -
                        latestStableDistanceCm) >=
                        DISTANCE_STABILITY_DEADBAND_CM;

                if (meaningfulChange)
                {
                    latestStableDistanceCm =
                        candidateDistance;
                }

                // Even when the candidate stays inside the deadband,
                // this is still a new genuine filtered measurement.
                // Refresh freshness without creating display chatter.
                latestStableDistanceTimestampMs =
                    sampleTimeMs;

                haveStableDistance =
                    true;
            }
        }
    }

    refreshSonarDerivedState(
        sampleTimeMs);

    refreshAnalyticsAndQuality(
        sampleTimeMs,
        false);

    // Local OLED gets the latest sonar/reservoir state immediately;
    // BLE publication remains on the existing telemetry cadence.
    buildDisplaySnapshot(
        sampleTimeMs);

    // Keep scheduler cadence anchored to the acquisition start, while
    // background-work windows are measured from actual completion.
    lastSonarCompletedMs =
        millis();

    lastSonarMeasurementMs =
        schedulerTimeMs;
}

// ------------------------------------------------------------
// Independent DHT22 service.
//
// The first confirmed testing implementation sampled the DHT22
// every 2 seconds.  Keep that physically appropriate cadence and
// do not tie the sensor to BLE publication.
// ------------------------------------------------------------

void performDHTMeasurement()
{
    const bool dhtOK =
        airSensor.read();

    const uint32_t sampleTimeMs =
        millis();

    dhtAttemptSeen =
        true;

    healthManager.updateDHT22(
        dhtOK,
        sampleTimeMs);

    // Rebuild the unified health snapshot immediately.  Sonar and
    // reservoir state are preserved; only the newest DHT health is
    // incorporated here.
    refreshSonarDerivedState(
        sampleTimeMs);

    refreshAnalyticsAndQuality(
        sampleTimeMs,
        false);

    buildDisplaySnapshot(
        sampleTimeMs);

    // Anchor the next DHT22 attempt to the COMPLETION of this genuine
    // transaction. Adafruit DHT 1.4.7 internally returns its cached
    // previous result when called <2000 ms after the previous physical
    // transaction. Completion-based scheduling therefore guarantees the
    // next successful wrapper call represents a new physical acquisition,
    // never a library-cache replay stamped as a new sample.
    lastDHTMeasurementMs =
        sampleTimeMs;
}

// ------------------------------------------------------------
// Independent asynchronous DS18B20 service.
//
// 12-bit precision is retained, but the ~750-ms conversion no longer
// blocks loop().  A conversion is requested every 2 seconds and read
// only after the physical sensor conversion time has elapsed.
// ------------------------------------------------------------

void serviceDS18B20(
    uint32_t nowMs,
    bool workWindowSafe)
{
    // First collect a conversion that has finished in the background.
    if (workWindowSafe &&
        waterSensor.conversionPending() &&
        waterSensor.conversionReady(nowMs))
    {
        const bool ds18OK =
            waterSensor.readConversion(
                nowMs);

        const uint32_t sampleTimeMs =
            millis();

        ds18AttemptSeen =
            true;

        healthManager.updateDS18B20(
            ds18OK,
            sampleTimeMs);

        refreshSonarDerivedState(
            sampleTimeMs);

        refreshAnalyticsAndQuality(
            sampleTimeMs,
            false);

        buildDisplaySnapshot(
            sampleTimeMs);

        nowMs =
            sampleTimeMs;
    }

    // Then start the next independent conversion when due.
    const bool requestDue =
        workWindowSafe &&
        !waterSensor.conversionPending() &&
        (firstDS18Request ||
         (nowMs - lastDS18RequestMs >=
          DS18_REQUEST_INTERVAL_MS));

    if (!requestDue)
    {
        return;
    }

    firstDS18Request =
        false;

    const bool requestStarted =
        waterSensor.requestConversion();

    const uint32_t requestAttemptCompletedMs =
        millis();

    if (requestStarted)
    {
        // Preserve a true 2-second physical conversion-start cadence by
        // anchoring the scheduler to the timestamp captured immediately
        // after DallasTemperature actually issued START CONVERSION.
        lastDS18RequestMs =
            waterSensor.conversionStartedAtMs();

        return;
    }

    // A failed addressed request / rediscovery (for example no probe
    // present or an ambiguous multi-probe bus) is a real sensor failure.
    // Timestamp it after the attempt so a bounded recovery scan is not
    // accidentally retried early.
    lastDS18RequestMs =
        requestAttemptCompletedMs;

    ds18AttemptSeen =
        true;

    healthManager.updateDS18B20(
        false,
        requestAttemptCompletedMs);

    refreshSonarDerivedState(
        requestAttemptCompletedMs);

    refreshAnalyticsAndQuality(
        requestAttemptCompletedMs,
        false);

    buildDisplaySnapshot(
        requestAttemptCompletedMs);
}

// ------------------------------------------------------------
// Deferred periodic Serial report.
//
// This is presentation only. It is intentionally separated from the
// 3-second analytics/BLE transaction so a slow or busy host terminal
// cannot delay packet delivery. It is serviced only just after sonar.
// ------------------------------------------------------------

void printCurrentTelemetrySummary(
    uint32_t reportTimeMs)
{
    const bool dhtOK =
        latestHealthData.dht22.valid &&
        isfinite(airSensor.temperatureC()) &&
        isfinite(airSensor.humidityPct());

    const bool ds18OK =
        latestHealthData.ds18b20.valid &&
        isfinite(waterSensor.temperatureC());

    const bool sonarFresh =
        stableDistanceFresh(reportTimeMs);

    Serial.println();
    Serial.println("========================================================");
    Serial.print("HYDROSENSE C3 MEASUREMENT #");
    Serial.println(measurementNumber);
    Serial.println("========================================================");

    Serial.print("Air Temperature      : ");
    if (dhtOK)
    {
        Serial.print(airSensor.temperatureC(), 2);
        Serial.println(" C");
    }
    else
    {
        Serial.println("UNAVAILABLE");
    }

    Serial.print("Humidity             : ");
    if (dhtOK)
    {
        Serial.print(airSensor.humidityPct(), 2);
        Serial.println(" %");
    }
    else
    {
        Serial.println("UNAVAILABLE");
    }

    Serial.print("Water Temperature    : ");
    if (ds18OK)
    {
        Serial.print(waterSensor.temperatureC(), 2);
        Serial.println(" C");
    }
    else
    {
        Serial.println("UNAVAILABLE");
    }

    Serial.print("DS18B20 conversion   : ");
    Serial.print(waterSensor.resolutionBits());
    Serial.print("-bit async / ");
    Serial.print(waterSensor.conversionTimeMs());
    Serial.println(" ms");

    Serial.print("HC-SR04 latest ping  : ");
    Serial.println(currentUltrasonicPingValid ? "VALID" : "MISSED");

    Serial.print("Stable Distance      : ");
    if (sonarFresh)
    {
        Serial.print(latestStableDistanceCm, 2);
        Serial.print(" cm");

        if (distanceHeld)
        {
            Serial.print("  [HELD LAST GENUINE, age ");
            Serial.print(stableDistanceAgeMs(reportTimeMs));
            Serial.print(" ms]");
        }

        Serial.println();
    }
    else
    {
        Serial.println("UNAVAILABLE / WARMING");
    }

    Serial.print("Sonar Window         : ");
    Serial.print(distanceFilter.sampleCount());
    Serial.print("/");
    Serial.println(distanceFilter.windowSize());

    Serial.print("Sonar Failures       : ");
    Serial.println(latestHealthData.ultrasonic.consecutiveFailures);

    Serial.print("Reservoir Level      : ");
    if (latestReservoirValid)
    {
        Serial.print(latestWaterLevelPct, 2);
        Serial.println(" %");
    }
    else
    {
        Serial.println("UNAVAILABLE");
    }

    Serial.print("Reservoir Volume     : ");
    if (isfinite(latestVolumeL))
    {
        Serial.print(latestVolumeL, 2);
        Serial.println(" L");
    }
    else
    {
        Serial.println("UNAVAILABLE");
    }

    Serial.print("Quality Score        : ");
    Serial.print(latestQualityData.scorePct, 1);
    Serial.println(" %");
    Serial.println("========================================================");
}

void serviceDeferredSerialPresentation(
    uint32_t nowMs)
{
    if (!postSonarWorkWindowOpen(nowMs))
    {
        return;
    }

    // A 30-ms post-sonar window spans multiple fast loop iterations.
    // Do not accidentally start several Serial jobs in that same window.
    if (serialPresentationSonarCompletionMs ==
        lastSonarCompletedMs)
    {
        return;
    }

    // Operator commands have priority over periodic presentation.
    // Run at most one diagnostic/presentation job per sonar period.
    if (servicePendingTerminalCommand(nowMs))
    {
        serialPresentationSonarCompletionMs =
            lastSonarCompletedMs;
        return;
    }

    if (serialLiveOutputEnabled)
    {
        if (bleDisconnectedSerialReportPending)
        {
            bleDisconnectedSerialReportPending = false;
            serialPresentationSonarCompletionMs = lastSonarCompletedMs;
            Serial.println("[BLE] ESP32-S3 DISCONNECTED");
            return;
        }

        if (bleConnectedSerialReportPending)
        {
            bleConnectedSerialReportPending = false;
            serialPresentationSonarCompletionMs = lastSonarCompletedMs;
            Serial.println("[BLE] ESP32-S3 CONNECTED");
            return;
        }

        if (bleAdvertisingSerialReportPending)
        {
            bleAdvertisingSerialReportPending = false;
            serialPresentationSonarCompletionMs = lastSonarCompletedMs;
            Serial.println("[BLE] Advertising restarted");
            return;
        }

        if (buttonSerialReportPending)
        {
            buttonSerialReportPending = false;
            serialPresentationSonarCompletionMs = lastSonarCompletedMs;

            Serial.print("[UI] Button press #");
            Serial.print(pendingButtonPressCount);
            Serial.print(" -> OLED page ");
            Serial.println(pendingButtonPageNumber);
            return;
        }

        if (bleTxSerialReportPending)
        {
            bleTxSerialReportPending = false;
            serialPresentationSonarCompletionMs = lastSonarCompletedMs;

            Serial.print("[BLE TX] SENSOR PACKET | seq=");
            Serial.print(pendingBleTxSequence);
            Serial.print(" | bytes=");
            Serial.print(sizeof(C3BlePacket));
            Serial.print(" | quality=");
            Serial.print(pendingBleTxQualityPct);
            Serial.print("% | validMask=0x");
            Serial.print(pendingBleTxValidMask, HEX);
            Serial.print(" | CRC=0x");
            Serial.println(pendingBleTxCrc16, HEX);
            return;
        }
    }

    if (!telemetrySerialReportPending ||
        !serialLiveOutputEnabled)
    {
        return;
    }

    telemetrySerialReportPending =
        false;

    serialPresentationSonarCompletionMs =
        lastSonarCompletedMs;

    printCurrentTelemetrySummary(
        nowMs);
}

// ------------------------------------------------------------
// Analytics / quality / BLE publication cycle.
//
// No physical DHT22 or DS18B20 conversion is started here.  This
// stage consumes the latest genuinely acquired sensor states produced
// by their independent schedulers above, plus the trusted Block-4
// ultrasonic state.
// ------------------------------------------------------------

void performTelemetryMeasurement(
    uint32_t schedulerTimeMs)
{
    ++measurementNumber;

    const uint32_t sampleTimeMs =
        millis();

    // Ensure health/hold/staleness reflects the exact publication time.
    refreshSonarDerivedState(
        sampleTimeMs);

    // Capture the deliberate 3-second trend sample here.  Current
    // analytics/quality are already refreshed on every sensor event,
    // but temporal history advances only on this publication cadence
    // and only when a genuinely new source timestamp exists.
    refreshAnalyticsAndQuality(
        sampleTimeMs,
        true);

    // --------------------------------------------------------
    // Local OLED snapshot
    // --------------------------------------------------------

    buildDisplaySnapshot(
        sampleTimeMs);

    // --------------------------------------------------------
    // BLE - exact existing 20-byte protocol.
    //
    // Publish the completed real sensor snapshot BEFORE verbose
    // Serial presentation. This preserves the proven low-latency
    // communication behavior from the earlier realtime-fixed build.
    // --------------------------------------------------------

    sendSensorPacket(
        sampleTimeMs);

    // --------------------------------------------------------
    // Professional serial summary is deferred to the first short
    // post-sonar window. BLE has already received this snapshot.
    // --------------------------------------------------------

    if (serialLiveOutputEnabled)
    {
        telemetrySerialReportPending =
            true;
    }

    lastTelemetryMs =
        schedulerTimeMs;

    firstTelemetryMeasurement =
        false;
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(
        SERIAL_BAUD);

    // Short USB-CDC settle only. Sensor-specific startup timing is
    // handled independently, so the complete C3 never sleeps for the
    // old multi-second presentation delay.
    delay(
        100);

    Serial.println();
    Serial.println(
        "========================================================");
    Serial.println(
        "             HYDROSENSE ESP32-C3");
    Serial.println(
        " ROBUST SENSOR + RESERVOIR + OLED + BLE NODE");
    Serial.println(
        "========================================================");

    // Core runtime state is clean before any physical acquisition starts.
    analytics.reset();
    healthManager.reset();

    // Physical button input is independent and cheap to initialize.
    button.begin();

    // --------------------------------------------------------
    // SENSOR HARDWARE FIRST
    //
    // This restores the clean first-testing-phase startup principle:
    // start real sensors before presentation/network work. DHT22 warms
    // up non-blockingly and the first 12-bit DS18B20 conversion runs in
    // the background while OLED/NVS/BLE are initialized.
    // --------------------------------------------------------

    Serial.println(
        "Initializing DHT22...");

    airSensor.begin();

    dhtInitializedAtMs =
        millis();

    Serial.println(
        "Initializing DS18B20...");

    waterSensor.begin();

    Serial.print(
        "DS18B20 Devices      : ");
    Serial.println(
        waterSensor.deviceCount());

    Serial.print(
        "DS18B20 Resolution   : ");
    Serial.print(
        waterSensor.resolutionBits());
    Serial.println(
        " bit");

    Serial.print(
        "DS18B20 Conversion   : async / ");
    Serial.print(
        waterSensor.conversionTimeMs());
    Serial.println(
        " ms");

    // If startup discovery found exactly one configured DS18B20, start
    // its first 12-bit conversion immediately so it runs in parallel with
    // the remaining setup. If no valid unique probe exists, do not perform
    // a second blocking discovery here; the real-time scheduler will retry
    // from the first post-sonar safe window.
    if (waterSensor.deviceCount() == 1 &&
        waterSensor.requestConversion())
    {
        firstDS18Request =
            false;

        lastDS18RequestMs =
            waterSensor.conversionStartedAtMs();
    }

    Serial.println(
        "Initializing HC-SR04...");

    ultrasonic.begin();
    distanceFilter.reset();

    Serial.print(
        "Distance Filter      : robust trimmed-mean window ");
    Serial.print(
        distanceFilter.windowSize());
    Serial.print(
        "; usable after ");
    Serial.print(
        DISTANCE_MIN_SAMPLES);
    Serial.println(
        " genuine samples");

    Serial.print(
        "HC-SR04 cadence      : ");
    Serial.print(
        SONAR_INTERVAL_MS);
    Serial.println(
        " ms per real ping");

    Serial.print(
        "DHT22 cadence        : ");
    Serial.print(
        DHT_INTERVAL_MS);
    Serial.println(
        " ms");

    Serial.print(
        "DHT22 startup warm-up: ");
    Serial.print(
        DHT_STARTUP_WARMUP_MS);
    Serial.println(
        " ms non-blocking");

    Serial.print(
        "DS18B20 request      : ");
    Serial.print(
        DS18_REQUEST_INTERVAL_MS);
    Serial.println(
        " ms");

    Serial.print(
        "Telemetry cadence    : ");
    Serial.print(
        TELEMETRY_INTERVAL_MS);
    Serial.println(
        " ms");

    // --------------------------------------------------------
    // LOCAL UI
    // --------------------------------------------------------

    Serial.println(
        "Initializing SH1106 OLED...");

    const bool oledOK =
        oled.begin();

    Serial.print(
        "OLED                 : ");
    Serial.println(
        oledOK
            ? "READY"
            : "NOT DETECTED");

    if (oledOK)
    {
        displayController.begin(
            millis());
    }

    // --------------------------------------------------------
    // PERSISTENT RESERVOIR CONFIGURATION
    // --------------------------------------------------------

    const bool calibrationStorageOK =
        calibration.begin();

    Serial.print(
        "Calibration NVS      : ");
    Serial.println(
        calibrationStorageOK
            ? "READY"
            : "FAILED");

    const bool profileStorageOK =
        loadReservoirProfile();

    Serial.print(
        "Reservoir Profile NVS: ");
    Serial.println(
        profileStorageOK
            ? "READY"
            : "FAILED");

    // --------------------------------------------------------
    // BLE
    // --------------------------------------------------------

    if (!initializeBLE())
    {
        Serial.println(
            "BLE INITIALIZATION FAILED");
    }

    buildDisplaySnapshot(
        millis());

    printReservoirStatus();
    printReservoirHelp();

    Serial.println(
        "C3 node ready. Waiting for ESP32-S3 BLE gateway...");
    Serial.println(
        "Press the GPIO3 button to change OLED page.");
    Serial.println(
        "========================================================");

    const uint32_t schedulerStartMs =
        millis();

    firstDHTMeasurement =
        true;

    lastDHTMeasurementMs =
        schedulerStartMs;

    // Keep the already-started initial DS18B20 conversion intact.
    if (firstDS18Request)
    {
        lastDS18RequestMs =
            schedulerStartMs;
    }

    firstTelemetryMeasurement =
        true;

    lastTelemetryMs =
        schedulerStartMs;

    firstSonarMeasurement =
        true;

    lastSonarMeasurementMs =
        schedulerStartMs;

    lastSonarCompletedMs =
        schedulerStartMs;
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    uint32_t nowMs =
        millis();

    // --------------------------------------------------------
    // HC-SR04 - highest-frequency physical deadline.
    //
    // Service sonar before the slower environmental sensors whenever
    // deadlines coincide. Temperature correction safely uses the latest
    // still-valid DHT22 reading and receives the newest reading on the
    // following ping. This minimizes 250-ms acquisition jitter.
    // --------------------------------------------------------

    const bool sonarCycleDue =
        firstSonarMeasurement ||
        (nowMs - lastSonarMeasurementMs >=
         SONAR_INTERVAL_MS);

    if (sonarCycleDue)
    {
        firstSonarMeasurement =
            false;

        performUltrasonicMeasurement(
            nowMs);

        nowMs =
            millis();
    }

    // Collect Serial input only. Completed commands are executed later
    // in a post-sonar window so terminal output cannot pre-empt sensing.
    updateTerminal();

    // Physical page button.
    button.update(
        nowMs);

    if (button.consumePress())
    {
        if (oled.isHealthy())
        {
            displayController.nextPage(
                nowMs);
        }

        if (serialLiveOutputEnabled)
        {
            pendingButtonPressCount =
                button.pressCount();

            pendingButtonPageNumber =
                displayController.pageNumber();

            buttonSerialReportPending =
                true;
        }
    }

    // --------------------------------------------------------
    // DHT22 - independent 2-second physical acquisition.
    // Power-up stabilization is non-blocking.
    // --------------------------------------------------------

    const bool dhtWarmupComplete =
        (nowMs - dhtInitializedAtMs) >=
        DHT_STARTUP_WARMUP_MS;

    const bool dhtCycleDue =
        dhtWarmupComplete &&
        (firstDHTMeasurement ||
         (nowMs - lastDHTMeasurementMs >=
          DHT_INTERVAL_MS));

    // DHT22 uses a timing-critical single-wire transaction. The normal
    // successful transaction is only a few milliseconds, but a malformed
    // pulse train can consume longer bounded timeout paths inside the DHT
    // library. Start it only immediately after a completed sonar ping so
    // the 250-ms HC-SR04 deadline always retains priority.
    const bool dhtWindowSafe =
        postSonarWorkWindowOpen(
            nowMs);

    if (dhtCycleDue &&
        dhtWindowSafe)
    {
        firstDHTMeasurement =
            false;

        performDHTMeasurement();

        nowMs =
            millis();
    }

    // --------------------------------------------------------
    // DS18B20 - asynchronous background conversion service.
    // No 750-ms loop blocking occurs here.
    // --------------------------------------------------------

    // All OneWire bus transactions (completed-read and any bounded
    // rediscovery/reconfiguration) are launched only after a sonar ping.
    // The 12-bit conversion itself continues independently in hardware.
    serviceDS18B20(
        nowMs,
        postSonarWorkWindowOpen(
            nowMs));

    nowMs =
        millis();

    // --------------------------------------------------------
    // BLE connection/reconnect service.
    //
    // Callback work is deferred here. Advertising restart is
    // non-blocking and aligned to the post-sonar safe window.
    // --------------------------------------------------------

    serviceBLEConnectionEvents(
        nowMs);

    nowMs =
        millis();

    // --------------------------------------------------------
    // Analytics / quality / BLE publication.
    //
    // First publication waits for both environmental sensor families
    // to produce an actual attempt. DS18B20 converts asynchronously and
    // DHT22 is first read after its non-blocking 2-second power-up warm-up.
    // A 3-second fallback prevents startup from waiting forever if a
    // sensor is physically absent.
    // --------------------------------------------------------

    const bool firstTelemetryReady =
        firstTelemetryMeasurement &&
        ((dhtAttemptSeen && ds18AttemptSeen) ||
         (nowMs - lastTelemetryMs >=
          TELEMETRY_INTERVAL_MS));

    const bool telemetryCycleDue =
        firstTelemetryReady ||
        (!firstTelemetryMeasurement &&
         (nowMs - lastTelemetryMs >=
          TELEMETRY_INTERVAL_MS));

    // Analytics/BLE publication is important but not a physical sensor
    // deadline. Align it to the post-sonar safe window so packet building,
    // CRC, characteristic update and notify can never start immediately
    // before the next 250-ms HC-SR04 acquisition.
    if (telemetryCycleDue &&
        postSonarWorkWindowOpen(nowMs))
    {
        performTelemetryMeasurement(
            nowMs);

        nowMs =
            millis();
    }

    // Safety net for a held value expiring between scheduled sonar
    // services. Normally performUltrasonicMeasurement() handles this.
    if (haveStableDistance &&
        !stableDistanceFresh(nowMs))
    {
        clearStableSonarRuntime(
            true);

        refreshSonarDerivedState(
            nowMs);

        refreshAnalyticsAndQuality(
            nowMs,
            false);

        buildDisplaySnapshot(
            nowMs);
    }

    // --------------------------------------------------------
    // Deferred Serial presentation / reservoir commands.
    // Potentially long output begins only just after sonar.
    // --------------------------------------------------------

    serviceDeferredSerialPresentation(
        nowMs);

    nowMs =
        millis();

    // --------------------------------------------------------
    // OLED service — sensor timing has priority.
    //
    // The display is updated only for a new real snapshot (or a forced
    // page change), and a frame is never started inside the guard
    // window immediately before the next 250-ms HC-SR04 acquisition.
    // If deferred, it is rendered just after that sonar service.
    // --------------------------------------------------------

    bool oledWindowSafe =
        true;

    if (!firstSonarMeasurement)
    {
        const uint32_t sonarElapsedMs =
            nowMs -
            lastSonarMeasurementMs;

        if (sonarElapsedMs >=
            SONAR_INTERVAL_MS)
        {
            oledWindowSafe =
                false;
        }
        else
        {
            const uint32_t untilNextSonarMs =
                SONAR_INTERVAL_MS -
                sonarElapsedMs;

            oledWindowSafe =
                untilNextSonarMs >
                OLED_SONAR_GUARD_MS;
        }
    }

    if (oled.isHealthy() &&
        oledWindowSafe)
    {
        displayController.update(
            latestDisplayData,
            nowMs);
    }

    delay(2);
}
