// postfx/ssao.slang — Screen-Space Ambient Occlusion (implementing miki_postfx).
//
// Classic hemisphere-based SSAO (Starcraft II / Crytek variant).
// Included for Compat tier and as fallback when GTAO is not available.

implementing miki_postfx;

import miki_core;

/// SSAO parameters.
public struct SSAOParams {
    public float radius;        // view-space hemisphere radius
    public float bias;          // depth bias to reduce self-occlusion
    public float power;         // AO power exponent
    public int sampleCount;     // number of hemisphere samples (16-64)
};

/// Compute SSAO for a single pixel using kernel samples.
/// kernelSamples: array of hemisphere sample directions (pre-generated).
[ForceInline]
public float computeSSAO(
    Texture2D<float> depthBuffer, SamplerState samp,
    float2 uv, float3 viewPos, float3 viewNormal,
    float4x4 projMatrix, float2 screenSize,
    SSAOParams params, StructuredBuffer<float4> kernelSamples,
    float2 noise2D
) {
    // Build TBN from noise-rotated normal
    float3 tangent = normalize(float3(noise2D.x, noise2D.y, 0.0f) - viewNormal * dot(float3(noise2D.x, noise2D.y, 0.0f), viewNormal));
    float3 bitangent = cross(viewNormal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, viewNormal);

    float occlusion = 0.0f;
    for (int i = 0; i < params.sampleCount; ++i) {
        float3 sampleDir = mul(TBN, kernelSamples[i].xyz);
        float3 samplePos = viewPos + sampleDir * params.radius;

        // Project to screen
        float4 offset = mul(projMatrix, float4(samplePos, 1.0f));
        float2 sampleUV = (offset.xy / offset.w) * 0.5f + 0.5f;

        float sampleDepth = depthBuffer.SampleLevel(samp, sampleUV, 0).r;
        float linearSample = projMatrix[3][2] / (sampleDepth - projMatrix[2][2]);

        float rangeCheck = smoothstep(0.0f, 1.0f, params.radius / max(abs(viewPos.z - linearSample), 1e-5f));
        occlusion += ((linearSample >= samplePos.z + params.bias) ? 1.0f : 0.0f) * rangeCheck;
    }

    occlusion = 1.0f - occlusion / float(params.sampleCount);
    return pow(occlusion, params.power);
}
