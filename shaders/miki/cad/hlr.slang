// cad/hlr.slang — GPU Hidden Line Removal for CAD wireframe rendering.
//
// Implements edge-based HLR using the visibility buffer:
// 1. Render solid geometry to depth buffer (reuse DepthPrePass).
// 2. For each edge segment, project to screen space and test against depth buffer.
// 3. Classify each edge pixel as visible, hidden, or tangent (silhouette).
// 4. Output visible edges as solid lines, hidden edges as dashed/dimmed.
//
// This approach is an order of magnitude faster than CPU-based HLR (HOOPS/OpenCascade)
// for large assemblies (>1M edges): GPU parallelism + depth buffer occlusion query.
//
// Reference: HOOPS Visualize GPU HLR, OpenCascade StdPrs_HLRToolShape.
// Performance target: < 2ms for 100K edges at 1080p.

implementing miki_cad;

import miki_core;

/// Edge classification result.
public enum EdgeVisibility : uint {
    Visible = 0,     // Edge is fully visible (draw solid)
    Hidden = 1,      // Edge is occluded by geometry (draw dashed or skip)
    Tangent = 2,     // Edge is a silhouette/tangent edge (draw solid, thinner)
    Boundary = 3,    // Edge is on a boundary/free edge (draw solid, thick)
};

/// HLR parameters.
public struct HlrParams {
    public float4x4 viewProj;         // View-projection matrix
    public float2 screenDimensions;   // Render target dimensions
    public float depthBias;           // Bias for depth comparison (default: 0.001)
    public float tangentThreshold;    // Dot product threshold for tangent edges (default: 0.05)
};

/// Test if a world-space point is occluded by the depth buffer.
/// Returns true if the point is behind the depth buffer at that pixel.
public bool isOccluded(
    Texture2D<float> depthBuffer,
    SamplerState pointSampler,
    float3 worldPos,
    HlrParams params
) {
    float4 clipPos = mul(params.viewProj, float4(worldPos, 1.0));
    float3 ndc = clipPos.xyz / clipPos.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y; // Flip Y for Vulkan
    if (any(uv < 0.0) || any(uv > 1.0)) return true; // Off-screen
    float sceneDepth = depthBuffer.SampleLevel(pointSampler, uv, 0).r;
    return ndc.z > sceneDepth + params.depthBias;
}

/// Classify an edge based on adjacent face normals and camera direction.
/// faceNormal0, faceNormal1: normals of the two faces sharing this edge.
///   For boundary edges, set faceNormal1 = float3(0).
/// viewDir: direction from edge midpoint to camera (normalized).
public EdgeVisibility classifyEdge(
    float3 faceNormal0,
    float3 faceNormal1,
    float3 viewDir,
    float tangentThreshold
) {
    bool hasFace1 = dot(faceNormal1, faceNormal1) > 0.001;
    if (!hasFace1) return EdgeVisibility::Boundary;

    float dot0 = dot(faceNormal0, viewDir);
    float dot1 = dot(faceNormal1, viewDir);

    // Silhouette: one face front-facing, one back-facing
    if (dot0 * dot1 < 0.0) return EdgeVisibility::Tangent;
    // Tangent edge: nearly perpendicular to view
    if (abs(dot0) < tangentThreshold || abs(dot1) < tangentThreshold) return EdgeVisibility::Tangent;
    // Both front-facing -> visible
    if (dot0 > 0.0 && dot1 > 0.0) return EdgeVisibility::Visible;
    // Both back-facing -> hidden
    return EdgeVisibility::Hidden;
}

/// Compute dashed line pattern for hidden edges.
/// screenDist: distance along the edge in screen pixels.
/// dashLength: length of dash in pixels (default: 8).
/// gapLength: length of gap in pixels (default: 4).
/// Returns 1.0 for dash, 0.0 for gap.
public float dashedLinePattern(float screenDist, float dashLength, float gapLength) {
    float period = dashLength + gapLength;
    float phase = fmod(screenDist, period);
    return phase < dashLength ? 1.0 : 0.0;
}
