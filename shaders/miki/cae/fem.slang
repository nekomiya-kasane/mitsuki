// cae/fem.slang — FEM mesh rendering and contour plot visualization.
//
// Renders finite element meshes with per-element or per-node scalar values
// mapped to a colormap (transfer function). Supports:
// - Linear/quadratic element interpolation
// - Contour lines (isolines) at specified scalar intervals
// - Wireframe overlay for mesh topology visualization
// - Deformed mesh rendering with displacement field
//
// Reference: ParaView surface LIC, VTK contouring, Ansys Mechanical post-processor.

implementing miki_cae;

import miki_core;

/// FEM element type for interpolation.
public enum FemElementType : uint {
    Triangle = 0,     // Linear triangle (3 nodes)
    Quad = 1,         // Bilinear quad (4 nodes)
    TriQuadratic = 2, // Quadratic triangle (6 nodes, mid-edge)
    QuadQuadratic = 3,// Quadratic quad (8/9 nodes)
    Tet = 4,          // Linear tetrahedron (volume rendering)
    Hex = 5,          // Linear hexahedron (volume rendering)
};

/// Interpolate scalar value within a linear triangle using barycentric coords.
/// nodeValues: scalar values at the 3 triangle vertices.
/// bary: barycentric coordinates of the fragment.
public float interpolateTriangle(float3 nodeValues, float3 bary) {
    return dot(nodeValues, bary);
}

/// Interpolate scalar value within a bilinear quad using parametric coords.
/// nodeValues: scalar values at 4 quad vertices (order: BL, BR, TR, TL).
/// uv: parametric coordinates [0,1]^2.
public float interpolateQuad(float4 nodeValues, float2 uv) {
    float v0 = lerp(nodeValues.x, nodeValues.y, uv.x);
    float v1 = lerp(nodeValues.w, nodeValues.z, uv.x);
    return lerp(v0, v1, uv.y);
}

/// Map a scalar value to a colormap. Uses turbo-like ramp by default.
/// value: normalized scalar [0, 1].
public float3 colormapTurbo(float value) {
    float t = saturate(value);
    float4 v4 = float4(1.0, t, t * t, t * t * t);
    float2 v2 = v4.zw * v4.z;
    float r = dot(v4, float4(0.13572138, 4.61539260, -42.66032258, 132.13108234))
            + dot(v2, float2(-152.94239396, 59.28637943));
    float g = dot(v4, float4(0.09140261, 2.19418839, 4.84296658, -14.18503333))
            + dot(v2, float2(4.27729857, 2.82956604));
    float b = dot(v4, float4(0.10667330, 12.64194608, -60.58204836, 110.36276771))
            + dot(v2, float2(-89.90310912, 27.34824973));
    return saturate(float3(r, g, b));
}

/// Compute contour line intensity at a given scalar value.
/// Returns ~1.0 near contour lines, ~0.0 away from them.
/// scalarValue: the interpolated scalar at this fragment.
/// contourInterval: spacing between contour lines.
/// lineWidth: width of contour lines in scalar space.
public float contourLineIntensity(float scalarValue, float contourInterval, float lineWidth) {
    float scaledVal = scalarValue / max(contourInterval, 1e-6);
    float frac_ = frac(scaledVal);
    float dist = min(frac_, 1.0 - frac_) * contourInterval;
    // Use screen-space derivative for consistent line width
    float dScalar = max(length(float2(ddx(scalarValue), ddy(scalarValue))), 1e-10);
    return 1.0 - smoothstep(0.0, lineWidth * dScalar, dist);
}

/// Apply displacement field to deform mesh vertex position.
/// originalPos: undeformed vertex position.
/// displacement: displacement vector at this vertex.
/// scale: displacement scale factor (for exaggerated deformation visualization).
public float3 applyDeformation(float3 originalPos, float3 displacement, float scale) {
    return originalPos + displacement * scale;
}

/// Map scalar to color with optional contour overlay.
public float4 femRenderFragment(
    float scalarValue,
    float scalarMin,
    float scalarMax,
    float contourInterval,
    float contourLineWidth,
    float4 contourColor
) {
    float normalized = saturate((scalarValue - scalarMin) / max(scalarMax - scalarMin, 1e-10));
    float3 baseColor = colormapTurbo(normalized);
    float contour = contourLineIntensity(scalarValue, contourInterval, contourLineWidth);
    float3 finalColor = lerp(baseColor, contourColor.rgb, contour * contourColor.a);
    return float4(finalColor, 1.0);
}
