// Probe: compute shared (groupshared) memory.
// Validates groupshared declaration and barrier usage.

groupshared float sharedData[64];

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupThreadID)
{
    sharedData[gid.x] = float(gid.x);
    GroupMemoryBarrierWithGroupSync();

    uint readIdx = (gid.x + 1) % 64;
    output[tid.x] = sharedData[readIdx];
}
