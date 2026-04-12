// brdf/diffuse.slang — Diffuse BRDF models.
//
// Implements Lambertian and Burley (Disney) diffuse models.
// The Burley model accounts for roughness-dependent retro-reflection.
//
// Reference: Burley 2012 "Physically Based Shading at Disney"

implementing miki_brdf;

static const float kInvPI = 0.31830988618379067;

/// Lambertian diffuse: f = albedo / pi.
public float3 lambertianDiffuse(float3 albedo) {
    return albedo * kInvPI;
}

/// Disney/Burley diffuse with roughness-dependent retro-reflection.
/// fd = (1/pi) * albedo * (1 + (Fd90 - 1)*(1-NdotL)^5) * (1 + (Fd90 - 1)*(1-NdotV)^5)
public float3 burleyDiffuse(float3 albedo, float roughness, float NdotV, float NdotL, float VdotH) {
    float fd90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
    float viewScatter  = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
    return albedo * kInvPI * lightScatter * viewScatter;
}
