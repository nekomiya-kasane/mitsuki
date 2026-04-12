// cae/point_cloud.slang — Point cloud splatting with Eye-Dome Lighting (EDL).
//
// Renders large point clouds using hardware-accelerated point splatting.
// Applies Eye-Dome Lighting (EDL) as a screen-space shading technique
// that provides depth perception without explicit normals.
//
// EDL technique: For each pixel, compare its depth with neighbors in a star
// pattern. Pixels with large depth discontinuities get darker shading,
// creating a pseudo-ambient-occlusion / edge-enhancement effect.
//
// Reference: Boucheny (2009) "Eye-Dome Lighting", CloudCompare EDL shader,
//            Potree point cloud renderer, HOOPS Visualize point splatting.
//
// Performance: EDL is a single fullscreen pass, < 0.3ms at 1080p.

implementing miki_cae;

import miki_core;

/// Point splat parameters.
public struct PointSplatParams {
    public float pointSize;         // Base point size in pixels
    public float sizeAttenuation;   // Distance-based size attenuation factor
    public bool  circularSplat;     // If true, discard corners for circular points
    public float nearPlane;
    public float farPlane;
};

/// EDL shading parameters.
public struct EdlParams {
    public float strength;          // EDL darkening strength (default: 1.0)
    public float radius;            // Sampling radius in pixels (default: 1.0)
    public uint  numSamples;        // Number of direction samples (default: 8)
    public float2 screenDimensions; // Render target size
    public float nearPlane;
    public float farPlane;
};

/// Compute EDL response for a single pixel.
/// depthBuffer: linear depth buffer (or hardware depth).
/// depthSampler: point sampler.
/// uv: current pixel UV.
/// centerDepth: depth at current pixel.
/// Returns a darkening factor [0, 1] where 0 = fully darkened, 1 = no darkening.
public float computeEdl(
    Texture2D<float> depthBuffer,
    SamplerState depthSampler,
    float2 uv,
    float centerDepth,
    EdlParams params
) {
    if (centerDepth >= params.farPlane) return 1.0; // Background

    float2 texelSize = 1.0 / params.screenDimensions;
    float logCenterDepth = log2(max(centerDepth, 1e-6));
    float response = 0.0;

    // Sample in a circular pattern
    for (uint i = 0; i < params.numSamples; i++) {
        float angle = float(i) * (6.28318530718 / float(params.numSamples));
        float2 offset = float2(cos(angle), sin(angle)) * params.radius * texelSize;
        float neighborDepth = depthBuffer.SampleLevel(depthSampler, uv + offset, 0).r;
        float logNeighborDepth = log2(max(neighborDepth, 1e-6));
        response += max(0.0, logCenterDepth - logNeighborDepth);
    }

    response /= float(params.numSamples);
    return exp(-response * params.strength);
}

/// Compute point splat size with distance attenuation.
/// worldDistance: distance from camera to point.
/// baseSize: base point size in pixels at unit distance.
/// attenuation: attenuation factor (0 = constant size, 1 = full perspective).
public float computePointSize(float worldDistance, float baseSize, float attenuation) {
    return baseSize / max(1.0 + attenuation * worldDistance, 0.01);
}

/// Discard fragment if outside circular splat radius.
/// pointCoord: gl_PointCoord / SV_PointCoord [0,1]^2 within the point sprite.
/// Returns true if the fragment should be discarded.
public bool isOutsideCircularSplat(float2 pointCoord) {
    float2 centered = pointCoord * 2.0 - 1.0;
    return dot(centered, centered) > 1.0;
}

/// Compute per-point color from scalar attribute + colormap.
public float4 pointColorFromScalar(float scalar, float scalarMin, float scalarMax) {
    float t = saturate((scalar - scalarMin) / max(scalarMax - scalarMin, 1e-10));
    // Viridis-like: reuse from scalar_field
    float3 c0 = float3(0.267, 0.005, 0.329);
    float3 c1 = float3(0.128, 0.567, 0.551);
    float3 c2 = float3(0.993, 0.906, 0.144);
    float3 color = t < 0.5 ? lerp(c0, c1, t * 2.0) : lerp(c1, c2, (t - 0.5) * 2.0);
    return float4(color, 1.0);
}
