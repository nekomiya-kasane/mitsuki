// rt/rt_shadows.slang — RT shadows.
//
// Ray-traced shadows with support for:
// - Hard shadows (single shadow ray per light)
// - Soft shadows (multiple rays sampling light solid angle)
// - Denoised 1spp shadows with temporal + spatial filtering
// - Contact hardening (PCSS-like with RT: penumbra scales with blocker distance)
//
// Reference: NVIDIA RTXDI shadows, AMD FidelityFX shadows,
//            "Ray Traced Shadows" (Wyman 2019 Ray Tracing Gems).

implementing miki_rt;

import miki_core;

/// RT shadow parameters.
public struct RtShadowParams {
    public float rayMaxDistance;     // Maximum shadow ray distance
    public float normalBias;        // Offset along normal to prevent self-shadowing
    public float lightRadius;       // Light source radius (for soft shadows)
    public uint  samplesPerPixel;   // Number of shadow rays (1 for denoised soft shadows)
    public bool  contactHardening;  // Enable PCSS-like contact hardening
};

/// Generate a shadow ray toward a point/directional light.
/// surfacePos: world-space surface point.
/// surfaceNormal: geometric normal for bias.
/// lightDir: normalized direction toward the light.
/// params: shadow configuration.
public void makeShadowRay(
    float3 surfacePos,
    float3 surfaceNormal,
    float3 lightDir,
    RtShadowParams params,
    out float3 rayOrigin,
    out float3 rayDir,
    out float rayTMax
) {
    rayOrigin = surfacePos + surfaceNormal * params.normalBias;
    rayDir = lightDir;
    rayTMax = params.rayMaxDistance;
}

/// Sample a point on a spherical light source for soft shadow ray.
/// lightCenter: world-space light position.
/// lightRadius: radius of the spherical light.
/// surfacePos: shading point.
/// xi: float2 random numbers [0, 1)^2.
/// Returns sampled direction toward the light surface.
public float3 sampleSphericalLight(
    float3 lightCenter,
    float lightRadius,
    float3 surfacePos,
    float2 xi
) {
    float3 toLight = lightCenter - surfacePos;
    float dist = length(toLight);
    float3 lightDir = toLight / max(dist, 1e-6);

    // Subtended solid angle
    float sinThetaMax = min(lightRadius / max(dist, lightRadius), 1.0);
    float cosThetaMax = sqrt(max(1.0 - sinThetaMax * sinThetaMax, 0.0));

    // Uniform cone sampling
    float cosTheta = 1.0 - xi.x * (1.0 - cosThetaMax);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = 6.28318530718 * xi.y;

    // Build tangent frame around lightDir
    float3 up = abs(lightDir.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 t = normalize(cross(up, lightDir));
    float3 b = cross(lightDir, t);

    return normalize(sinTheta * cos(phi) * t + sinTheta * sin(phi) * b + cosTheta * lightDir);
}

/// Penumbra estimation for contact-hardening shadows.
/// blockerDistance: average distance to shadow-casting geometry.
/// receiverDistance: distance from light to receiver surface.
/// lightRadius: physical light radius.
/// Returns penumbra size as a fraction of light solid angle.
public float estimatePenumbra(float blockerDistance, float receiverDistance, float lightRadius) {
    float blockerDist = max(receiverDistance - blockerDistance, 0.001);
    return (blockerDist / max(blockerDistance, 0.001)) * lightRadius;
}

/// Combine multi-sample shadow results.
/// visibility: array of per-sample visibility values (0 or 1).
/// sampleCount: number of valid samples.
public float combineShadowSamples(float visibilitySum, uint sampleCount) {
    return visibilitySum / max(float(sampleCount), 1.0);
}
