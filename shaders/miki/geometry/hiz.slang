// geometry/hiz.slang — Hierarchical Z-buffer (implementing miki_geometry).
//
// Two-pass occlusion culling (Nanite-style):
//   Pass A: Test against previous frame's HiZ → build current HiZ from survivors
//   Pass B: Re-test rejected geometry against current HiZ
// Adds ~0.1ms but eliminates 10-30% false negatives in dynamic scenes.

implementing miki_geometry;

import miki_core;

/// Sample HiZ mip at the given UV and mip level. Returns the conservative depth.
[ForceInline]
public float sampleHiZ(Texture2D<float> hizTexture, SamplerState hizSampler, float2 uv, uint mipLevel) {
    return hizTexture.SampleLevel(hizSampler, uv, float(mipLevel));
}

/// Compute the appropriate HiZ mip level for a given screen-space AABB.
[ForceInline]
public uint computeHiZMip(float2 minNDC, float2 maxNDC, float2 hizSize) {
    float2 sizePixels = (maxNDC - minNDC) * hizSize * 0.5f;
    float maxDim = max(sizePixels.x, sizePixels.y);
    return uint(ceil(log2(max(maxDim, 1.0f))));
}

/// Occlusion cull a bounding sphere against the HiZ pyramid.
/// Returns true if the sphere is OCCLUDED (should be culled).
[ForceInline]
public bool occlusionCullSphere(
    float3 center, float radius, float4x4 viewProj,
    Texture2D<float> hizTexture, SamplerState hizSampler, float2 hizSize
) {
    // Project sphere to screen-space AABB
    float4 clip = mul(viewProj, float4(center, 1.0f));
    if (clip.w <= 0.0f) return false;  // Behind camera, don't cull (let frustum handle it)

    float3 ndc = clip.xyz / clip.w;
    float projRadius = radius / clip.w;

    float2 minNDC = ndc.xy - projRadius;
    float2 maxNDC = ndc.xy + projRadius;

    // Clamp to NDC range [-1, 1]
    minNDC = clamp(minNDC, float2(-1.0f), float2(1.0f));
    maxNDC = clamp(maxNDC, float2(-1.0f), float2(1.0f));

    // Convert to UV [0, 1]
    float2 minUV = minNDC * 0.5f + 0.5f;
    float2 maxUV = maxNDC * 0.5f + 0.5f;
    float2 centerUV = (minUV + maxUV) * 0.5f;

    uint mip = computeHiZMip(minNDC, maxNDC, hizSize);
    float hizDepth = sampleHiZ(hizTexture, hizSampler, centerUV, mip);

    // Reversed-Z: objects are occluded if their nearest depth > HiZ depth
    float nearestDepth = ndc.z - projRadius;
    return nearestDepth > hizDepth;
}
