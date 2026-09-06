const mongoose = require("mongoose");

const mlBaselineSessionSchema = new mongoose.Schema(
    {
        deviceId: {
            type: String,
            required: true,
            trim: true,
            index: true
        },

        status: {
            type: String,
            enum: [
                "ACTIVE",
                "STOPPED",
                "TRAINED",
                "ABORTED"
            ],
            required: true,
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

        trainedAt: {
            type: Date,
            default: null
        },

        configurationSnapshot: {
            type: mongoose.Schema.Types.Mixed,
            required: true
        },

        operatorNote: {
            type: String,
            default: "",
            trim: true
        }
    },
    {
        timestamps: true,
        versionKey: false,
        strict: true
    }
);

mlBaselineSessionSchema.index(
    {
        deviceId: 1,
        startedAt: -1
    }
);

mlBaselineSessionSchema.index(
    {
        deviceId: 1,
        status: 1,
        startedAt: -1
    }
);

module.exports =
    mongoose.model(
        "MLBaselineSession",
        mlBaselineSessionSchema
    );