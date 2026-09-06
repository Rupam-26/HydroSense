const mongoose =
    require(
        "mongoose"
    );

function finiteNumberValidator(
    value
)
{
    return (
        typeof value ===
            "number" &&
        Number.isFinite(
            value
        )
    );
}

const requiredFiniteNumber =
{
    type:
        Number,

    required:
        true,

    validate:
    {
        validator:
            finiteNumberValidator,

        message:
            "{PATH} must be a finite number"
    }
};

// ============================================================
// REAL SENSOR DATA
// ============================================================

const measuredSchema =
    new mongoose.Schema(
        {
            airTemperatureC:
                requiredFiniteNumber,

            humidityPct:
            {
                ...requiredFiniteNumber,

                min:
                    0,

                max:
                    100
            },

            waterTemperatureC:
                requiredFiniteNumber,

            stableDistanceCm:
                requiredFiniteNumber,

            waterLevelPct:
            {
                ...requiredFiniteNumber,

                min:
                    0,

                max:
                    100
            }
        },
        {
            _id:
                false
        }
    );

// ============================================================
// DERIVED PHYSICS
// ============================================================

const derivedSchema =
    new mongoose.Schema(
        {
            dewPointC:
                requiredFiniteNumber,

            saturationVaporPressureKPa:
                requiredFiniteNumber,

            actualVaporPressureKPa:
                requiredFiniteNumber,

            vpdKPa:
                requiredFiniteNumber,

            absoluteHumidityGm3:
                requiredFiniteNumber,

            airWaterDeltaC:
                requiredFiniteNumber,

            absoluteAirWaterDeltaC:
                requiredFiniteNumber
        },
        {
            _id:
                false
        }
    );

// ============================================================
// EDGE AI
// ============================================================

const estimatedSchema =
    new mongoose.Schema(
        {
            tdsPpm:
                requiredFiniteNumber,

            model:
            {
                type:
                    String,

                required:
                    true,

                trim:
                    true
            },

            status:
            {
                type:
                    String,

                required:
                    true,

                trim:
                    true
            },

            inferenceMicros:
            {
                type:
                    Number,

                required:
                    true,

                min:
                    0
            }
        },
        {
            _id:
                false
        }
    );

// ============================================================
// QUALITY / COMMUNICATION
// ============================================================

const qualitySchema =
    new mongoose.Schema(
        {
            scorePct:
            {
                ...requiredFiniteNumber,

                min:
                    0,

                max:
                    100
            },

            flags:
            {
                type:
                    Number,

                required:
                    true,

                min:
                    0
            },

            crcFailures:
            {
                type:
                    Number,

                default:
                    0,

                min:
                    0
            },

            wrongLengths:
            {
                type:
                    Number,

                default:
                    0,

                min:
                    0
            },

            badHeaders:
            {
                type:
                    Number,

                default:
                    0,

                min:
                    0
            },

            badVersions:
            {
                type:
                    Number,

                default:
                    0,

                min:
                    0
            },

            queueDrops:
            {
                type:
                    Number,

                default:
                    0,

                min:
                    0
            }
        },
        {
            _id:
                false
        }
    );

// ============================================================
// COMPLETE HISTORICAL DOCUMENT
// ============================================================

const telemetrySchema =
    new mongoose.Schema(
        {
            deviceId:
            {
                type:
                    String,

                required:
                    true,

                trim:
                    true,

                index:
                    true
            },

            sequence:
            {
                type:
                    Number,

                required:
                    true,

                min:
                    0,

                max:
                    255
            },

            measured:
            {
                type:
                    measuredSchema,

                required:
                    true
            },

            derived:
            {
                type:
                    derivedSchema,

                required:
                    true
            },

            estimated:
            {
                type:
                    estimatedSchema,

                required:
                    true
            },

            quality:
            {
                type:
                    qualitySchema,

                required:
                    true
            },

            packetAgeMs:
            {
                type:
                    Number,

                required:
                    true,

                min:
                    0
            },

            bleConnected:
            {
                type:
                    Boolean,

                required:
                    true
            },

            wifiConnected:
            {
                type:
                    Boolean,

                required:
                    true
            },

            timestamp:
            {
                type:
                    Date,

                default:
                    Date.now,

                index:
                    true
            }
        },
        {
            versionKey:
                false,

            strict:
                true
        }
    );

telemetrySchema.index(
    {
        deviceId:
            1,

        timestamp:
            -1
    }
);

telemetrySchema.index(
    {
        timestamp:
            -1
    }
);

module.exports =
    mongoose.model(
        "Telemetry",
        telemetrySchema
    );