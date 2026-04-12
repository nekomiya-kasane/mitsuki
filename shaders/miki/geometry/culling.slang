// geometry/culling.slang — GPU culling functions (implementing miki_geometry).
//
// 3-level hierarchical culling:
//   Level 1: Instance/meshlet frustum + occlusion (two-pass, Nanite-style)
//   Level 2: Meshlet cone cull
//   Level 3: Per-triangle backface + degenerate + small-tri + micro-frustum
//
// Pure functions here are CPU-testable via host-callable target.

implementing miki_geometry;

import miki_core;

/// Frustum cull a bounding sphere against 6 planes.
/// Returns true if the sphere is OUTSIDE (should be culled).
[ForceInline]
public bool frustumCullSphere(float3 center, float radius, float4 planes[6]) {
    [ForceUnroll]
    for (int i = 0; i < 6; ++i) {
        float dist = dot(planes[i].xyz, center) + planes[i].w;
        if (dist < -radius) return true;
    }
    return false;
}

/// Cone cull a meshlet. Returns true if the meshlet is BACKFACING (should be culled).
/// Uses meshoptimizer's cone culling formula.
[ForceInline]
public bool coneCullMeshlet(float3 coneApex, float3 coneAxis, float coneCutoff, float3 eyePos) {
    float3 toEye = normalize(eyePos - coneApex);
    return dot(toEye, coneAxis) < coneCutoff;
}

/// Backface-cull a triangle in clip space. Returns true if BACKFACING.
/// Uses 2D cross product of edges in NDC.
[ForceInline]
public bool cullTriangleBackface(float4 v0, float4 v1, float4 v2) {
    float2 ndc0 = v0.xy / v0.w;
    float2 ndc1 = v1.xy / v1.w;
    float2 ndc2 = v2.xy / v2.w;
    float cross2d = (ndc1.x - ndc0.x) * (ndc2.y - ndc0.y) - (ndc1.y - ndc0.y) * (ndc2.x - ndc0.x);
    return cross2d <= 0.0f;  // CW winding = backface
}

/// Small-triangle cull: reject if all 3 NDC edges are shorter than 1 pixel.
/// Returns true if the triangle is TOO SMALL (should be culled).
[ForceInline]
public bool isSmallTriangle(float2 ndc0, float2 ndc1, float2 ndc2, float2 viewportSize) {
    float2 pixel0 = ndc0 * viewportSize * 0.5f;
    float2 pixel1 = ndc1 * viewportSize * 0.5f;
    float2 pixel2 = ndc2 * viewportSize * 0.5f;

    float2 e0 = pixel1 - pixel0;
    float2 e1 = pixel2 - pixel1;
    float2 e2 = pixel0 - pixel2;

    float area2 = abs(e0.x * e1.y - e0.y * e1.x);
    return area2 < 1.0f;  // Sub-pixel triangle
}

/// Degenerate triangle check (indices).
[ForceInline]
public bool isDegenerate(uint i0, uint i1, uint i2) {
    return i0 == i1 || i1 == i2 || i0 == i2;
}
