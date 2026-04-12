// lighting/area_light.slang — Polygonal area lights via LTC (implementing miki_lighting).
//
// Linearly Transformed Cosines (LTC) method (Heitz et al. 2016).
// Transforms the BRDF lobe into a cosine distribution, then evaluates
// the analytic irradiance integral over a polygonal light source.

implementing miki_lighting;

import miki_core;

/// Compute the analytic irradiance from a rectangular area light.
/// Uses the vector irradiance formula for a polygon (closed-form for rectangles).
[ForceInline]
public float evaluateRectLightIrradiance(
    float3 surfacePos, float3 surfaceNormal,
    float3 lightCenter, float3 lightRight, float3 lightUp,
    float halfWidth, float halfHeight
) {
    // Compute 4 corners of the rectangular light
    float3 corners[4];
    corners[0] = lightCenter - lightRight * halfWidth - lightUp * halfHeight;
    corners[1] = lightCenter + lightRight * halfWidth - lightUp * halfHeight;
    corners[2] = lightCenter + lightRight * halfWidth + lightUp * halfHeight;
    corners[3] = lightCenter - lightRight * halfWidth + lightUp * halfHeight;

    // Integrate using the edge formula (vector irradiance)
    float3 irradiance = float3(0.0f);
    [ForceUnroll]
    for (int i = 0; i < 4; ++i) {
        float3 v0 = normalize(corners[i] - surfacePos);
        float3 v1 = normalize(corners[(i + 1) % 4] - surfacePos);
        float3 edgeCross = cross(v0, v1);
        float sinTheta = length(edgeCross);
        float cosTheta = dot(v0, v1);
        float theta = atan2(sinTheta, cosTheta);
        if (sinTheta > 1e-6f) {
            irradiance += theta * normalize(edgeCross);
        }
    }

    // Project onto surface normal hemisphere
    float result = max(dot(irradiance, surfaceNormal), 0.0f);
    return result / (2.0f * 3.14159265f);  // Normalize by 2*pi (hemisphere solid angle factor)
}

/// Apply the LTC inverse transform matrix to transform BRDF into cosine distribution.
/// The Minv matrix is fetched from the LTC LUT based on (roughness, NdotV).
[ForceInline]
public float3 applyLTCTransform(float3 dir, float3x3 Minv) {
    float3 transformed = mul(Minv, dir);
    return normalize(transformed);
}
