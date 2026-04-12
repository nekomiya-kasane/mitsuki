// lighting/ddgi.slang — Dynamic Diffuse Global Illumination (implementing miki_lighting).
//
// DDGI (Majercik et al. 2019, 2021) places irradiance + visibility probes
// in a regular 3D grid. Probes are updated each frame via ray tracing or
// light field probes, then sampled during shading for diffuse GI.

implementing miki_lighting;

import miki_core;
import miki_math;

/// DDGI probe grid configuration.
public struct DDGIVolume {
    public float3 origin;       // world-space origin of the probe grid
    public float3 spacing;      // spacing between probes (x, y, z)
    public uint3 probeCount;    // number of probes in each dimension
    public float hysteresis;    // temporal blending factor (0.97 typical)
    public float maxRayDistance; // max distance for probe rays
};

/// Compute the 3D probe grid index for a world position.
[ForceInline]
public int3 worldToProbeIndex(float3 worldPos, DDGIVolume volume) {
    float3 local = (worldPos - volume.origin) / volume.spacing;
    return int3(clamp(local, float3(0.0f), float3(volume.probeCount) - 1.0f));
}

/// Compute the linear probe index from 3D coordinates.
[ForceInline]
public uint probeLinearIndex(int3 probeCoord, uint3 probeCount) {
    return uint(probeCoord.z) * probeCount.x * probeCount.y
         + uint(probeCoord.y) * probeCount.x
         + uint(probeCoord.x);
}

/// Compute the world position of a probe.
[ForceInline]
public float3 probeWorldPosition(int3 probeCoord, DDGIVolume volume) {
    return volume.origin + float3(probeCoord) * volume.spacing;
}

/// Sample irradiance from a single probe using octahedral encoding.
/// The irradiance texture stores each probe's irradiance as an octahedral map.
[ForceInline]
public float3 sampleProbeIrradiance(
    Texture2D<float4> irradianceAtlas, SamplerState probeSampler,
    uint probeIndex, float3 direction, uint probeTexSize, uint atlasWidth
) {
    // Compute probe atlas UV from probe index + octahedral direction
    uint probesPerRow = atlasWidth / probeTexSize;
    uint probeX = probeIndex % probesPerRow;
    uint probeY = probeIndex / probesPerRow;

    // Octahedral encoding of direction → [0, 1]^2
    float2 oct = octEncode(direction) * 0.5f + 0.5f;

    // Map to atlas UV
    float2 atlasUV;
    atlasUV.x = (float(probeX * probeTexSize) + oct.x * float(probeTexSize - 1) + 0.5f) / float(atlasWidth);
    atlasUV.y = (float(probeY * probeTexSize) + oct.y * float(probeTexSize - 1) + 0.5f) / float(atlasWidth);

    return irradianceAtlas.SampleLevel(probeSampler, atlasUV, 0.0f).rgb;
}

/// Octahedral encoding: direction on unit sphere → [−1, 1]^2.
[ForceInline]
public float2 octEncode(float3 n) {
    float l1norm = abs(n.x) + abs(n.y) + abs(n.z);
    float2 result = n.xy * (1.0f / l1norm);
    if (n.z < 0.0f) {
        result = (1.0f - abs(result.yx)) * float2(result.x >= 0.0f ? 1.0f : -1.0f, result.y >= 0.0f ? 1.0f : -1.0f);
    }
    return result;
}

/// Trilinear interpolation of irradiance from 8 surrounding probes.
[ForceInline]
public float3 sampleDDGIIrradiance(
    float3 worldPos, float3 surfaceNormal, DDGIVolume volume,
    Texture2D<float4> irradianceAtlas, SamplerState probeSampler,
    uint probeTexSize, uint atlasWidth
) {
    float3 local = (worldPos - volume.origin) / volume.spacing;
    int3 baseProbe = int3(floor(local));
    float3 alpha = local - float3(baseProbe);

    float3 totalIrradiance = float3(0.0f);
    float totalWeight = 0.0f;

    // 8 surrounding probes (trilinear corners)
    [ForceUnroll]
    for (int z = 0; z < 2; ++z) {
        [ForceUnroll]
        for (int y = 0; y < 2; ++y) {
            [ForceUnroll]
            for (int x = 0; x < 2; ++x) {
                int3 probeCoord = baseProbe + int3(x, y, z);
                probeCoord = clamp(probeCoord, int3(0), int3(volume.probeCount) - 1);

                float3 probePos = probeWorldPosition(probeCoord, volume);
                float3 toProbe = normalize(probePos - worldPos);

                // Backface weight: reduce contribution from probes behind surface
                float backfaceWeight = max(0.0001f, dot(toProbe, surfaceNormal));

                // Trilinear weight
                float3 trilinear = float3(
                    x == 0 ? (1.0f - alpha.x) : alpha.x,
                    y == 0 ? (1.0f - alpha.y) : alpha.y,
                    z == 0 ? (1.0f - alpha.z) : alpha.z
                );
                float weight = trilinear.x * trilinear.y * trilinear.z * backfaceWeight;

                uint linearIdx = probeLinearIndex(probeCoord, volume.probeCount);
                float3 irradiance = sampleProbeIrradiance(
                    irradianceAtlas, probeSampler, linearIdx, surfaceNormal, probeTexSize, atlasWidth
                );

                totalIrradiance += irradiance * weight;
                totalWeight += weight;
            }
        }
    }

    return totalWeight > 0.0f ? totalIrradiance / totalWeight : float3(0.0f);
}
