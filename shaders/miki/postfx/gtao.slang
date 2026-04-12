// postfx/gtao.slang — Ground Truth Ambient Occlusion (implementing miki_postfx).
//
// GTAO (Jimenez 2016): horizon-based AO with cosine-weighted integration.
// Better quality than SSAO with similar cost. Spatiotemporal filtering recommended.

implementing miki_postfx;

import miki_core;

/// GTAO configuration.
public struct GTAOParams {
    public float radius;          // world-space AO radius
    public float falloffStart;    // distance where AO starts to fade
    public float falloffEnd;      // distance where AO is fully faded
    public float power;           // AO power exponent
    public int directionCount;    // number of search directions (4-12)
    public int stepsPerDir;       // march steps per direction (4-8)
    public float2 noiseScale;    // noise texture tiling
};

/// Compute GTAO for a single pixel.
/// Returns AO factor [0=occluded, 1=fully visible].
[ForceInline]
public float computeGTAO(
    Texture2D<float> depthBuffer, SamplerState samp,
    float2 uv, float3 viewNormal, float linearDepth,
    float4x4 projMatrix, float2 screenSize,
    GTAOParams params, float2 noise
) {
    float ao = 0.0f;
    float invRadius2 = 1.0f / (params.radius * params.radius);
    float angleStep = 3.14159265f / float(params.directionCount);

    for (int d = 0; d < params.directionCount; ++d) {
        float angle = angleStep * (float(d) + noise.x);
        float2 dir = float2(cos(angle), sin(angle));
        float maxHorizon = -1.0f;

        for (int s = 1; s <= params.stepsPerDir; ++s) {
            float t = (float(s) + noise.y) / float(params.stepsPerDir);
            float2 sampleUV = uv + dir * (t * params.radius / linearDepth) / screenSize;

            if (any(sampleUV < float2(0.0f)) || any(sampleUV > float2(1.0f))) break;

            float sampleDepth = depthBuffer.SampleLevel(samp, sampleUV, 0).r;
            // Linearize depth (simplified — assumes reverse-Z)
            float sampleLinear = projMatrix[3][2] / (sampleDepth - projMatrix[2][2]);

            float3 horizonVec = float3((sampleUV - uv) * screenSize, linearDepth - sampleLinear);
            float dist2 = dot(horizonVec, horizonVec);
            float horizon = dot(normalize(horizonVec), viewNormal);

            float falloff = saturate(1.0f - dist2 * invRadius2);
            maxHorizon = max(maxHorizon, horizon * falloff);
        }

        ao += 1.0f - maxHorizon;
    }

    ao /= float(params.directionCount);
    return pow(ao, params.power);
}
