// postfx/color_grade.slang — Color grading / LUT (implementing miki_postfx).
//
// 3D LUT-based color grading and procedural adjustments
// (saturation, contrast, gamma, lift/gamma/gain).

implementing miki_postfx;

import miki_core;

/// Apply 3D color LUT. lutSize = dimension (e.g. 32 for 32^3 LUT).
[ForceInline]
public float3 applyColorLUT(Texture3D<float4> lut, SamplerState samp, float3 color, float lutSize) {
    float scale = (lutSize - 1.0f) / lutSize;
    float offset = 0.5f / lutSize;
    return lut.SampleLevel(samp, color * scale + float3(offset), 0).rgb;
}

/// Saturation adjustment. amount = 0 (grayscale) to 2+ (oversaturated).
[ForceInline]
public float3 adjustSaturation(float3 color, float amount) {
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    return lerp(float3(luma), color, amount);
}

/// Contrast adjustment around midpoint (0.5 for LDR).
[ForceInline]
public float3 adjustContrast(float3 color, float contrast, float midpoint) {
    return (color - float3(midpoint)) * contrast + float3(midpoint);
}

/// Lift/Gamma/Gain color correction (ASC CDL).
[ForceInline]
public float3 liftGammaGain(float3 color, float3 lift, float3 gamma, float3 gain) {
    float3 result = color * gain + lift;
    result = max(result, float3(0.0f));
    return pow(result, 1.0f / max(gamma, float3(1e-5f)));
}
