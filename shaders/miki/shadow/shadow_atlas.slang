// shadow/shadow_atlas.slang — Shadow atlas management (implementing miki_shadow).
//
// All punctual light shadows packed into a single large atlas texture.
// Each light's shadow map occupies a tile (rect) within the atlas.
// Tile allocation is managed by the CPU; this file provides GPU-side
// UV remapping from local shadow map space to atlas UV.

implementing miki_shadow;

import miki_core;

/// Shadow atlas tile descriptor. CPU-side allocator fills these.
public struct ShadowAtlasTile {
    public float2 uvOffset;   // top-left corner in atlas UV space [0,1]
    public float2 uvScale;    // tile size in atlas UV space
    public float4x4 lightViewProj;  // light-space transform for this tile
    public float depthBias;
    public float normalBias;
    public float _pad0;
    public float _pad1;
};

/// Map local shadow UV [0,1]^2 to atlas UV.
[ForceInline]
public float2 localToAtlasUV(float2 localUV, ShadowAtlasTile tile) {
    return tile.uvOffset + localUV * tile.uvScale;
}

/// Sample shadow atlas with PCF 2x2 (hardware bilinear comparison).
[ForceInline]
public float sampleShadowAtlasPCF(
    Texture2D<float> shadowAtlas, SamplerComparisonState shadowSampler,
    float2 atlasUV, float refDepth
) {
    return shadowAtlas.SampleCmpLevelZero(shadowSampler, atlasUV, refDepth);
}

/// Sample shadow atlas with 3x3 PCF for softer shadows.
[ForceInline]
public float sampleShadowAtlasPCF3x3(
    Texture2D<float> shadowAtlas, SamplerComparisonState shadowSampler,
    float2 atlasUV, float refDepth, float2 texelSize
) {
    float shadow = 0.0f;
    [ForceUnroll]
    for (int y = -1; y <= 1; ++y) {
        [ForceUnroll]
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(float(x), float(y)) * texelSize;
            shadow += shadowAtlas.SampleCmpLevelZero(shadowSampler, atlasUV + offset, refDepth);
        }
    }
    return shadow / 9.0f;
}
