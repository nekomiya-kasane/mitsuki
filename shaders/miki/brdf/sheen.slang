// brdf/sheen.slang — Sheen BRDF lobe for fabric/cloth materials.
//
// Charlie sheen model (Estevez & Kulla 2017, used by glTF KHR_materials_sheen).
// Ashikhmin inverted Gaussian distribution for soft fabric appearance.
//
// Reference: Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"

implementing miki_brdf;

static const float kSheenPI = 3.14159265358979323846;

/// Charlie sheen NDF — inverted Gaussian distribution.
/// sheenRoughness in [0, 1].
public float charlieD(float NdotH, float sheenRoughness) {
    float alpha = sheenRoughness * sheenRoughness;
    float invAlpha = 1.0 / alpha;
    float cos2h = NdotH * NdotH;
    float sin2h = max(1.0 - cos2h, 0.0);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * kSheenPI);
}

/// Ashikhmin visibility for sheen (simplified, no height correlation needed).
public float sheenVisibility(float NdotV, float NdotL) {
    return 1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV));
}

/// Evaluate sheen BRDF lobe.
public float3 evaluateSheen(float3 sheenColor, float sheenRoughness,
                             float NdotH, float NdotV, float NdotL) {
    float D = charlieD(NdotH, max(sheenRoughness, 0.01));
    float V = sheenVisibility(NdotV, NdotL);
    return sheenColor * D * V;
}
