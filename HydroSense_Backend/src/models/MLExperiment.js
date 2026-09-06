const mongoose = require("mongoose");

const mlExperimentSchema = new mongoose.Schema(
    {
        deviceId: {
            type: String,
            required: true,
            trim: true,
            index: true
        },

        modelArtifactId: {
            type: mongoose.Schema.Types.ObjectId,
            ref: "MLModelArtifact",
            required: true,
            index: true
        },

        label: {
            type: String,
            enum: [
                "HEALTHY_CONTROL",
                "SENSOR_DISCONNECT",
                "STUCK_OUTPUT",
                "EXCESSIVE_NOISE",
                "SUDDEN_PERTURBATION",
                "PROBE_DEGRADATION",
                "OTHER_FAULT"
            ],
            required: true
        },

        isFault: {
            type: Boolean,
            required: true
        },

        status: {
            type: String,
            enum: [
                "ACTIVE",
                "STOPPED",
                "ABORTED"
            ],
            default: "ACTIVE",
            index: true
        },

        startedAt: {
            type: Date,
            required: true,
            index: true
        },

        stoppedAt: {
            type: Date,
            default: null
        },

        operatorNote: {
            type: String,
            default: "",
            trim: true
        },

        scoredSampleCount: {
            type: Number,
            default: 0
        },

        unscorableSampleCount: {
            type: Number,
            default: 0
        }
    },
    {
        timestamps: true,
        versionKey: false,
        strict: true
    }
);

mlExperimentSchema.index(
    {
        deviceId: 1,
        startedAt: -1
    }
);

mlExperimentSchema.index(
    {
        deviceId: 1,
        status: 1,
        startedAt: -1
    }
);

module.exports =
    mongoose.model(
        "MLExperiment",
        mlExperimentSchema
    );