// cad/measure.slang — GPU measurement utilities for CAD.
//
// Provides GPU-side computation of distances, angles, and areas for
// interactive CAD measurement tools. Uses reconstructed world positions
// from the depth buffer or visibility buffer.
//
// All measurements operate in world space with configurable precision.
//
// Reference: HOOPS Visualize measurement API, Autodesk Viewer measure tool.

implementing miki_cad;

import miki_core;

/// Reconstruct world position from screen UV and depth.
/// uv: normalized screen coordinates [0,1].
/// depth: hardware depth value [0,1].
/// invViewProj: inverse of view-projection matrix.
public float3 reconstructWorldPosition(float2 uv, float depth, float4x4 invViewProj) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Vulkan Y-flip
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

/// Point-to-point distance in world space.
public float measureDistance(float3 p0, float3 p1) {
    return length(p1 - p0);
}

/// Angle between two vectors (in radians).
public float measureAngle(float3 v0, float3 v1) {
    float d = dot(normalize(v0), normalize(v1));
    return acos(clamp(d, -1.0, 1.0));
}

/// Point-to-line (segment) minimum distance.
/// p: query point. a, b: line segment endpoints.
public float pointToSegmentDistance(float3 p, float3 a, float3 b) {
    float3 ab = b - a;
    float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-10));
    float3 closest = a + ab * t;
    return length(p - closest);
}

/// Point-to-plane distance (signed).
public float pointToPlaneDistance(float3 p, float3 planeNormal, float planeDist) {
    return dot(planeNormal, p) + planeDist;
}

/// Triangle area from 3 vertices (world space).
public float triangleArea(float3 v0, float3 v1, float3 v2) {
    return 0.5 * length(cross(v1 - v0, v2 - v0));
}

/// Compute radius of a circle passing through 3 points (circumradius).
/// Useful for fillet radius measurement.
public float circumradius(float3 p0, float3 p1, float3 p2) {
    float a = length(p1 - p0);
    float b = length(p2 - p1);
    float c = length(p0 - p2);
    float area = triangleArea(p0, p1, p2);
    return (a * b * c) / max(4.0 * area, 1e-10);
}

/// Project a measurement line to screen space for annotation rendering.
/// Returns screen-space position for label placement (midpoint of p0-p1).
public float2 projectToScreen(float3 worldPos, float4x4 viewProj, float2 screenDim) {
    float4 clipPos = mul(viewProj, float4(worldPos, 1.0));
    float2 ndc = clipPos.xy / clipPos.w;
    float2 screen = (ndc * 0.5 + 0.5) * screenDim;
    screen.y = screenDim.y - screen.y; // Flip Y
    return screen;
}
