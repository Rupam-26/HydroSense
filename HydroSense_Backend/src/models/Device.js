const mongoose =
    require(
        "mongoose"
    );

const healthSchema =
    new mongoose.Schema(
        {
            packetValid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            dht22Valid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            ds18b20Valid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            ultrasonicValid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            reservoirValid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            calibrationValid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            environmentalFeaturesValid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            thermalFeaturesValid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            tdsValid:
            {
                type:
                    Boolean,

                default:
                    null
            },

            bleConnected:
            {
                type:
                    Boolean,

                default:
                    null
            },

            wifiConnected:
            {
                type:
                    Boolean,

                default:
                    null
            }
        },
        {
            _id:
                false
        }
    );

const deviceSchema =
    new mongoose.Schema(
        {
            deviceId:
            {
                type:
                    String,

                required:
                    true,

                unique:
                    true,

                trim:
                    true,

                index:
                    true
            },

            displayName:
            {
                type:
                    String,

                default:
                    null,

                trim:
                    true
            },

            locationLabel:
            {
                type:
                    String,

                default:
                    null,

                trim:
                    true
            },

            status:
            {
                type:
                    String,

                enum:
                [
                    "ONLINE",
                    "DEGRADED",
                    "OFFLINE",
                    "UNKNOWN"
                ],

                default:
                    "UNKNOWN",

                index:
                    true
            },

            lastSeenAt:
            {
                type:
                    Date,

                default:
                    null,

                index:
                    true
            },

            lastRealtimeAt:
            {
                type:
                    Date,

                default:
                    null
            },

            lastHistoricalAt:
            {
                type:
                    Date,

                default:
                    null
            },

            lastSequence:
            {
                type:
                    Number,

                default:
                    null
            },

            lastPacketAgeMs:
            {
                type:
                    Number,

                default:
                    null
            },

            lastQualityScorePct:
            {
                type:
                    Number,

                default:
                    null
            },

            health:
            {
                type:
                    healthSchema,

                default:
                    () => ({})
            },

            metadata:
            {
                firmwareVersion:
                {
                    type:
                        String,

                    default:
                        null
                },

                notes:
                {
                    type:
                        String,

                    default:
                        null
                }
            }
        },
        {
            timestamps:
                true,

            versionKey:
                false,

            strict:
                true
        }
    );

deviceSchema.index(
    {
        status:
            1,

        lastSeenAt:
            -1
    }
);

module.exports =
    mongoose.model(
        "Device",
        deviceSchema
    );