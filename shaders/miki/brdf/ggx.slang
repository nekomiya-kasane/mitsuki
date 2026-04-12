// brdf/ggx.slang — GGX/Trowbridge-Reitz specular BRDF functions.
//
// Implements the GGX normal distribution, Smith-joint masking-shadowing,
// and Fresnel-Schlick approximation. Used by DSPBR and other material models.
//
// References:
//   - Walter et al. 2007 "Microfacet Models for Refraction"
//   - Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet BRDFs"
//   - Karis 2013 "Real Shading in Unreal Engine 4"

implementing miki_brdf;

static const float kMinRoughness = 0.045;  // Prevent numerical singularity
static const float kPI = 3.14159265358979323846;

/// GGX normal distribution function D(h).
/// alpha = roughness^2 (remapped roughness).
public float ggxD(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (kPI * denom * denom);
}

/// Smith-joint masking-shadowing G2 / (4 * NdotL * NdotV).
/// Height-correlated form (Heitz 2014).
public float ggxSmithJoint(float NdotV, float NdotL, float alpha) {
    float a2 = alpha * alpha;
    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-7);
}

/// Schlick approximation for Fresnel reflectance.
public float3 fresnelSchlick(float cosTheta, float3 F0) {
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return F0 + (1.0 - F0) * (t2 * t2 * t);
}

/// Fresnel-Schlick with roughness (for environment map pre-integration).
public float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * (t2 * t2 * t);
}

/// Sample GGX VNDF (Heitz 2018, Dupuy & Benyoub 2023 improved sampling).
/// Returns half-vector in tangent space.
public float3 sampleGGXVNDF(float3 Ve, float alpha, float2 xi) {
    // Stretch view
    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));
    // Orthonormal basis
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);
    // Parameterization of projected area
    float r = sqrt(xi.x);
    float phi = 2.0 * kPI * xi.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
    // Reproject
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    return normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));
}

/// PDF for GGX VNDF sampling.
public float pdfGGXVNDF(float NdotH, float VdotH, float alpha) {
    float D = ggxD(NdotH, alpha);
    return D * NdotH / (4.0 * max(VdotH, 1e-7));
}
