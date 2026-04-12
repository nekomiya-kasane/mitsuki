// lighting/light_interface.slang — ILight interface (implementing miki_lighting).
//
// Abstract light interface for polymorphic light evaluation.
// Concrete types (PointLight, DirectionalLight, SpotLight, AreaLight)
// are selected at link time or iterated via typed buffers.

implementing miki_lighting;

import miki_core;

/// Light type enumeration matching the C++ LightType.
public enum LightType : uint {
    Directional = 0,
    Point = 1,
    Spot = 2,
    Area = 3,
};

/// Packed GPU light data. 64 bytes aligned for structured buffer access.
public struct GpuLight {
    public float3 position;      // world position (point/spot/area)
    public float range;           // attenuation range
    public float3 direction;     // forward direction (directional/spot/area)
    public float innerCosAngle;   // cos(innerConeAngle) for spot
    public float3 color;         // linear HDR color * intensity
    public float outerCosAngle;   // cos(outerConeAngle) for spot
    public LightType type;
    public float areaWidth;       // area light half-width
    public float areaHeight;      // area light half-height
    public uint shadowMapIndex;   // index into shadow atlas, 0xFFFFFFFF = no shadow
};

/// ILight interface: evaluate radiance contribution from a light source.
public interface ILight {
    /// Evaluate incident radiance at a surface point from this light.
    /// Returns (radiance, lightDir, attenuation, distance).
    float3 evaluate(float3 surfacePos, float3 surfaceNormal, out float3 lightDir, out float attenuation);

    /// Compute shadow factor [0=fully shadowed, 1=fully lit].
    float shadowFactor(float3 surfacePos, float3 lightDir);
};

/// Compute distance attenuation with inverse-square falloff and range clamp.
/// Uses UE5-style smooth falloff: (1 - (d/r)^2)^2 / (d^2 + 1).
[ForceInline]
public float computeDistanceAttenuation(float distance, float range) {
    if (range <= 0.0f) return 0.0f;
    float ratio = distance / range;
    float ratio2 = ratio * ratio;
    float falloff = saturate(1.0f - ratio2);
    falloff *= falloff;
    return falloff / (distance * distance + 1.0f);
}

/// Compute spot light angular attenuation.
[ForceInline]
public float computeSpotAttenuation(float cosAngle, float innerCos, float outerCos) {
    return saturate((cosAngle - outerCos) / max(innerCos - outerCos, 1e-5f));
}
