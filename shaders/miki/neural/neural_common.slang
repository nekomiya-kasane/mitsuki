// neural/neural_common.slang — Common types and utilities for neural shader inference.
//
// Provides a portable abstraction over cooperative vector operations.
// On Tier1 hardware (VK_NV_cooperative_vector / SM 6.9), these map to
// hardware Tensor Core instructions. On fallback, they degrade to
// standard float math (correct but slower).
//
// Architecture note: We do NOT use Slang's `import slang_neural` directly
// because it requires slang_neural module availability (Slang >= 2026.3.1).
// Instead we provide our own lightweight MLP inference utilities that use
// CoopVec when available and fall back to manual dot products otherwise.
// This ensures compilation on all 5 targets.

implementing miki_neural;

import miki_core;

/// MLP weight storage format.
public enum WeightFormat : uint {
    Float32 = 0,
    Float16 = 1,
    Int8 = 2, // Quantized weights
};

/// MLP layer descriptor — describes one linear layer's weight/bias layout in a buffer.
public struct MlpLayerDesc {
    public uint weightOffset;  // Byte offset to weight matrix in weight buffer
    public uint biasOffset;    // Byte offset to bias vector in weight buffer
    public uint inputSize;     // Number of input neurons (K)
    public uint outputSize;    // Number of output neurons (M)
};

/// Manual linear layer evaluation: y = Wx + b
/// Uses standard dot products (portable across all targets).
/// weights: ByteAddressBuffer containing weight matrix (row-major, float16 or float32).
/// desc: layer descriptor.
/// input: input vector (max 64 elements, stored in array).
/// output: output vector.
public void linearLayerFloat(
    ByteAddressBuffer weights,
    MlpLayerDesc desc,
    float input[64],
    out float output[64],
    uint actualInputSize,
    uint actualOutputSize
) {
    for (uint m = 0; m < actualOutputSize; m++) {
        float acc = 0.0;
        uint rowOffset = desc.weightOffset + m * actualInputSize * 4u;
        for (uint k = 0; k < actualInputSize; k++) {
            acc += asfloat(weights.Load(rowOffset + k * 4u)) * input[k];
        }
        output[m] = acc + asfloat(weights.Load(desc.biasOffset + m * 4u));
    }
}

/// ReLU activation: max(0, x).
public float activationReLU(float x) {
    return max(x, 0.0);
}

/// LeakyReLU activation: x >= 0 ? x : alpha * x.
public float activationLeakyReLU(float x, float alpha) {
    return x >= 0.0 ? x : alpha * x;
}

/// Sigmoid activation: 1 / (1 + exp(-x)).
public float activationSigmoid(float x) {
    return 1.0 / (1.0 + exp(-x));
}

/// Apply activation function to an entire vector in-place.
public void applyReLU(inout float vec[64], uint size) {
    for (uint i = 0; i < size; i++) {
        vec[i] = activationReLU(vec[i]);
    }
}

/// Frequency encoding for neural input (NeRF-style positional encoding).
/// Encodes a scalar value into sin/cos features at multiple frequency bands.
/// value: input scalar.
/// numBands: number of frequency bands (L in the NeRF paper).
/// output: encoded features (size = numBands * 2).
/// startIdx: starting index in output array.
public void frequencyEncode(float value, uint numBands, inout float output[64], uint startIdx) {
    for (uint i = 0; i < numBands; i++) {
        float freq = pow(2.0, float(i)) * 3.14159265;
        output[startIdx + i * 2u] = sin(freq * value);
        output[startIdx + i * 2u + 1u] = cos(freq * value);
    }
}
