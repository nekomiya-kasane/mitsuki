// brdf/dspbr.slang — Disney Standard PBR material (DSPBR).
//
// Full-featured PBR material implementing IMaterial interface.
// Combines GGX specular, Burley diffuse, sheen, clearcoat lobes.
// This is the default material for miki renderer.
//
// Reference: Burley 2012 "Physically Based Shading at Disney"
//            Karis 2013 "Real Shading in Unreal Engine 4"
//            glTF 2.0 PBR metallic-roughness model

implementing miki_brdf;

static const float kDSPBR_PI = 3.14159265358979323846;

/// DSPBR shading data — prepared once per shading point.
public struct DSPBRShadingData {
    public float3 baseColor;
    public float  metallic;
    public float  roughness;
    public float  alpha;          ///< roughness^2 (remapped)
    public float3 F0;             ///< Fresnel reflectance at normal incidence
    public float3 diffuseColor;   ///< baseColor * (1 - metallic)
    public float  sheenRoughness;
    public float3 sheenColor;
    public float  clearcoat;
    public float  clearcoatRoughness;
    public float3 N;              ///< Shading normal
    public float3 V;              ///< View direction
    public float  NdotV;
};

/// Disney Standard PBR material.
public struct DSPBR : IMaterial {
    typedef DSPBRShadingData ShadingData;

    // Material parameters (set externally via push constants / material buffer)
    public float3 baseColor;
    public float  metallic;
    public float  roughness;
    public float3 sheenColor;
    public float  sheenRoughness;
    public float  clearcoat;
    public float  clearcoatRoughness;

    public DSPBRShadingData prepareShadingData(SurfaceInteraction si) {
        DSPBRShadingData sd;
        sd.baseColor = baseColor;
        sd.metallic = metallic;
        sd.roughness = max(roughness, 0.045);  // Prevent singularity
        sd.alpha = sd.roughness * sd.roughness;
        sd.F0 = lerp(float3(0.04), baseColor, metallic);
        sd.diffuseColor = baseColor * (1.0 - metallic);
        sd.sheenColor = sheenColor;
        sd.sheenRoughness = sheenRoughness;
        sd.clearcoat = clearcoat;
        sd.clearcoatRoughness = max(clearcoatRoughness, 0.045);
        sd.N = si.normal;
        sd.V = si.viewDir;
        sd.NdotV = max(dot(si.normal, si.viewDir), 1e-5);
        return sd;
    }

    public float3 evaluate(DSPBRShadingData sd, float3 wi, float3 wo) {
        float3 N = sd.N;
        float NdotL = max(dot(N, wi), 0.0);
        float NdotV = sd.NdotV;
        if (NdotL <= 0.0) return float3(0.0);

        float3 H = normalize(wi + wo);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(wo, H), 0.0);

        // Specular: GGX microfacet
        float  D = ggxD(NdotH, sd.alpha);
        float  G = ggxSmithJoint(NdotV, NdotL, sd.alpha);
        float3 F = fresnelSchlick(VdotH, sd.F0);
        float3 specular = D * G * F;

        // Diffuse: Burley
        float3 diffuse = burleyDiffuse(sd.diffuseColor, sd.roughness, NdotV, NdotL, VdotH);

        // Energy conservation: diffuse is attenuated by (1 - F)
        float3 kD = (1.0 - F) * (1.0 - sd.metallic);
        float3 result = kD * diffuse + specular;

        // Sheen lobe
        if (any(sd.sheenColor > float3(0.0))) {
            result += evaluateSheen(sd.sheenColor, sd.sheenRoughness, NdotH, NdotV, NdotL);
        }

        // Clearcoat lobe
        if (sd.clearcoat > 0.0) {
            result += evaluateClearcoat(sd.clearcoat, sd.clearcoatRoughness,
                                         NdotH, NdotV, NdotL, VdotH);
        }

        return result * NdotL;
    }

    public BRDFSample sample(DSPBRShadingData sd, float3 wo, float2 xi) {
        BRDFSample result;
        // VNDF sampling for GGX specular
        float3 Ve = float3(dot(wo, float3(1, 0, 0)), dot(wo, float3(0, 1, 0)), dot(wo, sd.N));
        float3 H = sampleGGXVNDF(Ve, sd.alpha, xi);
        // Transform H back to world space (simplified: assume N-aligned TBN)
        float3 Hw = normalize(H.x * float3(1, 0, 0) + H.y * float3(0, 1, 0) + H.z * sd.N);
        float3 wi = reflect(-wo, Hw);

        float NdotL = dot(sd.N, wi);
        if (NdotL <= 0.0) {
            result.direction = float3(0.0);
            result.weight = float3(0.0);
            result.pdf = 0.0;
            return result;
        }

        result.direction = wi;
        result.pdf = pdf(sd, wi, wo);
        if (result.pdf > 0.0) {
            result.weight = evaluate(sd, wi, wo) / result.pdf;
        } else {
            result.weight = float3(0.0);
        }
        return result;
    }

    public float pdf(DSPBRShadingData sd, float3 wi, float3 wo) {
        float3 H = normalize(wi + wo);
        float NdotH = max(dot(sd.N, H), 0.0);
        float VdotH = max(dot(wo, H), 0.0);
        return pdfGGXVNDF(NdotH, VdotH, sd.alpha);
    }
};

/// Simplified Lambertian material for fallback / performance-critical paths.
public struct SimpleLambert : IMaterial {
    typedef DSPBRShadingData ShadingData;

    public float3 baseColor;

    public DSPBRShadingData prepareShadingData(SurfaceInteraction si) {
        DSPBRShadingData sd;
        sd.baseColor = baseColor;
        sd.metallic = 0.0;
        sd.roughness = 1.0;
        sd.alpha = 1.0;
        sd.F0 = float3(0.04);
        sd.diffuseColor = baseColor;
        sd.sheenColor = float3(0.0);
        sd.sheenRoughness = 0.0;
        sd.clearcoat = 0.0;
        sd.clearcoatRoughness = 0.0;
        sd.N = si.normal;
        sd.V = si.viewDir;
        sd.NdotV = max(dot(si.normal, si.viewDir), 1e-5);
        return sd;
    }

    public float3 evaluate(DSPBRShadingData sd, float3 wi, float3 wo) {
        float NdotL = max(dot(sd.N, wi), 0.0);
        return lambertianDiffuse(sd.diffuseColor) * NdotL;
    }

    public BRDFSample sample(DSPBRShadingData sd, float3 wo, float2 xi) {
        BRDFSample result;
        // Cosine-weighted hemisphere sampling
        float phi = 2.0 * kDSPBR_PI * xi.x;
        float cosTheta = sqrt(xi.y);
        float sinTheta = sqrt(1.0 - xi.y);
        result.direction = normalize(sinTheta * cos(phi) * float3(1, 0, 0)
                                   + sinTheta * sin(phi) * float3(0, 1, 0)
                                   + cosTheta * sd.N);
        result.pdf = cosTheta / kDSPBR_PI;
        if (result.pdf > 0.0) {
            result.weight = evaluate(sd, result.direction, wo) / result.pdf;
        } else {
            result.weight = float3(0.0);
        }
        return result;
    }

    public float pdf(DSPBRShadingData sd, float3 wi, float3 wo) {
        float NdotL = max(dot(sd.N, wi), 0.0);
        return NdotL / kDSPBR_PI;
    }
};
