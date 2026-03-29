// Probe: GLSL push constant → UBO mapping.
// In GLSL 4.30, [[vk::push_constant]] must map to a uniform block.
// Validates that the Slang compiler emits a UBO for push constants.

struct PushData {
    float4x4 modelMatrix;
    float4   color;
    float    time;
    uint     objectId;
};

[[vk::push_constant]]
PushData pc;

[[vk::binding(0, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[tid.x] = mul(pc.modelMatrix, pc.color) * pc.time + float4(float(pc.objectId), 0, 0, 0);
}
