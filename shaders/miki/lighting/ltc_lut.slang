// lighting/ltc_lut.slang — LTC lookup tables (implementing miki_lighting).
//
// Pre-computed LUT for Linearly Transformed Cosines (Heitz 2016).
// Two 64x64 RGBA textures: Minv matrix + Fresnel amplitude.

implementing miki_lighting;

import miki_core;

/// Fetch the LTC inverse matrix from the LUT.
/// u = NdotV, v = roughness. Returns column-major 3x3 matrix.
[ForceInline]
public float3x3 fetchLTCMatrix(Texture2D<float4> ltcMatLUT, SamplerState lutSampler, float NdotV, float roughness) {
    float4 t = ltcMatLUT.SampleLevel(lutSampler, float2(NdotV, roughness), 0.0f);
    // Matrix is stored as (a, 0, b, 0, 1, 0, c, 0, d) → 4 scalars + identity
    float3x3 Minv;
    Minv[0] = float3(t.x, 0.0f, t.y);
    Minv[1] = float3(0.0f, 1.0f, 0.0f);
    Minv[2] = float3(t.z, 0.0f, t.w);
    return Minv;
}

/// Fetch the LTC Fresnel amplitude from the LUT.
/// Returns (Fresnel scale, Fresnel bias) for the split-sum.
[ForceInline]
public float2 fetchLTCFresnel(Texture2D<float4> ltcFresnelLUT, SamplerState lutSampler, float NdotV, float roughness) {
    float4 t = ltcFresnelLUT.SampleLevel(lutSampler, float2(NdotV, roughness), 0.0f);
    return float2(t.x, t.y);
}
