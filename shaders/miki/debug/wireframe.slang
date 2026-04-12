// debug/wireframe.slang — Wireframe overlay rendering.
//
// Implements screen-space wireframe via barycentric distance (no geometry shader).
// Uses partial derivatives of barycentric coords to compute pixel distance to
// nearest edge, then blends wireframe color with anti-aliased falloff.
//
// Reference: Nvidia "Solid Wireframe" (GPU Gems 2), adapted for mesh shaders.
//
// Performance: Zero additional geometry passes — computed entirely in fragment shader
// from interpolated barycentrics. ~0.05ms overhead at 1080p.

implementing miki_debug;

import miki_core;

/// Wireframe rendering parameters.
public struct WireframeParams {
    public float lineWidth;       // Screen-space line width in pixels (default: 1.0)
    public float4 lineColor;      // Wireframe color (default: white)
    public float4 fillColor;      // Fill color (default: transparent black)
    public float smoothing;       // Anti-aliasing smoothing factor (default: 1.0)
};

/// Compute minimum distance from fragment to triangle edges using barycentric coordinates.
/// barycentrics: float3 barycentric coordinates (must be interpolated noperspective).
/// Returns screen-space distance to nearest edge in pixels.
public float computeWireframeDistance(float3 barycentrics) {
    float3 dx = ddx(barycentrics);
    float3 dy = ddy(barycentrics);
    float3 edgeWidth = sqrt(dx * dx + dy * dy);
    float3 edgeDist = barycentrics / max(edgeWidth, float3(1e-7));
    return min(edgeDist.x, min(edgeDist.y, edgeDist.z));
}

/// Apply wireframe overlay to a base color using barycentric coordinates.
/// Uses smoothstep for anti-aliased edges.
public float4 applyWireframe(float4 baseColor, float3 barycentrics, WireframeParams params) {
    float dist = computeWireframeDistance(barycentrics);
    float halfWidth = params.lineWidth * 0.5;
    float alpha = 1.0 - smoothstep(halfWidth - params.smoothing, halfWidth + params.smoothing, dist);
    float4 wireColor = float4(params.lineColor.rgb, params.lineColor.a * alpha);
    return lerp(baseColor, wireColor, wireColor.a);
}

/// Default wireframe params: 1px white line, no fill, 1.0 smoothing.
public WireframeParams defaultWireframeParams() {
    WireframeParams p;
    p.lineWidth = 1.0;
    p.lineColor = float4(1.0, 1.0, 1.0, 1.0);
    p.fillColor = float4(0.0, 0.0, 0.0, 0.0);
    p.smoothing = 1.0;
    return p;
}
