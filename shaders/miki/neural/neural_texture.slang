// neural/neural_texture.slang — Neural texture compression decoder.
//
// Decodes neural-compressed textures using a small MLP:
// Input:  4 floats (uv.x, uv.y, mipLevel, featureIndex)
// Hidden: 2 layers of 16 neurons with ReLU activation
// Output: 4 floats (R, G, B, A)
//
// Architecture: 4 -> 16 -> 16 -> 4 MLP
// Total parameters: (4*16+16) + (16*16+16) + (16*4+4) = 404 floats = 1.6 KB
//
// This is the inference-only path. Training happens offline via RTXNS
// or PyTorch + slang-torch differentiable rendering pipeline.
//
// On Tier1 hardware with VK_NV_cooperative_vector, the matrix-vector
// multiplies map to Tensor Core hardware for ~4x speedup.
//
// Performance target: < 0.5ms per 1024x1024 texture decode dispatch.
//
// Reference: NVIDIA NTC (Neural Texture Compression), BCn + neural hybrid.

implementing miki_neural;

import miki_core;

/// Neural texture weight buffer layout.
/// Weights are packed sequentially: [layer0_weights, layer0_bias, layer1_weights, ...]
public struct NeuralTextureWeights {
    public static const uint kInputSize = 4u;
    public static const uint kHiddenSize = 16u;
    public static const uint kOutputSize = 4u;
    public static const uint kNumLayers = 3u;

    // Layer 0: 4 -> 16 = 64 weights + 16 bias = 80 floats = 320 bytes
    public static const uint kLayer0WeightOffset = 0u;
    public static const uint kLayer0BiasOffset = 256u;   // 64 * 4

    // Layer 1: 16 -> 16 = 256 weights + 16 bias = 272 floats = 1088 bytes
    public static const uint kLayer1WeightOffset = 320u;  // 80 * 4
    public static const uint kLayer1BiasOffset = 1344u;   // (80 + 256) * 4

    // Layer 2: 16 -> 4 = 64 weights + 4 bias = 68 floats = 272 bytes
    public static const uint kLayer2WeightOffset = 1408u;  // (80 + 272) * 4
    public static const uint kLayer2BiasOffset = 1664u;    // (80 + 272 + 64) * 4

    // Total buffer size: 1680 bytes (420 floats)
    public static const uint kTotalBufferSize = 1680u;
};

/// Decode a neural texture sample.
/// weightBuffer: ByteAddressBuffer containing trained MLP weights.
/// uv: texture coordinates [0, 1]^2.
/// mipLevel: mip level (fractional).
/// featureIndex: material feature channel index.
/// Returns decoded RGBA color.
public float4 decodeNeuralTexture(
    ByteAddressBuffer weightBuffer,
    float2 uv,
    float mipLevel,
    uint featureIndex
) {
    // Assemble input vector
    float hidden0[64];
    float hidden1[64];
    float inputVec[64];
    inputVec[0] = uv.x;
    inputVec[1] = uv.y;
    inputVec[2] = mipLevel;
    inputVec[3] = float(featureIndex);

    // Layer 0: input(4) -> hidden(16), ReLU
    MlpLayerDesc desc0;
    desc0.weightOffset = NeuralTextureWeights::kLayer0WeightOffset;
    desc0.biasOffset = NeuralTextureWeights::kLayer0BiasOffset;
    desc0.inputSize = NeuralTextureWeights::kInputSize;
    desc0.outputSize = NeuralTextureWeights::kHiddenSize;
    linearLayerFloat(weightBuffer, desc0, inputVec, hidden0, 4u, 16u);
    applyReLU(hidden0, 16u);

    // Layer 1: hidden(16) -> hidden(16), ReLU
    MlpLayerDesc desc1;
    desc1.weightOffset = NeuralTextureWeights::kLayer1WeightOffset;
    desc1.biasOffset = NeuralTextureWeights::kLayer1BiasOffset;
    desc1.inputSize = NeuralTextureWeights::kHiddenSize;
    desc1.outputSize = NeuralTextureWeights::kHiddenSize;
    linearLayerFloat(weightBuffer, desc1, hidden0, hidden1, 16u, 16u);
    applyReLU(hidden1, 16u);

    // Layer 2: hidden(16) -> output(4), sigmoid for [0,1] output
    MlpLayerDesc desc2;
    desc2.weightOffset = NeuralTextureWeights::kLayer2WeightOffset;
    desc2.biasOffset = NeuralTextureWeights::kLayer2BiasOffset;
    desc2.inputSize = NeuralTextureWeights::kHiddenSize;
    desc2.outputSize = NeuralTextureWeights::kOutputSize;
    float outputVec[64];
    linearLayerFloat(weightBuffer, desc2, hidden1, outputVec, 16u, 4u);

    return float4(
        activationSigmoid(outputVec[0]),
        activationSigmoid(outputVec[1]),
        activationSigmoid(outputVec[2]),
        activationSigmoid(outputVec[3])
    );
}

/// Batch decode multiple texels in a compute workgroup.
/// Each thread decodes one texel. Results written to output texture.
public float4 decodeNeuralTextureBilinear(
    ByteAddressBuffer weightBuffer,
    float2 uv,
    float mipLevel,
    uint featureIndex,
    float2 texelSize
) {
    // Sample 4 nearest texels and bilinear interpolate
    float2 uvFloor = floor(uv / texelSize) * texelSize;
    float2 frac_ = (uv - uvFloor) / texelSize;

    float4 s00 = decodeNeuralTexture(weightBuffer, uvFloor, mipLevel, featureIndex);
    float4 s10 = decodeNeuralTexture(weightBuffer, uvFloor + float2(texelSize.x, 0), mipLevel, featureIndex);
    float4 s01 = decodeNeuralTexture(weightBuffer, uvFloor + float2(0, texelSize.y), mipLevel, featureIndex);
    float4 s11 = decodeNeuralTexture(weightBuffer, uvFloor + texelSize, mipLevel, featureIndex);

    return lerp(lerp(s00, s10, frac_.x), lerp(s01, s11, frac_.x), frac_.y);
}
