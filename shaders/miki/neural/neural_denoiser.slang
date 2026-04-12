// neural/neural_denoiser.slang — ML denoiser inference for RT output.
//
// Implements a lightweight neural denoiser as a compute shader:
// - Encoder: 3x3 conv-like neighborhood gather into feature vector
// - Small MLP: feature vector -> denoised RGB
// - Temporal accumulation input for stability
//
// Architecture (simplified U-Net):
// Input: noisy RGB (3) + albedo (3) + normal (3) + depth (1) + motion (2) = 12
// Hidden: 32 -> 32 -> 16
// Output: denoised RGB (3)
// Total: (12*32+32) + (32*32+32) + (32*16+16) + (16*3+3) = 1971 params
//
// This is a lightweight inference path. Full SVGF/BMFR spatial filtering
// (from rt/denoiser.slang) handles the spatial component; this neural
// denoiser provides the learned temporal fusion and detail recovery.
//
// Performance target: < 2ms at 1080p for full inference dispatch.
//
// Reference: NVIDIA NRD neural mode, Intel Open Image Denoise (OIDN),
//            "Neural Bilateral Grid Denoising" (Mildenhall 2023).

implementing miki_neural;

import miki_core;

/// Neural denoiser input feature assembly.
/// Gathers features from G-buffer textures for the denoiser MLP.
public struct DenoiserFeatures {
    public float3 noisyColor;     // Raw RT output
    public float3 albedo;         // Material albedo (from G-buffer)
    public float3 worldNormal;    // Surface normal (from G-buffer)
    public float  depth;          // Linear depth
    public float2 motionVector;   // Screen-space motion vector
};

/// Pack DenoiserFeatures into a 12-element float array for MLP input.
public void packDenoiserInput(DenoiserFeatures features, out float input[64]) {
    input[0] = features.noisyColor.x;
    input[1] = features.noisyColor.y;
    input[2] = features.noisyColor.z;
    input[3] = features.albedo.x;
    input[4] = features.albedo.y;
    input[5] = features.albedo.z;
    input[6] = features.worldNormal.x;
    input[7] = features.worldNormal.y;
    input[8] = features.worldNormal.z;
    input[9] = features.depth;
    input[10] = features.motionVector.x;
    input[11] = features.motionVector.y;
}

/// Neural denoiser weight layout.
public struct NeuralDenoiserWeights {
    public static const uint kInputSize = 12u;
    public static const uint kHidden0 = 32u;
    public static const uint kHidden1 = 32u;
    public static const uint kHidden2 = 16u;
    public static const uint kOutputSize = 3u;

    // Layer offsets (float32, byte offsets)
    public static const uint kL0WeightOff = 0u;
    public static const uint kL0BiasOff = 1536u;   // 12*32*4
    public static const uint kL1WeightOff = 1664u;  // (12*32+32)*4
    public static const uint kL1BiasOff = 5760u;    // (12*32+32+32*32)*4
    public static const uint kL2WeightOff = 5888u;
    public static const uint kL2BiasOff = 7936u;    // +32*16*4
    public static const uint kL3WeightOff = 8000u;
    public static const uint kL3BiasOff = 8192u;    // +16*3*4
    public static const uint kTotalSize = 8204u;     // +3*4
};

/// Run neural denoiser inference for a single pixel.
/// weights: ByteAddressBuffer containing trained denoiser weights.
/// features: assembled input features for this pixel.
/// Returns denoised RGB color.
public float3 neuralDenoise(
    ByteAddressBuffer weights,
    DenoiserFeatures features
) {
    float inputVec[64];
    packDenoiserInput(features, inputVec);

    // Layer 0: 12 -> 32, LeakyReLU
    MlpLayerDesc d0;
    d0.weightOffset = NeuralDenoiserWeights::kL0WeightOff;
    d0.biasOffset = NeuralDenoiserWeights::kL0BiasOff;
    d0.inputSize = 12u;
    d0.outputSize = 32u;
    float h0[64];
    linearLayerFloat(weights, d0, inputVec, h0, 12u, 32u);
    for (uint i = 0; i < 32u; i++) h0[i] = activationLeakyReLU(h0[i], 0.1);

    // Layer 1: 32 -> 32, LeakyReLU
    MlpLayerDesc d1;
    d1.weightOffset = NeuralDenoiserWeights::kL1WeightOff;
    d1.biasOffset = NeuralDenoiserWeights::kL1BiasOff;
    d1.inputSize = 32u;
    d1.outputSize = 32u;
    float h1[64];
    linearLayerFloat(weights, d1, h0, h1, 32u, 32u);
    for (uint i = 0; i < 32u; i++) h1[i] = activationLeakyReLU(h1[i], 0.1);

    // Layer 2: 32 -> 16, LeakyReLU
    MlpLayerDesc d2;
    d2.weightOffset = NeuralDenoiserWeights::kL2WeightOff;
    d2.biasOffset = NeuralDenoiserWeights::kL2BiasOff;
    d2.inputSize = 32u;
    d2.outputSize = 16u;
    float h2[64];
    linearLayerFloat(weights, d2, h1, h2, 32u, 16u);
    for (uint i = 0; i < 16u; i++) h2[i] = activationLeakyReLU(h2[i], 0.1);

    // Layer 3: 16 -> 3, sigmoid (output in [0,1])
    MlpLayerDesc d3;
    d3.weightOffset = NeuralDenoiserWeights::kL3WeightOff;
    d3.biasOffset = NeuralDenoiserWeights::kL3BiasOff;
    d3.inputSize = 16u;
    d3.outputSize = 3u;
    float outputVec[64];
    linearLayerFloat(weights, d3, h2, outputVec, 16u, 3u);

    return float3(
        activationSigmoid(outputVec[0]),
        activationSigmoid(outputVec[1]),
        activationSigmoid(outputVec[2])
    );
}

/// Temporal blend: combine neural denoiser output with history.
/// current: neural denoiser output for this frame.
/// history: accumulated result from previous frame.
/// confidence: reprojection confidence [0,1].
public float3 neuralDenoiserTemporalBlend(float3 current, float3 history, float confidence) {
    float alpha = lerp(1.0, 0.05, confidence);
    return lerp(history, current, alpha);
}
