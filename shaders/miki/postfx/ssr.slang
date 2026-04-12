// postfx/ssr.slang — Screen-Space Reflections (implementing miki_postfx).
//
// Hi-Z ray march (Uludag 2014) for efficient screen-space tracing.
// DDA ray march fallback for lower tiers.

implementing miki_postfx;

import miki_core;

/// Hi-Z ray march result.
public struct SSRHitResult {
    public float2 hitUV;
    public float hitDepth;
    public bool hit;
    public uint iterationsUsed;
};

/// Hierarchical Z-buffer ray march for SSR.
/// Traces a ray from viewPos in reflectDir through the Hi-Z pyramid.
[ForceInline]
public SSRHitResult hiZRayMarch(
    Texture2D<float> hiZBuffer, SamplerState samp,
    float3 rayOriginVS, float3 rayDirVS,
    float4x4 projMatrix, float2 screenSize,
    uint maxIterations, uint maxMipLevel, float thickness
) {
    SSRHitResult result;
    result.hit = false;
    result.iterationsUsed = 0;

    // Project ray to screen space
    float4 startClip = mul(projMatrix, float4(rayOriginVS, 1.0f));
    float4 endClip = mul(projMatrix, float4(rayOriginVS + rayDirVS, 1.0f));
    float2 startUV = (startClip.xy / startClip.w) * 0.5f + 0.5f;
    float2 endUV = (endClip.xy / endClip.w) * 0.5f + 0.5f;

    float2 delta = endUV - startUV;
    float2 currentUV = startUV;
    uint mipLevel = maxMipLevel;

    for (uint i = 0; i < maxIterations; ++i) {
        result.iterationsUsed = i + 1;

        if (any(currentUV < float2(0.0f)) || any(currentUV > float2(1.0f))) break;

        float sceneDepth = hiZBuffer.SampleLevel(samp, currentUV, float(mipLevel)).r;
        float rayDepth = startClip.w + (endClip.w - startClip.w) * (float(i) / float(maxIterations));

        if (rayDepth > sceneDepth && rayDepth < sceneDepth + thickness) {
            result.hit = true;
            result.hitUV = currentUV;
            result.hitDepth = sceneDepth;
            break;
        }

        float stepSize = 1.0f / float(1u << mipLevel);
        currentUV += delta * stepSize;

        if (rayDepth > sceneDepth) {
            mipLevel = max(mipLevel - 1, 0u);
        } else if (mipLevel < maxMipLevel) {
            mipLevel++;
        }
    }

    return result;
}
