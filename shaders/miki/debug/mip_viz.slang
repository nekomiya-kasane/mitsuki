// debug/mip_viz.slang — Texture mip level visualization.
//
// Colors fragments by which mip level is being sampled, revealing texture
// streaming issues, UV density problems, and minification/magnification.
//
// Technique: Use ddx/ddy of UVs to compute the implicit mip level, then
// map to a discrete color palette matching standard debug mip views.
//
// Reference: RenderDoc mip overlay, NVIDIA Nsight texture mip visualization.

implementing miki_debug;

import miki_core;

/// Compute the implicit mip level from UV derivatives and texture dimensions.
/// uv: texture coordinates.
/// texDimensions: int2(width, height) of the base mip.
public float computeMipLevel(float2 uv, int2 texDimensions) {
    float2 texelCoord = uv * float2(texDimensions);
    float2 dx = ddx(texelCoord);
    float2 dy = ddy(texelCoord);
    float maxDelta = max(dot(dx, dx), dot(dy, dy));
    return 0.5 * log2(max(maxDelta, 1.0));
}

/// Standard mip level color palette (up to 12 levels).
/// Each mip level gets a distinct, perceptually separable color.
public float3 mipLevelColor(uint mipLevel) {
    // 12-level palette: same convention as RenderDoc/PIX
    switch (mipLevel) {
        case 0u:  return float3(1.0, 0.0, 0.0);   // Red:        Mip 0 (base, likely magnified)
        case 1u:  return float3(1.0, 0.5, 0.0);   // Orange:     Mip 1
        case 2u:  return float3(1.0, 1.0, 0.0);   // Yellow:     Mip 2
        case 3u:  return float3(0.5, 1.0, 0.0);   // Lime:       Mip 3
        case 4u:  return float3(0.0, 1.0, 0.0);   // Green:      Mip 4 (ideal for many scenes)
        case 5u:  return float3(0.0, 1.0, 0.5);   // Teal:       Mip 5
        case 6u:  return float3(0.0, 1.0, 1.0);   // Cyan:       Mip 6
        case 7u:  return float3(0.0, 0.5, 1.0);   // Azure:      Mip 7
        case 8u:  return float3(0.0, 0.0, 1.0);   // Blue:       Mip 8
        case 9u:  return float3(0.5, 0.0, 1.0);   // Violet:     Mip 9
        case 10u: return float3(1.0, 0.0, 1.0);   // Magenta:    Mip 10
        case 11u: return float3(1.0, 0.0, 0.5);   // Rose:       Mip 11
        default:  return float3(0.5, 0.5, 0.5);   // Gray:       Mip 12+
    }
}

/// Visualize the mip level being sampled for a texture lookup.
/// Returns a blended color between the two nearest integer mip levels.
public float3 visualizeMipLevel(float2 uv, int2 texDimensions) {
    float mip = computeMipLevel(uv, texDimensions);
    uint mipFloor = uint(max(mip, 0.0));
    uint mipCeil = mipFloor + 1u;
    float frac_ = frac(max(mip, 0.0));
    return lerp(mipLevelColor(mipFloor), mipLevelColor(mipCeil), frac_);
}

/// Check if texture is being magnified (mip < 0, i.e. pixel covers less than 1 texel).
public bool isMagnified(float2 uv, int2 texDimensions) {
    return computeMipLevel(uv, texDimensions) < 0.0;
}

/// Checker pattern overlay to visualize texel density.
/// Returns 0 or 1 in an alternating pattern at the given mip level scale.
public float texelDensityChecker(float2 uv, int2 texDimensions, uint targetMip) {
    float2 texelCoord = uv * float2(texDimensions) / float(1u << targetMip);
    int2 cell = int2(floor(texelCoord));
    return float((cell.x + cell.y) & 1);
}
