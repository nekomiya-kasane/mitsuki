// rt/rt_reflections.slang — RT reflections.
//
// Implements hybrid RT reflections:
// - Full RT reflections for mirror/glossy surfaces (roughness < threshold).
// - Screen-space fallback for rough surfaces (SSR from miki-postfx).
// - Importance-sampled reflection rays using GGX VNDF sampling.
// - Temporal accumulation with reprojection for noise reduction.
//
// Reference: UE5 Lumen reflections, AMD FidelityFX Hybrid Reflections,
//            Stochastic Screen-Space Reflections (Stachowiak 2015).

implementing miki_rt;

import miki_core;

/// RT reflection parameters.
public struct RtReflectionParams {
    public float roughnessThreshold; // Below this: RT. Above: SSR fallback (default: 0.3)
    public float rayMaxDistance;      // Maximum ray trace distance in world units
    public float temporalAlpha;       // Temporal blend factor (default: 0.05)
    public uint  maxSamplesPerPixel;  // Samples for denoising convergence
    public float normalBias;          // Ray origin offset along normal
};

/// Determine if a surface should use RT or SSR for reflections.
/// roughness: material roughness [0, 1].
/// threshold: cutoff roughness value.
public bool shouldUseRtReflection(float roughness, float threshold) {
    return roughness < threshold;
}

/// Generate a reflection ray direction using GGX VNDF sampling.
/// wo: outgoing view direction (toward camera).
/// normal: surface normal.
/// roughness: material roughness.
/// xi: float2 random numbers [0, 1)^2.
/// Returns sampled reflection direction.
public float3 sampleGgxReflection(float3 wo, float3 normal, float roughness, float2 xi) {
    float alpha = roughness * roughness;

    // Build tangent frame from normal
    float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);

    // Transform wo to local space
    float3 woLocal = float3(dot(wo, tangent), dot(wo, normal), dot(wo, bitangent));

    // VNDF sampling (Heitz 2018)
    float3 wh = normalize(float3(alpha * woLocal.x, woLocal.y, alpha * woLocal.z));
    if (wh.y < 0.0) wh = -wh;

    float3 t1 = (wh.y < 0.999) ? normalize(cross(float3(0, 1, 0), wh)) : float3(1, 0, 0);
    float3 t2 = cross(wh, t1);

    float r = sqrt(xi.x);
    float phi = 6.28318530718 * xi.y;
    float s = 0.5 * (1.0 + wh.y);
    float t1Weight = r * cos(phi);
    float t2Weight = (1.0 - s) * sqrt(max(1.0 - t1Weight * t1Weight, 0.0))
                   + s * r * sin(phi);

    float3 nh = t1Weight * t1 + t2Weight * t2
              + sqrt(max(1.0 - t1Weight * t1Weight - t2Weight * t2Weight, 0.0)) * wh;
    float3 neLocal = normalize(float3(alpha * nh.x, max(nh.y, 0.0), alpha * nh.z));

    // Transform half vector back to world space
    float3 halfVec = tangent * neLocal.x + normal * neLocal.y + bitangent * neLocal.z;
    return reflect(-wo, halfVec);
}

/// Temporal accumulation for RT reflections.
/// current: current frame's reflection color.
/// history: previous frame's accumulated reflection color.
/// alpha: blend factor (lower = more temporal stability, higher = more responsive).
public float3 temporalAccumulate(float3 current, float3 history, float alpha) {
    return lerp(history, current, alpha);
}

/// Confidence-based temporal blend: reduce alpha when motion/disocclusion detected.
/// alpha: base temporal blend factor.
/// motionLength: screen-space motion vector magnitude.
/// depthDiff: normalized depth difference between current and reprojected.
public float adjustTemporalAlpha(float alpha, float motionLength, float depthDiff) {
    float motionReject = saturate(motionLength * 10.0);
    float depthReject = saturate(depthDiff * 100.0);
    return lerp(alpha, 1.0, max(motionReject, depthReject));
}
