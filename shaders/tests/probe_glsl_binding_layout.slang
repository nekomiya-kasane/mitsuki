// Probe: GLSL layout(binding) mapping.
// Validates that [[vk::binding(b, s)]] maps to layout(binding=b) in GLSL 4.30.
// GLSL 4.30 does not support descriptor sets — set index is flattened.

[[vk::binding(0, 0)]]
cbuffer FrameData {
    float4x4 viewProj;
    float    time;
};

[[vk::binding(1, 0)]]
StructuredBuffer<float4> vertices;

[[vk::binding(0, 1)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[tid.x] = mul(viewProj, vertices[tid.x]) * time;
}
