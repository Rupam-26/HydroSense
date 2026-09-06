// SPDX-FileCopyrightText: 2026 Rupam Mondal
// SPDX-License-Identifier: MIT

const mongoose = require("mongoose");

const scoreStatsSchema = new mongoose.Schema(
    {
        count: {
            type: Number,
            default: 0
        },

        minimum: {
            type: Number,
            default: null
        },

        maximum: {
            type: Number,
            default: null
        },

        mean: {
            type: Number,
            default: null
        },

        median: {
            type: Number,
            default: null
        },

        q01: {
            type: Number,
            default: null
        },

        q05: {
            type: Number,
            default: null
        },

        q95: {
            type: Number,
            default: null
        },

        q99: {
            type: Number,
            default: null
        }
    },
    {
        _id: false
    }
);

const mlModelArtifactSchema = new mongoose.Schema(
    {
        deviceId: {
            type: String,
            required: true,
            trim: true,
            index: true
        },

        baselineSessionId: {
            type: mongoose.Schema.Types.ObjectId,
            ref: "MLBaselineSession",
            required: true,
            index: true
        },

        modelType: {
            type: String,
            enum: [
                "ISOLATION_FOREST"
            ],
            required: true,
            default: "ISOLATION_FOREST"
        },

        versionLabel: {
            type: String,
            required: true,
            trim: true
        },

        status: {
            type: String,
            enum: [
                "TRAINING",
                "READY",
                "FAILED",
                "CANCELLED",
                "ARCHIVED"
            ],
            required: true,
            default: "TRAINING",
            index: true
        },

        active: {
            type: Boolean,
            default: false,
            index: true
        },

        artifactPath: {
            type: String,
            default: null,
            trim: true
        },

        features: {
            type: [String],
            required: true,
            default: []
        },

        trainingConfiguration: {
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
        },

        sampleCounts: {
            total: {
                type: Number,
                default: 0
            },

            training: {
                type: Number,
                default: 0
            },

            validation: {
                type: Number,
                default: 0
            }
        },

        baselineTimeRange: {
            from: {
                type: Date,
                default: null
            },

            to: {
                type: Date,
                default: null
            }
        },

        trainingScores: {
            type: scoreStatsSchema,
            default: () => ({})
        },

        validationScores: {
            type: scoreStatsSchema,
            default: () => ({})
        },

        threshold: {
            calibrated: {
                type: Boolean,
                default: false
            },

            decisionFunctionThreshold: {
                type: Number,
                default: null
            },

            calibratedAt: {
                type: Date,
                default: null
            },

            healthySampleCount: {
                type: Number,
                default: 0
            },

            faultSampleCount: {
                type: Number,
                default: 0
            },

            selectedMetric: {
                type: String,
                default: null
            },

            metricValue: {
                type: Number,
                default: null
            }
        },

        trainingError: {
            type: String,
            default: null
        },

        trainedAt: {
            type: Date,
            default: null
        },

        cancelledAt: {
            type: Date,
            default: null
        },

        activatedAt: {
            type: Date,
            default: null
        }
    },
    {
        timestamps: true,
        versionKey: false,
        strict: true
    }
);

mlModelArtifactSchema.index(
    {
        deviceId: 1,
        createdAt: -1
    }
);

mlModelArtifactSchema.index(
    {
        deviceId: 1,
        active: 1
    }
);

module.exports =
    mongoose.model(
        "MLModelArtifact",
        mlModelArtifactSchema
    );