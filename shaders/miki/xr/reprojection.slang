// xr/reprojection.slang — Late-latch reprojection for low-latency XR.
//
// Implements Asynchronous Time Warp (ATW) / Asynchronous Space Warp (ASW):
// - Rotational reprojection: compensates for head rotation between render and display.
// - Positional reprojection: uses depth buffer for parallax correction.
// - Motion extrapolation: predicts next frame for missed frame deadlines.
//
// This runs as the final pass before scanout, using the latest head pose
// from the VR runtime (late-latched after main rendering completes).
//
// Reference: Oculus ATW/ASW, SteamVR Motion Smoothing, Meta Quest reprojection.

implementing miki_xr;

import miki_core;

/// Reprojection mode.
public enum ReprojectionMode : uint {
    RotationOnly = 0,    // Rotation-only time warp (fast, no depth needed)
    PositionalDepth = 1, // Depth-based positional reprojection (better quality)
    MotionVector = 2,    // Motion vector extrapolation (ASW-style)
};

/// Reprojection parameters.
public struct ReprojectionParams {
    public float4x4 renderViewProj;    // View-proj used for the rendered frame
    public float4x4 displayViewProj;   // View-proj for the display time (late-latched)
    public float4x4 invRenderViewProj; // Inverse of render view-proj
    public float2   screenDimensions;
    public ReprojectionMode mode;
};

/// Pure rotational reprojection (time warp).
/// Remaps each pixel from render pose to display pose using homography.
/// renderUv: UV in the rendered frame.
/// Returns the corrected UV to sample from the rendered frame.
public float2 rotationalReprojection(float2 renderUv, ReprojectionParams params) {
    float2 ndc = renderUv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Unproject from render pose
    float4 worldDir = mul(params.invRenderViewProj, float4(ndc, 0.0, 1.0));
    worldDir.xyz /= worldDir.w;

    // Reproject to display pose
    float4 displayClip = mul(params.displayViewProj, float4(worldDir.xyz, 1.0));
    float2 displayNdc = displayClip.xy / displayClip.w;
    displayNdc.y = -displayNdc.y;
    return displayNdc * 0.5 + 0.5;
}

/// Depth-based positional reprojection.
/// Uses depth buffer to reconstruct world position and reproject to display pose.
/// renderUv: UV in the rendered frame.
/// depth: hardware depth value at this pixel.
public float2 positionalReprojection(float2 renderUv, float depth, ReprojectionParams params) {
    float2 ndc = renderUv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Reconstruct world position
    float4 worldPos = mul(params.invRenderViewProj, float4(ndc, depth, 1.0));
    worldPos.xyz /= worldPos.w;

    // Reproject to display pose
    float4 displayClip = mul(params.displayViewProj, float4(worldPos.xyz, 1.0));
    float2 displayNdc = displayClip.xy / displayClip.w;
    displayNdc.y = -displayNdc.y;
    return displayNdc * 0.5 + 0.5;
}

/// Detect disocclusion holes in reprojection.
/// When a pixel reprojects to a location that was hidden in the rendered frame,
/// it creates a hole that needs inpainting.
/// reprojectedUv: the reprojected UV.
/// Returns true if the UV is out of bounds (disoccluded).
public bool isDisoccluded(float2 reprojectedUv) {
    return any(reprojectedUv < 0.0) || any(reprojectedUv > 1.0);
}

/// Simple hole-filling: use nearest valid neighbor.
/// color: sampled color (may be invalid if disoccluded).
/// fallbackColor: color from a wider filter or previous frame.
/// isHole: true if this pixel needs inpainting.
public float4 fillDisocclusion(float4 color, float4 fallbackColor, bool isHole) {
    return isHole ? fallbackColor : color;
}

/// Compute reprojection confidence based on motion magnitude.
/// Large motion -> low confidence -> blend more with fallback.
public float reprojectionConfidence(float2 renderUv, float2 displayUv) {
    float motionMag = length(displayUv - renderUv);
    return saturate(1.0 - motionMag * 5.0);
}
