// shadow/csm.slang — Cascaded Shadow Maps (implementing miki_shadow).
//
// Splits the view frustum into N cascades with logarithmic-linear
// blend for depth distribution. Each cascade has its own light-space
// view-projection matrix.

implementing miki_shadow;

import miki_core;

/// Maximum supported cascade count.
public static const uint kMaxCascades = 4u;

/// Cascade parameters stored in a constant buffer.
public struct CascadeParams {
    public float4x4 lightViewProj[4];  // kMaxCascades
    public float4 splitDepths;          // cascade split distances (view space)
    public float2 shadowMapSize;        // per-cascade shadow map resolution
    public float cascadeBlendWidth;     // world-space blend transition
    public float normalBias;
};

/// Select which cascade to use based on fragment view-space depth.
[ForceInline]
public uint selectCascade(float viewDepth, float4 splitDepths) {
    // splitDepths.x/y/z/w = cascade boundaries
    uint cascade = 0;
    if (viewDepth > splitDepths.x) cascade = 1;
    if (viewDepth > splitDepths.y) cascade = 2;
    if (viewDepth > splitDepths.z) cascade = 3;
    return min(cascade, kMaxCascades - 1u);
}

/// Compute the blend factor between two adjacent cascades.
/// Returns 0 = use current cascade, 1 = use next cascade.
[ForceInline]
public float computeCascadeBlend(float viewDepth, float splitDepth, float blendWidth) {
    if (blendWidth <= 0.0f) return 0.0f;
    return saturate((viewDepth - splitDepth + blendWidth) / blendWidth);
}

/// Transform world position to shadow map UV + depth for a given cascade.
[ForceInline]
public float3 worldToShadowUV(float3 worldPos, float4x4 lightViewProj) {
    float4 lightClip = mul(lightViewProj, float4(worldPos, 1.0f));
    float3 ndc = lightClip.xyz / lightClip.w;
    // Map from [-1,1] to [0,1] UV space
    return float3(ndc.xy * 0.5f + 0.5f, ndc.z);
}

/// Apply normal-based bias to reduce shadow acne.
[ForceInline]
public float3 applyNormalBias(float3 worldPos, float3 worldNormal, float3 lightDir, float bias) {
    float nDotL = dot(worldNormal, lightDir);
    float scaleFactor = 1.0f - saturate(nDotL);
    return worldPos + worldNormal * (bias * scaleFactor);
}
