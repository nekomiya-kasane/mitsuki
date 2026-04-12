// brdf/aniso.slang — Anisotropic GGX specular BRDF.
//
// Extends GGX NDF with separate roughness along tangent/bitangent.
// Used for brushed metal, hair, carbon fiber.
//
// Reference: Burley 2012 "Physically Based Shading at Disney"
//            Heitz 2014 anisotropic masking-shadowing

implementing miki_brdf;

static const float kAnisoPI = 3.14159265358979323846;

/// Anisotropic GGX NDF.
/// alphaT, alphaB: roughness along tangent and bitangent.
public float anisoGGXD(float NdotH, float TdotH, float BdotH, float alphaT, float alphaB) {
    float a2 = alphaT * alphaB;
    float3 v = float3(alphaB * TdotH, alphaT * BdotH, a2 * NdotH);
    float v2 = dot(v, v);
    float w2 = a2 / v2;
    return a2 * w2 * w2 / kAnisoPI;
}

/// Anisotropic Smith-joint visibility.
public float anisoSmithJoint(float NdotV, float NdotL,
                              float TdotV, float BdotV,
                              float TdotL, float BdotL,
                              float alphaT, float alphaB) {
    float lambdaV = NdotL * length(float3(alphaT * TdotV, alphaB * BdotV, NdotV));
    float lambdaL = NdotV * length(float3(alphaT * TdotL, alphaB * BdotL, NdotL));
    return 0.5 / max(lambdaV + lambdaL, 1e-7);
}

/// Map scalar roughness + anisotropy to tangent/bitangent roughness.
/// anisotropy in [-1, 1].
public void roughnessToAniso(float roughness, float anisotropy,
                              out float alphaT, out float alphaB) {
    float aspect = sqrt(1.0 - 0.9 * abs(anisotropy));
    float alpha = roughness * roughness;
    if (anisotropy >= 0.0) {
        alphaT = alpha / aspect;
        alphaB = alpha * aspect;
    } else {
        alphaT = alpha * aspect;
        alphaB = alpha / aspect;
    }
    alphaT = max(alphaT, 0.001);
    alphaB = max(alphaB, 0.001);
}
