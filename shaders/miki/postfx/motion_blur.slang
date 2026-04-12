// postfx/motion_blur.slang — Per-pixel motion blur (implementing miki_postfx).
//
// Velocity buffer-based motion blur with tile max optimization.
// McGuire 2012: A reconstruction filter for plausible motion blur.

implementing miki_postfx;

import miki_core;

/// Compute per-pixel velocity from current and previous frame clip-space positions.
[ForceInline]
public float2 computeVelocity(float4 currentClip, float4 prevClip) {
    float2 currentNDC = currentClip.xy / currentClip.w;
    float2 prevNDC = prevClip.xy / prevClip.w;
    return (currentNDC - prevNDC) * 0.5f;  // NDC to UV space
}

/// Clamp velocity length to max blur radius (in UV space).
[ForceInline]
public float2 clampVelocity(float2 velocity, float maxLengthUV) {
    float len = length(velocity);
    if (len > maxLengthUV && len > 0.0f) {
        return velocity * (maxLengthUV / len);
    }
    return velocity;
}

/// Compute tile-max velocity (downsample velocity buffer by tile size).
/// Used to skip tiles with zero/negligible motion.
[ForceInline]
public float2 selectDominantVelocity(float2 v0, float2 v1) {
    return (dot(v0, v0) > dot(v1, v1)) ? v0 : v1;
}
