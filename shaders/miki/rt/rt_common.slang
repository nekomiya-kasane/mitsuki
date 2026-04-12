// rt/rt_common.slang — Common RT payload, hit info, and utility structures.
//
// Shared data types for all ray tracing shaders: payloads, hit attributes,
// ray generation helpers, and acceleration structure utilities.
//
// These types form the RT pipeline contract between ray generation,
// closest-hit, any-hit, miss, and intersection shaders.

implementing miki_rt;

import miki_core;

/// Maximum ray recursion depth.
public static const uint kMaxRayRecursion = 4u;

/// Ray payload for primary/reflection rays.
public struct RayPayload {
    public float3 color;       // Accumulated radiance
    public float  hitT;        // Hit distance (FLT_MAX if miss)
    public float3 worldPos;    // World-space hit position
    public float3 worldNormal; // World-space geometric normal at hit
    public uint   instanceID;  // Instance index for material lookup
    public uint   primitiveID; // Primitive index for attribute interpolation
    public uint   recursionDepth; // Current bounce count
    public bool   hit;         // True if ray hit geometry
};

/// Shadow ray payload (minimal for performance).
public struct ShadowPayload {
    public float visibility;   // 1.0 = fully lit, 0.0 = fully shadowed
};

/// Hit attributes for triangle intersection.
public struct TriangleHitAttributes {
    public float2 barycentrics; // Barycentric coordinates (beta, gamma)
};

/// Reconstruct world-space hit position from ray and hit distance.
public float3 hitWorldPosition(float3 rayOrigin, float3 rayDir, float hitT) {
    return rayOrigin + rayDir * hitT;
}

/// Interpolate vertex attribute using barycentric coordinates.
/// a0, a1, a2: attribute values at triangle vertices.
/// bary: float2 barycentric coordinates (beta, gamma). alpha = 1 - beta - gamma.
public float3 baryInterpolate3(float3 a0, float3 a1, float3 a2, float2 bary) {
    return a0 * (1.0 - bary.x - bary.y) + a1 * bary.x + a2 * bary.y;
}

public float2 baryInterpolate2(float2 a0, float2 a1, float2 a2, float2 bary) {
    return a0 * (1.0 - bary.x - bary.y) + a1 * bary.x + a2 * bary.y;
}

/// Offset a ray origin along the geometric normal to avoid self-intersection.
/// Uses the Wachter & Binder (2019) normal-offset method for robust origins.
/// worldPos: hit position. normal: geometric normal. bias: offset distance.
public float3 offsetRayOrigin(float3 worldPos, float3 normal, float bias) {
    return worldPos + normal * bias;
}

/// Generate camera ray from pixel coordinates.
/// pixelCoord: integer pixel position.
/// screenDim: render target dimensions.
/// invViewProj: inverse view-projection matrix.
/// cameraPos: world-space camera position.
public void generateCameraRay(
    float2 pixelCoord,
    float2 screenDim,
    float4x4 invViewProj,
    float3 cameraPos,
    out float3 rayOrigin,
    out float3 rayDir
) {
    float2 uv = (pixelCoord + 0.5) / screenDim;
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Vulkan Y-flip
    float4 worldTarget = mul(invViewProj, float4(ndc, 0.0, 1.0));
    worldTarget.xyz /= worldTarget.w;
    rayOrigin = cameraPos;
    rayDir = normalize(worldTarget.xyz - cameraPos);
}

/// Russian roulette termination probability based on throughput.
/// throughput: current path throughput (product of BRDFs / PDFs).
/// minBounce: minimum bounces before RR kicks in (default: 3).
public float russianRouletteProbability(float3 throughput, uint bounce, uint minBounce) {
    if (bounce < minBounce) return 1.0;
    return min(max(throughput.x, max(throughput.y, throughput.z)), 0.95);
}
