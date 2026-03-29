// Probe: explicit [[vk::binding]] mapping across multiple sets.
// Validates correct set/binding assignment.

[[vk::binding(0, 0)]]
cbuffer CameraData {
    float4x4 viewProj;
};

[[vk::binding(0, 1)]]
StructuredBuffer<float4> positions;

[[vk::binding(1, 1)]]
StructuredBuffer<float4> normals;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float4 pos = positions[tid.x];
    float4 nrm = normals[tid.x];
    output[tid.x] = mul(viewProj, pos) + nrm;
}
