// postfx/cas.slang — Contrast Adaptive Sharpening (implementing miki_postfx).
//
// AMD FidelityFX CAS — spatially adaptive sharpening that avoids
// over-sharpening bright/dark areas and haloing artifacts.

implementing miki_postfx;

import miki_core;

/// CAS sharpening filter. sharpness ∈ [0, 1], 0 = no sharpening, 1 = max.
[ForceInline]
public float3 casFilter(Texture2D<float4> src, int2 pos, float sharpness) {
    float3 a = src.Load(int3(pos + int2(0, -1), 0)).rgb;
    float3 b = src.Load(int3(pos + int2(-1, 0), 0)).rgb;
    float3 c = src.Load(int3(pos, 0)).rgb;
    float3 d = src.Load(int3(pos + int2(1, 0), 0)).rgb;
    float3 e = src.Load(int3(pos + int2(0, 1), 0)).rgb;

    // Per-channel min/max for adaptive sharpening
    float3 mnRGB = min(min(a, b), min(c, min(d, e)));
    float3 mxRGB = max(max(a, b), max(c, max(d, e)));

    // Sharpening amount inversely proportional to local contrast
    float3 rcpM = 1.0f / (mxRGB + float3(1e-5f));
    float3 amp = saturate(min(mnRGB, 2.0f - mxRGB) * rcpM);
    amp = sqrt(amp);

    float peak = -1.0f / lerp(8.0f, 5.0f, sharpness);
    float3 w = amp * peak;

    // Apply filter
    float3 rcpWeight = 1.0f / (1.0f + 4.0f * w);
    return saturate((a * w + b * w + c + d * w + e * w) * rcpWeight);
}
