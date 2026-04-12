// neural/nrc.slang — Neural Radiance Cache (NRC).
//
// GPU hash-grid encoder + small MLP for indirect illumination caching.
// Replaces traditional irradiance probes for dynamic scenes where probe
// placement is difficult (indoor CAD models, complex CAE geometries).
//
// Architecture (Instant-NGP style, Muller et al. 2022):
// 1. Multi-resolution hash grid: 3D position -> feature vector
//    - L levels, each with T hash table entries of F features
//    - Default: L=16 levels, T=2^19 entries, F=2 features per entry
// 2. Small MLP: feature vector (L*F=32) + view direction (3) -> RGB (3)
//    - 32+3=35 inputs -> 64 hidden (ReLU) -> 64 hidden (ReLU) -> 3 output
//
// Performance target: < 0.1ms for 1M NRC queries.
//
// Reference: Muller et al. "Instant Neural Graphics Primitives" (2022),
//            NVIDIA NRC (GTC 2024), SHaRC (NVIDIA 2024).

implementing miki_neural;

import miki_core;

/// Hash grid configuration.
public struct HashGridConfig {
    public uint numLevels;       // Number of resolution levels (default: 16)
    public uint hashTableSize;   // Number of entries per level (default: 2^19 = 524288)
    public uint featuresPerEntry; // Features per hash entry (default: 2)
    public float baseResolution; // Finest grid resolution in world units
    public float logScale;       // Log2 scale factor between levels (default: 1.38)
    public float3 aabbMin;       // World-space AABB minimum for the hash grid
    public float3 aabbMax;       // World-space AABB maximum for the hash grid
};

/// Compute hash grid resolution for a given level.
public float gridResolution(HashGridConfig config, uint level) {
    return config.baseResolution * pow(2.0, float(level) * config.logScale);
}

/// Spatial hash function (same as rt_gi.slang sharcHash but with level support).
/// Maps a 3D integer grid cell to a hash table index.
public uint hashGridCell(int3 cell, uint level, uint tableSize) {
    // XOR-based spatial hash with level mixing
    uint h = uint(cell.x) * 1u ^ uint(cell.y) * 2654435761u ^ uint(cell.z) * 805459861u;
    h ^= level * 2246822519u;
    return h % tableSize;
}

/// Encode a 3D position into the multi-resolution hash grid.
/// Returns a feature vector of size numLevels * featuresPerEntry.
/// featureBuffer: StructuredBuffer containing hash grid feature entries.
/// config: hash grid configuration.
/// worldPos: 3D position to encode.
/// outputFeatures: output feature vector.
public void hashGridEncode(
    StructuredBuffer<float> featureBuffer,
    HashGridConfig config,
    float3 worldPos,
    out float features[64]
) {
    // Normalize position to [0, 1] within AABB
    float3 aabbSize = max(config.aabbMax - config.aabbMin, float3(1e-6));
    float3 normalizedPos = (worldPos - config.aabbMin) / aabbSize;

    uint featureIdx = 0u;
    for (uint level = 0; level < config.numLevels && level < 16u; level++) {
        float resolution = gridResolution(config, level);
        float3 scaledPos = normalizedPos * resolution;
        int3 cell = int3(floor(scaledPos));
        float3 frac_ = scaledPos - float3(cell);

        // Trilinear interpolation of features from 8 corners
        float cornerFeatures[2]; // featuresPerEntry
        for (uint f = 0; f < min(config.featuresPerEntry, 2u); f++) {
            cornerFeatures[f] = 0.0;
        }

        for (uint corner = 0; corner < 8u; corner++) {
            int3 offset = int3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
            int3 cornerCell = cell + offset;
            uint hashIdx = hashGridCell(cornerCell, level, config.hashTableSize);

            float3 w = float3(
                (corner & 1u) != 0u ? frac_.x : (1.0 - frac_.x),
                (corner & 2u) != 0u ? frac_.y : (1.0 - frac_.y),
                (corner & 4u) != 0u ? frac_.z : (1.0 - frac_.z)
            );
            float weight = w.x * w.y * w.z;

            for (uint f = 0; f < min(config.featuresPerEntry, 2u); f++) {
                uint bufferIdx = (level * config.hashTableSize + hashIdx) * config.featuresPerEntry + f;
                cornerFeatures[f] += featureBuffer[bufferIdx] * weight;
            }
        }

        for (uint f = 0; f < min(config.featuresPerEntry, 2u); f++) {
            features[featureIdx] = cornerFeatures[f];
            featureIdx++;
        }
    }
    // Zero-fill remaining
    for (uint i = featureIdx; i < 64u; i++) features[i] = 0.0;
}

/// NRC MLP weight layout.
/// Input: hash features (32) + view direction (3) = 35
/// Hidden: 64 -> 64
/// Output: radiance RGB (3)
public struct NrcMlpWeights {
    public static const uint kInputSize = 35u;
    public static const uint kHiddenSize = 64u;
    public static const uint kOutputSize = 3u;

    // Byte offsets
    public static const uint kL0WeightOff = 0u;          // 35*64*4 = 8960
    public static const uint kL0BiasOff = 8960u;          // 64*4 = 256
    public static const uint kL1WeightOff = 9216u;         // 64*64*4 = 16384
    public static const uint kL1BiasOff = 25600u;          // 64*4 = 256
    public static const uint kL2WeightOff = 25856u;        // 64*3*4 = 768
    public static const uint kL2BiasOff = 26624u;          // 3*4 = 12
    public static const uint kTotalSize = 26636u;
};

/// Query the Neural Radiance Cache.
/// Encodes position via hash grid, concatenates with view direction,
/// and evaluates MLP to produce radiance estimate.
public float3 queryNrc(
    StructuredBuffer<float> hashGridFeatures,
    ByteAddressBuffer mlpWeights,
    HashGridConfig config,
    float3 worldPos,
    float3 viewDir
) {
    // Step 1: Hash grid encode
    float hashFeatures[64];
    hashGridEncode(hashGridFeatures, config, worldPos, hashFeatures);

    // Step 2: Concatenate hash features + view direction
    float mlpInput[64];
    uint numHashFeatures = min(config.numLevels * config.featuresPerEntry, 32u);
    for (uint i = 0; i < numHashFeatures; i++) mlpInput[i] = hashFeatures[i];
    float3 normDir = normalize(viewDir);
    mlpInput[numHashFeatures] = normDir.x;
    mlpInput[numHashFeatures + 1u] = normDir.y;
    mlpInput[numHashFeatures + 2u] = normDir.z;

    uint totalInput = numHashFeatures + 3u;

    // Step 3: MLP inference (35 -> 64 -> 64 -> 3)
    MlpLayerDesc d0;
    d0.weightOffset = NrcMlpWeights::kL0WeightOff;
    d0.biasOffset = NrcMlpWeights::kL0BiasOff;
    d0.inputSize = totalInput;
    d0.outputSize = 64u;
    float h0[64];
    linearLayerFloat(mlpWeights, d0, mlpInput, h0, totalInput, 64u);
    applyReLU(h0, 64u);

    MlpLayerDesc d1;
    d1.weightOffset = NrcMlpWeights::kL1WeightOff;
    d1.biasOffset = NrcMlpWeights::kL1BiasOff;
    d1.inputSize = 64u;
    d1.outputSize = 64u;
    float h1[64];
    linearLayerFloat(mlpWeights, d1, h0, h1, 64u, 64u);
    applyReLU(h1, 64u);

    MlpLayerDesc d2;
    d2.weightOffset = NrcMlpWeights::kL2WeightOff;
    d2.biasOffset = NrcMlpWeights::kL2BiasOff;
    d2.inputSize = 64u;
    d2.outputSize = 3u;
    float outputVec[64];
    linearLayerFloat(mlpWeights, d2, h1, outputVec, 64u, 3u);

    // Exponential activation for non-negative radiance
    return float3(exp(outputVec[0]), exp(outputVec[1]), exp(outputVec[2]));
}
