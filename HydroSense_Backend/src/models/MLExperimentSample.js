const mongoose = require("mongoose");

const mlExperimentSampleSchema = new mongoose.Schema(
    {
        deviceId: {
            type: String,
            required: true,
            trim: true,
            index: true
        },

        experimentId: {
            type: mongoose.Schema.Types.ObjectId,
            ref: "MLExperiment",
            required: true,
            index: true
        },

        modelArtifactId: {
            type: mongoose.Schema.Types.ObjectId,
            ref: "MLModelArtifact",
            required: true,
            index: true
        },

        sourceTimestamp: {
            type: Date,
            required: true,
            index: true
        },

        sequence: {
            type: Number,
            default: null
        },

        isFault: {
            type: Boolean,
            required: true,
            index: true
        },

        label: {
            type: String,
            required: true,
            trim: true
        },

        scorable: {
            type: Boolean,
            required: true,
            default: false,
            index: true
        },

        features: {
            type: mongoose.Schema.Types.Mixed,
            default: null
        },

        decisionFunction: {
            type: Number,
            default: null
        },

        scoreSamples: {
            type: Number,
            default: null
        },

        thresholdAvailable: {
            type: Boolean,
            default: false
        },

        thresholdAtCapture: {
            type: Number,
            default: null
        },

        predictedFault: {
            type: Boolean,
            default: null
        },

        unscorableReason: {
            type: String,
            default: null,
            trim: true
        }
    },
    {
        timestamps: true,
        versionKey: false,
        strict: true
    }
);

mlExperimentSampleSchema.index(
    {
        experimentId: 1,
        sourceTimestamp: 1
    }
);

mlExperimentSampleSchema.index(
    {
        modelArtifactId: 1,
        isFault: 1,
        scorable: 1
    }
);

module.exports =
    mongoose.model(
        "MLExperimentSample",
        mlExperimentSampleSchema
    );