// Probe: push constant layout.
// Validates correct push constant struct mapping.

struct PushData {
    float4x4 transform;
    float4   color;
    float    time;
    uint     frameIndex;
};

[[vk::push_constant]]
PushData pc;

[[vk::binding(0, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[tid.x] = mul(pc.transform, pc.color) * pc.time;
}
