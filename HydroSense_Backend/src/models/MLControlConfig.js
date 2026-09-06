const mongoose = require("mongoose");

const mlControlConfigSchema = new mongoose.Schema(
    {
        deviceId: {
            type: String,
            required: true,
            trim: true,
            unique: true,
            index: true
        },

        reservoir: {
            shape: {
                type: String,
                enum: [
                    "UNCONFIGURED",
                    "RECTANGULAR",
                    "VERTICAL_CYLINDER",
                    "DEPTH_ONLY"
                ],
                default: "UNCONFIGURED"
            },

            heightCm: {
                type: Number,
                default: null
            },

            lengthCm: {
                type: Number,
                default: null
            },

            widthCm: {
                type: Number,
                default: null
            },

            diameterCm: {
                type: Number,
                default: null
            },

            fullDistanceCm: {
                type: Number,
                default: null
            },

            emptyDistanceCm: {
                type: Number,
                default: null
            }
        },

        baseline: {
            targetDurationHours: {
                type: Number,
                default: null
            },

            minimumCoveragePct: {
                type: Number,
                default: null
            },

            maximumGapMultiplier: {
                type: Number,
                default: null
            },

            robustZThreshold: {
                type: Number,
                default: null
            },

            sustainedStrangeSamples: {
                type: Number,
                default: null
            },

            missingWarningSeconds: {
                type: Number,
                default: null
            },

            unchangedWarningSeconds: {
                type: Number,
                default: null
            },

            minimumCalibrationSamplesPerClass: {
                type: Number,
                default: null
            },

            features: {
                type: [String],

                default: [
                    "airTemperatureC",
                    "humidityPct",
                    "waterTemperatureC",
                    "stableDistanceCm",
                    "waterLevelPct"
                ]
            }
        },

        isolationForest: {
            nEstimators: {
                type: Number,
                default: null
            },

            trainFraction: {
                type: Number,
                default: null
            },

            randomState: {
                type: Number,
                default: null
            }
        }
    },
    {
        timestamps: true,
        versionKey: false,
        strict: true
    }
);

module.exports =
    mongoose.model(
        "MLControlConfig",
        mlControlConfigSchema
    );