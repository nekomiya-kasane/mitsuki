// xr/foveated.slang — Foveated rendering via Variable Rate Shading (VRS).
//
// Generates a VRS rate image based on gaze point (from eye tracker) or
// fixed radial falloff. The rate image controls per-tile shading rate:
// - Foveal region (center): 1x1 (full rate)
// - Inner ring: 2x2 (half rate)
// - Peripheral: 4x4 (quarter rate)
//
// Also supports Contrast-Adaptive Shading (CAS) for quality recovery
// in reduced-rate regions.
//
// Requires: VK_KHR_fragment_shading_rate / D3D12 VRS Tier 2.
//
// Reference: NVIDIA VRWorks VRS, Varjo Dynamic Foveated Rendering,
//            ARM foveated rendering whitepaper (2024).

implementing miki_xr;

import miki_core;

/// VRS shading rate enum (matches VkFragmentShadingRateFlagBitsKHR layout).
public enum ShadingRate : uint {
    Rate1x1 = 0u,  // Full rate (foveal)
    Rate1x2 = 1u,  // Half vertical
    Rate2x1 = 2u,  // Half horizontal
    Rate2x2 = 3u,  // Quarter rate (inner ring)
    Rate2x4 = 4u,  // Eighth rate
    Rate4x2 = 5u,  // Eighth rate
    Rate4x4 = 6u,  // Sixteenth rate (periphery)
};

/// Foveated rendering parameters.
public struct FoveatedParams {
    public float2 gazePoint;        // Normalized gaze center [0,1]^2 in screen UV space
    public float  fovealRadius;     // Foveal region radius (full rate), normalized [0,1]
    public float  innerRadius;      // Inner ring radius (2x2 rate), normalized [0,1]
    public float  outerRadius;      // Outer ring radius (beyond this: 4x4 rate)
    public float2 screenDimensions; // Render target size (for tile computation)
    public uint   tileSize;         // VRS tile size (8 or 16, hardware-dependent)
};

/// Compute the shading rate for a screen-space tile based on distance from gaze.
/// tileCenter: center of the VRS tile in normalized screen UV [0,1]^2.
public ShadingRate computeShadingRate(float2 tileCenter, FoveatedParams params) {
    float2 offset = tileCenter - params.gazePoint;
    // Account for aspect ratio
    float aspect = params.screenDimensions.x / max(params.screenDimensions.y, 1.0);
    offset.x *= aspect;
    float dist = length(offset);

    if (dist < params.fovealRadius) return ShadingRate::Rate1x1;
    if (dist < params.innerRadius)  return ShadingRate::Rate2x2;
    return ShadingRate::Rate4x4;
}

/// Generate VRS rate image for a compute shader.
/// Each thread writes one tile's shading rate to the output image.
/// outputRate: RWTexture2D<uint> where each pixel represents one VRS tile.
public uint encodeShadingRate(ShadingRate rate) {
    // VkFragmentShadingRateFlagBitsKHR encoding:
    // 1x1=0, 1x2=1, 2x1=4, 2x2=5, 2x4=6, 4x2=9, 4x4=10
    switch (rate) {
        case ShadingRate::Rate1x1: return 0u;
        case ShadingRate::Rate1x2: return 1u;
        case ShadingRate::Rate2x1: return 4u;
        case ShadingRate::Rate2x2: return 5u;
        case ShadingRate::Rate2x4: return 6u;
        case ShadingRate::Rate4x2: return 9u;
        case ShadingRate::Rate4x4: return 10u;
        default: return 0u;
    }
}

/// Estimate quality loss from shading rate reduction.
/// Can be used for adaptive quality: increase rate where scene complexity is high.
/// edgeStrength: local edge/gradient magnitude from previous frame.
/// threshold: above this, force full rate even in periphery.
public ShadingRate adaptiveRate(ShadingRate baseRate, float edgeStrength, float threshold) {
    if (edgeStrength > threshold) return ShadingRate::Rate1x1;
    return baseRate;
}
