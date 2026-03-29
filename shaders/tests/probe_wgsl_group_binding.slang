// Probe: WGSL @group/@binding syntax.
// Validates that [[vk::binding(b, s)]] maps to @group(s) @binding(b) in WGSL.
// Tests multiple groups and bindings.

[[vk::binding(0, 0)]]
cbuffer Params {
    float4x4 transform;
    float    scale;
};

[[vk::binding(0, 1)]]
StructuredBuffer<float4> positions;

[[vk::binding(1, 1)]]
StructuredBuffer<float4> colors;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float4 pos = mul(transform, positions[tid.x]) * scale;
    output[tid.x] = pos + colors[tid.x];
}
