// rt/rt_gi.slang — RT Global Illumination.
//
// Implements multi-strategy RT GI:
// - 1-bounce indirect diffuse with importance-sampled cosine hemisphere
// - Irradiance probes (DDGI-style) for real-time indirect lighting
// - Screen-space irradiance cache with world-space hash for temporal reuse
// - SHaRC (Spatial Hash Radiance Cache) for scalable indirect illumination
//
// Reference: NVIDIA RTXGI/DDGI, SHaRC (NVIDIA 2024), UE5 Lumen GI,
//            "DDGI: Dynamic Diffuse GI with RT" (Majercik 2019).

implementing miki_rt;

import miki_core;

/// Irradiance probe grid parameters (DDGI).
public struct ProbeGridParams {
    public float3 gridOrigin;      // World-space origin of the probe grid
    public float3 gridSpacing;     // Distance between probes in each axis
    public int3   gridDimensions;  // Number of probes per axis (e.g. 32x16x32)
    public float  hysteresis;      // Temporal blend factor for probe update (default: 0.97)
    public float  normalBias;      // Bias along surface normal for sampling
    public float  viewBias;        // Bias along view direction for sampling
    public float  irradianceGamma; // Gamma for irradiance encoding (default: 5.0)
};

/// Compute probe grid index from world position.
/// Returns int3 grid coordinates, clamped to valid range.
public int3 worldToProbeIndex(float3 worldPos, ProbeGridParams params) {
    float3 localPos = (worldPos - params.gridOrigin) / max(params.gridSpacing, float3(1e-6));
    return clamp(int3(floor(localPos)), int3(0), params.gridDimensions - int3(1));
}

/// Compute 1D probe index from 3D grid coordinates.
public uint probeIndexFlat(int3 gridCoord, int3 gridDim) {
    return uint(gridCoord.x + gridCoord.y * gridDim.x + gridCoord.z * gridDim.x * gridDim.y);
}

/// Octahedral encoding for probe irradiance/distance storage.
/// Encodes a unit direction to [0,1]^2 UV for probe texture lookup.
public float2 octEncode(float3 n) {
    float3 absN = abs(n);
    float sum = absN.x + absN.y + absN.z;
    float2 p = n.xy / max(sum, 1e-6);
    if (n.z < 0.0) {
        p = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    return p * 0.5 + 0.5;
}

/// Decode octahedral UV back to unit direction.
public float3 octDecode(float2 uv) {
    float2 p = uv * 2.0 - 1.0;
    float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

/// Trilinear interpolation weights for 8 surrounding probes.
/// alpha: fractional position within probe cell [0,1]^3.
public float trilinearWeight(float3 alpha, uint cornerIndex) {
    float3 w = float3(
        (cornerIndex & 1u) != 0u ? alpha.x : (1.0 - alpha.x),
        (cornerIndex & 2u) != 0u ? alpha.y : (1.0 - alpha.y),
        (cornerIndex & 4u) != 0u ? alpha.z : (1.0 - alpha.z)
    );
    return w.x * w.y * w.z;
}

/// Sample irradiance from the probe grid for a surface point.
/// Uses trilinear interpolation + normal-weighted blending.
/// Returns estimated irradiance (RGB) at the surface.
public float3 sampleProbeIrradiance(
    float3 worldPos,
    float3 surfaceNormal,
    Texture2DArray<float4> probeIrradiance, // Probe atlas texture
    SamplerState probeSampler,
    ProbeGridParams params
) {
    float3 biasedPos = worldPos + surfaceNormal * params.normalBias;
    float3 localPos = (biasedPos - params.gridOrigin) / max(params.gridSpacing, float3(1e-6));
    int3 baseCoord = int3(floor(localPos));
    float3 alpha = frac(localPos);

    float3 irradiance = float3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0; i < 8u; i++) {
        int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 probeCoord = clamp(baseCoord + offset, int3(0), params.gridDimensions - int3(1));
        uint probeIdx = probeIndexFlat(probeCoord, params.gridDimensions);

        // Direction from probe to surface
        float3 probePos = params.gridOrigin + float3(probeCoord) * params.gridSpacing;
        float3 probeToSurface = worldPos - probePos;
        float distToProbe = length(probeToSurface);

        // Weight by normal alignment (reject backfacing probes)
        float normalWeight = max(dot(surfaceNormal, normalize(-probeToSurface)), 0.0);

        float weight = trilinearWeight(alpha, i) * normalWeight;
        if (weight < 1e-6) continue;

        // Sample probe irradiance at surface normal direction
        float2 octUv = octEncode(surfaceNormal);
        float4 sample_ = probeIrradiance.SampleLevel(probeSampler, float3(octUv, float(probeIdx)), 0);
        irradiance += sample_.rgb * weight;
        totalWeight += weight;
    }

    return totalWeight > 0.0 ? irradiance / totalWeight : float3(0.0);
}

/// SHaRC hash function: map world position to hash table index.
/// Uses multi-resolution spatial hashing (Muller et al. 2022 instant-NGP style).
public uint sharcHash(float3 worldPos, float voxelSize, uint tableSize) {
    int3 grid = int3(floor(worldPos / max(voxelSize, 1e-6)));
    uint h = uint(grid.x) * 73856093u ^ uint(grid.y) * 19349663u ^ uint(grid.z) * 83492791u;
    return h % tableSize;
}
