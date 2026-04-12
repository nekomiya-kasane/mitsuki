// postfx/tonemap.slang — Tonemapping operators (implementing miki_postfx).
//
// ACES, AgX, Khronos PBR Neutral, Reinhard, exposure adaptation.
// Default: Khronos PBR Neutral (2024) — perceptually uniform, no hue shift.

implementing miki_postfx;

import miki_core;

/// Reinhard tonemapping (simple luminance-based).
[ForceInline]
public float3 tonemapReinhard(float3 color) {
    return color / (1.0f + color);
}

/// ACES Filmic Tonemapping (Narkowicz 2015 approximation).
[ForceInline]
public float3 tonemapACES(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

/// AgX tonemapping (Troy Sobotka 2023). Better hue preservation than ACES.
[ForceInline]
public float3 tonemapAgX(float3 color) {
    // AgX log encoding
    float3 encoded = max(color, float3(1e-10f));
    encoded = log2(encoded);
    encoded = (encoded - (-12.47393f)) / (4.026069f - (-12.47393f));
    encoded = clamp(encoded, float3(0.0f), float3(1.0f));
    // AgX Base contrast
    float3 x = encoded;
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

/// Khronos PBR Neutral tonemapping (2024). No hue shift, perceptually uniform.
[ForceInline]
public float3 tonemapPBRNeutral(float3 color) {
    float startCompression = 0.8f - 0.04f;
    float desaturation = 0.15f;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    float newPeak = 1.0f - (1.0f - startCompression) / (1.0f + (peak - startCompression) / (1.0f - startCompression));
    color *= newPeak / peak;
    float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return lerp(color, float3(newPeak), g);
}

/// Apply exposure adjustment (EV100-based).
[ForceInline]
public float3 applyExposure(float3 color, float exposureEV) {
    return color * exp2(exposureEV);
}
