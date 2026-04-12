// brdf/iridescence.slang — Thin-film iridescence.
//
// Thin-film interference modifies the Fresnel term based on film thickness
// and viewing angle. Used for soap bubbles, oil slicks, beetle shells.
//
// Reference: Belcour & Barla 2017 "A Practical Extension to Microfacet Theory
//            for the Modeling of Varying Iridescence"
//            glTF KHR_materials_iridescence extension

implementing miki_brdf;

static const float kIridescencePI = 3.14159265358979323846;

/// Compute thin-film Fresnel reflectance modification.
/// thickness: film thickness in nanometers (100-1000nm typical range).
/// ior: index of refraction of the thin film (1.3 typical).
/// baseF0: base material F0 to modify.
/// cosTheta: dot(N, V) or dot(N, H).
public float3 iridescenceFresnel(float thickness, float ior, float3 baseF0, float cosTheta) {
    // Thin film interference: OPD = 2 * n * d * cos(theta_t)
    float sinTheta2 = 1.0 - cosTheta * cosTheta;
    float sinThetaT2 = sinTheta2 / (ior * ior);
    float cosThetaT = sqrt(max(1.0 - sinThetaT2, 0.0));
    float opd = 2.0 * ior * thickness * cosThetaT;

    // Phase shift per wavelength (RGB: 650nm, 510nm, 475nm)
    float3 wavelengths = float3(650.0, 510.0, 475.0);
    float3 phase = 2.0 * kIridescencePI * opd / wavelengths;

    // Interference pattern modulates reflectance
    float3 thinFilmFactor = 0.5 + 0.5 * cos(phase);

    // Blend between base F0 and thin-film modified F0
    return lerp(baseF0, thinFilmFactor, 0.5);
}
