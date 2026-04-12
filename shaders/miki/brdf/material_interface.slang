// brdf/material_interface.slang — IMaterial interface definition (§15.4.1).
//
// All material models implement this interface. Pass shaders use
// `extern struct MaterialImpl : IMaterial;` for link-time specialization.

implementing miki_brdf;

/// Shading data common to all material evaluations.
public struct SurfaceInteraction {
    public float3 position;     ///< World-space hit point
    public float3 normal;       ///< Shading normal (post-normal-map)
    public float3 geometricN;   ///< Geometric normal (flat)
    public float3 tangent;      ///< Tangent vector (for aniso)
    public float3 bitangent;    ///< Bitangent vector
    public float2 uv;           ///< Texture coordinates
    public float3 viewDir;      ///< View direction (toward camera, normalized)
};

/// Material evaluation result.
public struct BRDFSample {
    public float3 direction;    ///< Sampled direction (world space)
    public float3 weight;       ///< BRDF * cosTheta / pdf
    public float  pdf;          ///< Probability density
};

/// Material interface — all material models must implement this.
/// Link-time specialization resolves the concrete type, producing monomorphic codegen.
public interface IMaterial {
    associatedtype ShadingData;

    /// Prepare shading data from surface interaction and material parameters.
    ShadingData prepareShadingData(SurfaceInteraction si);

    /// Evaluate BRDF f(wi, wo) * cosTheta for the given directions.
    float3 evaluate(ShadingData sd, float3 wi, float3 wo);

    /// Sample a direction from the BRDF lobe.
    BRDFSample sample(ShadingData sd, float3 wo, float2 xi);

    /// PDF of sampling direction wi given outgoing wo.
    float pdf(ShadingData sd, float3 wi, float3 wo);
};
