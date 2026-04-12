// lighting/ibl.slang — Image-Based Lighting (implementing miki_lighting).
//
// Split-sum approximation for specular IBL (Karis 2013).
// Diffuse irradiance from pre-filtered cubemap or SH coefficients.

implementing miki_lighting;

import miki_core;
import miki_math;

/// Sample the pre-filtered environment map for specular IBL.
/// roughness maps to mip level.
[ForceInline]
public float3 samplePrefilteredEnvMap(TextureCube envMap, SamplerState envSampler, float3 reflDir, float roughness, float maxMip) {
    float mip = roughness * maxMip;
    return envMap.SampleLevel(envSampler, reflDir, mip).rgb;
}

/// Sample the BRDF integration LUT (split-sum second term).
/// Input: NdotV, roughness. Output: (scale, bias) for F0 * scale + bias.
[ForceInline]
public float2 sampleBRDFLUT(Texture2D<float2> brdfLUT, SamplerState lutSampler, float NdotV, float roughness) {
    return brdfLUT.SampleLevel(lutSampler, float2(NdotV, roughness), 0.0f);
}

/// Evaluate specular IBL using split-sum approximation.
[ForceInline]
public float3 evaluateSpecularIBL(
    float3 F0, float roughness, float NdotV, float3 reflDir,
    TextureCube envMap, SamplerState envSampler, float maxMip,
    Texture2D<float2> brdfLUT, SamplerState lutSampler
) {
    float3 prefilteredColor = samplePrefilteredEnvMap(envMap, envSampler, reflDir, roughness, maxMip);
    float2 brdf = sampleBRDFLUT(brdfLUT, lutSampler, NdotV, roughness);
    return prefilteredColor * (F0 * brdf.x + brdf.y);
}

/// Evaluate diffuse irradiance from a pre-convolved irradiance cubemap.
[ForceInline]
public float3 evaluateDiffuseIBL(TextureCube irradianceMap, SamplerState sampler, float3 normal) {
    return irradianceMap.SampleLevel(sampler, normal, 0.0f).rgb;
}
