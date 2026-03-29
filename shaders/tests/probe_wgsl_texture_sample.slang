// Probe: WGSL texture sampling.
// Validates Texture2D.SampleLevel maps to textureSampleLevel in WGSL.
// WGSL separates textures and samplers like Vulkan/SPIR-V.

[[vk::binding(0, 0)]]
Texture2D<float4> diffuseMap;

[[vk::binding(1, 0)]]
SamplerState linearSampler;

[[vk::binding(2, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float2 uv = float2(tid.xy) / 64.0;
    float4 color = diffuseMap.SampleLevel(linearSampler, uv, 0.0);
    output[tid.x + tid.y * 8] = color;
}
