// core/color_space.slang — Color space conversion utilities (implementing miki_core).

implementing miki_core;

/// sRGB gamma curve (single channel): linear -> sRGB.
public float linearToSrgb(float c) {
    return (c <= 0.0031308) ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

/// sRGB inverse gamma curve (single channel): sRGB -> linear.
public float srgbToLinear(float c) {
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/// sRGB -> linear (RGB, alpha pass-through).
public float4 srgbToLinear(float4 color) {
    return float4(srgbToLinear(color.r), srgbToLinear(color.g), srgbToLinear(color.b), color.a);
}

/// Linear -> sRGB (RGB, alpha pass-through).
public float4 linearToSrgb(float4 color) {
    return float4(linearToSrgb(color.r), linearToSrgb(color.g), linearToSrgb(color.b), color.a);
}

// --- Stubs for future phases ---

/// ACES filmic tone mapping (Phase 3a).
public float3 acesFilmic(float3 color) {
    // Stub: identity pass-through until Phase 3a PBR pipeline
    return color;
}

/// Rec.2020 -> Rec.709 conversion (Phase 3a HDR support).
public float3 rec2020ToRec709(float3 color) {
    // Stub: identity pass-through until Phase 3a HDR pipeline
    return color;
}
