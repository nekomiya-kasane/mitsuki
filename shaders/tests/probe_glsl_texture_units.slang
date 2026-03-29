// Probe: GLSL texture unit usage.
// Validates Texture2D + SamplerState mapping to GLSL sampler2D.
// GLSL 4.30 uses combined texture-sampler units.

[[vk::binding(0, 0)]]
Texture2D<float4> albedoMap;

[[vk::binding(1, 0)]]
Texture2D<float4> normalMap;

[[vk::binding(2, 0)]]
SamplerState linearSampler;

[[vk::binding(3, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float2 uv = float2(tid.xy) / 256.0;
    float4 albedo = albedoMap.SampleLevel(linearSampler, uv, 0.0);
    float4 normal = normalMap.SampleLevel(linearSampler, uv, 0.0);
    output[tid.x + tid.y * 8] = albedo + normal;
}
