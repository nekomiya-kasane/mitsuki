// postfx/fxaa.slang — Fast Approximate Anti-Aliasing (implementing miki_postfx).
//
// FXAA 3.11 quality/performance tradeoff (Lottes 2009/2011).
// Operates on luma (green channel or perceptual luminance).

implementing miki_postfx;

import miki_core;

/// Compute perceptual luminance for FXAA edge detection.
[ForceInline]
public float fxaaLuma(float3 color) {
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

/// FXAA edge detection: returns true if local contrast exceeds threshold.
[ForceInline]
public bool fxaaIsEdge(float lumaN, float lumaS, float lumaE, float lumaW, float lumaM,
    float edgeThreshold, float edgeThresholdMin) {
    float range = max(max(lumaN, lumaS), max(lumaE, lumaW)) - min(min(lumaN, lumaS), min(lumaE, lumaW));
    return range >= max(edgeThresholdMin, lumaM * edgeThreshold);
}

/// Sub-pixel aliasing factor (controls how much sub-pixel detail is removed).
[ForceInline]
public float fxaaSubpixelFactor(float lumaN, float lumaS, float lumaE, float lumaW,
    float lumaNW, float lumaNE, float lumaSW, float lumaSE, float lumaM, float subpixelQuality) {
    float lumaAvg = (lumaN + lumaS + lumaE + lumaW) * 0.25f;
    float lumaAvgFull = (lumaN + lumaS + lumaE + lumaW + lumaNW + lumaNE + lumaSW + lumaSE) * 0.125f;
    float subpixelRange = abs(lumaAvgFull - lumaM);
    float range = max(max(lumaN, lumaS), max(lumaE, lumaW)) - min(min(lumaN, lumaS), min(lumaE, lumaW));
    if (range > 0.0f) {
        return saturate(subpixelRange / range) * subpixelQuality;
    }
    return 0.0f;
}
