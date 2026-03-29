// Probe: texture array sampling.
// Validates Texture2DArray + SamplerState binding.

[[vk::binding(0, 0)]]
Texture2DArray texArray;

[[vk::binding(1, 0)]]
SamplerState texSampler;

[[vk::binding(2, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float3 uv = float3(float(tid.x) / 64.0f, 0.5f, 0.0f);
    output[tid.x] = texArray.SampleLevel(texSampler, uv, 0);
}
