// postfx/taa.slang — Temporal Anti-Aliasing (implementing miki_postfx).
//
// Variance-clipping TAA (Salvi 2016 / Karis 2014).
// Jittered sub-pixel sampling + neighborhood clamping for stability.

implementing miki_postfx;

import miki_core;

/// Compute Halton(2,3) jitter for frame index. Returns sub-pixel offset in [-0.5, 0.5].
[ForceInline]
public float2 computeJitter(uint frameIndex) {
    // Halton base-2
    float h2 = 0.0f;
    float invBase2 = 0.5f;
    uint idx = frameIndex;
    while (idx > 0u) {
        h2 += float(idx & 1u) * invBase2;
        idx >>= 1u;
        invBase2 *= 0.5f;
    }
    // Halton base-3
    float h3 = 0.0f;
    float invBase3 = 1.0f / 3.0f;
    idx = frameIndex;
    while (idx > 0u) {
        h3 += float(idx % 3u) * invBase3;
        idx /= 3u;
        invBase3 /= 3.0f;
    }
    return float2(h2 - 0.5f, h3 - 0.5f);
}

/// Neighborhood variance clipping (Salvi 2016).
/// Clamps history color to the AABB defined by current frame's 3x3 neighborhood stats.
[ForceInline]
public float3 clipToAABB(float3 history, float3 aabbMin, float3 aabbMax) {
    float3 center = (aabbMin + aabbMax) * 0.5f;
    float3 extent = (aabbMax - aabbMin) * 0.5f + float3(1e-5f);
    float3 offset = history - center;
    float3 ts = abs(offset / extent);
    float t = max(ts.x, max(ts.y, ts.z));
    if (t > 1.0f) {
        return center + offset / t;
    }
    return history;
}

/// Compute blend factor for TAA feedback. Lower = more history (smoother but ghostier).
[ForceInline]
public float computeTAABlendFactor(float velocityLength, float baseBlend, float velocityScale) {
    return clamp(baseBlend + velocityLength * velocityScale, baseBlend, 1.0f);
}
