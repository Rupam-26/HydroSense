#include "tinyml/S3TinyMLTDS.h"

#include <cmath>

#include "tinyml/HydroSense_TDS_constants.h"
#include "tinyml/HydroSense_TDS_FP32_weights.h"

// ============================================================
// HYDROSENSE TDS SOFT SENSOR
//
// Direct FP32 neural-network inference.
//
// Exact trained architecture:
//
//   3
//   ↓
//   Dense 32 + ReLU
//   ↓
//   Dense 16 + ReLU
//   ↓
//   Dense 8 + ReLU
//   ↓
//   Dense 1
//
// Input order:
//   0 = DHT_temp
//   1 = DHT_humidity
//   2 = water_temp
//
// No TensorFlow.
// No TFLite.
// No external ML runtime.
// ============================================================

namespace
{

    // ============================================================
    // RELU
    // ============================================================

    inline float relu(
        float value)
    {
        return value > 0.0f
                   ? value
                   : 0.0f;
    }

    // ============================================================
    // DENSE LAYER 1
    //
    // Input  : 3
    // Output : 32
    // Activation: ReLU
    // ============================================================

    void denseLayer1(
        const float input[3],
        float output[32])
    {
        using namespace HydroSenseTDSWeights;

        for (
            int outputIndex = 0;
            outputIndex < L1_SIZE;
            ++outputIndex)
        {
            float value =
                L1_BIAS[outputIndex];

            for (
                int inputIndex = 0;
                inputIndex < INPUT_SIZE;
                ++inputIndex)
            {
                value +=
                    input[inputIndex] *
                    W1[inputIndex *
                           L1_SIZE +
                       outputIndex];
            }

            output[outputIndex] =
                relu(
                    value);
        }
    }

    // ============================================================
    // DENSE LAYER 2
    //
    // Input  : 32
    // Output : 16
    // Activation: ReLU
    // ============================================================

    void denseLayer2(
        const float input[32],
        float output[16])
    {
        using namespace HydroSenseTDSWeights;

        for (
            int outputIndex = 0;
            outputIndex < L2_SIZE;
            ++outputIndex)
        {
            float value =
                L2_BIAS[outputIndex];

            for (
                int inputIndex = 0;
                inputIndex < L1_SIZE;
                ++inputIndex)
            {
                value +=
                    input[inputIndex] *
                    W2[inputIndex *
                           L2_SIZE +
                       outputIndex];
            }

            output[outputIndex] =
                relu(
                    value);
        }
    }

    // ============================================================
    // DENSE LAYER 3
    //
    // Input  : 16
    // Output : 8
    // Activation: ReLU
    // ============================================================

    void denseLayer3(
        const float input[16],
        float output[8])
    {
        using namespace HydroSenseTDSWeights;

        for (
            int outputIndex = 0;
            outputIndex < L3_SIZE;
            ++outputIndex)
        {
            float value =
                L3_BIAS[outputIndex];

            for (
                int inputIndex = 0;
                inputIndex < L2_SIZE;
                ++inputIndex)
            {
                value +=
                    input[inputIndex] *
                    W3[inputIndex *
                           L3_SIZE +
                       outputIndex];
            }

            output[outputIndex] =
                relu(
                    value);
        }
    }

    // ============================================================
    // OUTPUT LAYER
    //
    // Input  : 8
    // Output : 1
    // Activation: Linear
    // ============================================================

    float outputLayer(
        const float input[8])
    {
        using namespace HydroSenseTDSWeights;

        float value =
            OUTPUT_BIAS[0];

        for (
            int inputIndex = 0;
            inputIndex < L3_SIZE;
            ++inputIndex)
        {
            value +=
                input[inputIndex] *
                W4[inputIndex];
        }

        return value;
    }

    // ============================================================
    // RUN COMPLETE NETWORK
    // ============================================================

    float runNetwork(
        const float standardizedInput[3])
    {
        float layer1[32];
        float layer2[16];
        float layer3[8];

        denseLayer1(
            standardizedInput,
            layer1);

        denseLayer2(
            layer1,
            layer2);

        denseLayer3(
            layer2,
            layer3);

        return outputLayer(
            layer3);
    }

}

// ============================================================
// INITIALIZATION
// ============================================================

bool S3TinyMLTDS::begin()
{
    initialized_ =
        false;

    // --------------------------------------------------------
    // Verify normalization parameters.
    // --------------------------------------------------------

    for (
        int i = 0;
        i < 3;
        ++i)
    {
        if (
            !isfinite(
                HS_X_MEAN[i]) ||
            !isfinite(
                HS_X_SCALE[i]) ||
            HS_X_SCALE[i] <=
                0.0f)
        {
            return false;
        }
    }

    if (
        !isfinite(
            HS_TDS_MEAN) ||
        !isfinite(
            HS_TDS_SCALE) ||
        HS_TDS_SCALE <=
            0.0f)
    {
        return false;
    }

    initialized_ =
        true;

    return true;
}

// ============================================================
// READY
// ============================================================

bool S3TinyMLTDS::ready() const
{
    return initialized_;
}

// ============================================================
// PREDICTION
// ============================================================

S3TDSResult S3TinyMLTDS::predict(
    const S3SensorSnapshot &snapshot)
{
    S3TDSResult result;

    result.clear();

    result.sourceSequence =
        snapshot.sequence;

    // --------------------------------------------------------
    // MODEL INITIALIZATION CHECK
    // --------------------------------------------------------

    if (
        !initialized_)
    {
        result.status =
            S3TDSStatus::NotReady;

        return result;
    }

    // --------------------------------------------------------
    // REQUIRED SENSOR CHECK
    //
    // Model requires:
    //
    // DHT_temp
    // DHT_humidity
    // water_temp
    // --------------------------------------------------------

    if (
        !snapshot.airEnvironmentAvailable() ||
        !snapshot.waterTemperatureAvailable())
    {
        result.status =
            S3TDSStatus::InputUnavailable;

        return result;
    }

    // --------------------------------------------------------
    // RAW INPUT
    //
    // IMPORTANT:
    // Feature order must remain exactly identical to training.
    // --------------------------------------------------------

    const float rawInput[3] =
        {
            snapshot.airTemperatureC,
            snapshot.humidityPct,
            snapshot.waterTemperatureC};

    // --------------------------------------------------------
    // STANDARDIZE INPUT
    //
    // x_scaled =
    //      (x - training_mean)
    //      /
    //      training_std
    // --------------------------------------------------------

    float standardizedInput[3];

    for (
        int i = 0;
        i < 3;
        ++i)
    {
        if (
            !isfinite(
                rawInput[i]))
        {
            result.status =
                S3TDSStatus::InputUnavailable;

            return result;
        }

        standardizedInput[i] =
            (rawInput[i] -
             HS_X_MEAN[i]) /
            HS_X_SCALE[i];

        if (
            !isfinite(
                standardizedInput[i]))
        {
            result.status =
                S3TDSStatus::InputUnavailable;

            return result;
        }
    }

    // --------------------------------------------------------
    // FP32 MODEL HAS NO INT8 INPUT SATURATION.
    // --------------------------------------------------------

    result.inputSaturated =
        false;

    // --------------------------------------------------------
    // EDGE INFERENCE
    // --------------------------------------------------------

    const uint32_t startedUs =
        micros();

    const float standardizedOutput =
        runNetwork(
            standardizedInput);

    result.inferenceMicros =
        micros() -
        startedUs;

    // --------------------------------------------------------
    // CHECK NETWORK OUTPUT
    // --------------------------------------------------------

    if (
        !isfinite(
            standardizedOutput))
    {
        result.status =
            S3TDSStatus::InvalidOutput;

        return result;
    }

    // --------------------------------------------------------
    // CONVERT STANDARDIZED OUTPUT BACK TO TDS PPM
    //
    // TDS =
    // standardized_output * target_std
    // +
    // target_mean
    // --------------------------------------------------------

    const float tdsPpm =
        standardizedOutput *
            HS_TDS_SCALE +
        HS_TDS_MEAN;

    result.normalizedOutput =
        standardizedOutput;

    result.estimatedTdsPpm =
        tdsPpm;

    // --------------------------------------------------------
    // FINAL OUTPUT VALIDATION
    // --------------------------------------------------------

    if (
        !isfinite(
            tdsPpm) ||
        tdsPpm <
            0.0f)
    {
        result.status =
            S3TDSStatus::InvalidOutput;

        return result;
    }

    result.status =
        S3TDSStatus::Valid;

    return result;
}

// ============================================================
// STATUS TEXT
// ============================================================

const char *S3TinyMLTDS::statusText(
    S3TDSStatus status)
{
    switch (
        status)
    {
    case S3TDSStatus::NotReady:

        return "NOT_READY";

    case S3TDSStatus::InputUnavailable:

        return "INPUT_UNAVAILABLE";

    case S3TDSStatus::InputSaturated:

        return "INPUT_OUT_OF_RANGE";

    case S3TDSStatus::InvokeFailed:

        return "INFERENCE_FAILED";

    case S3TDSStatus::InvalidOutput:

        return "INVALID_OUTPUT";

    case S3TDSStatus::Valid:

        return "VALID";

    default:

        return "UNKNOWN";
    }
}