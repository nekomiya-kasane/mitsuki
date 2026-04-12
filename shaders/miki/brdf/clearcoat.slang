// brdf/clearcoat.slang — Clearcoat BRDF lobe.
//
// Separate specular layer on top of the base material (automotive paint, lacquer).
// Uses isotropic GGX with fixed IOR ~1.5 (F0 = 0.04).
//
// Reference: Karis 2013 "Real Shading in Unreal Engine 4"
//            Burley 2012 "Physically Based Shading at Disney"

implementing miki_brdf;

static const float kClearcoatF0 = 0.04;  // IOR ~1.5
static const float kClearcoatPI = 3.14159265358979323846;

/// Evaluate clearcoat specular lobe.
/// clearcoatRoughness: roughness of the clearcoat layer.
/// clearcoat: strength factor in [0, 1].
public float evaluateClearcoat(float clearcoat, float clearcoatRoughness,
                                float NdotH, float NdotV, float NdotL, float VdotH) {
    float alpha = max(clearcoatRoughness * clearcoatRoughness, 0.001);
    float D = ggxD(NdotH, alpha);
    float G = ggxSmithJoint(NdotV, NdotL, alpha);
    float3 F = fresnelSchlick(VdotH, float3(kClearcoatF0));
    return clearcoat * D * G * F.x;
}
