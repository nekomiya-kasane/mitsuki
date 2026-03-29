// Probe: memory vs execution barriers.
// Validates AllMemoryBarrier and GroupMemoryBarrier.

groupshared uint sharedCounter[1];

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupThreadID)
{
    if (gid.x == 0) {
        sharedCounter[0] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint original;
    InterlockedAdd(sharedCounter[0], 1u, original);
    GroupMemoryBarrierWithGroupSync();

    if (gid.x == 0) {
        output[tid.x / 64] = sharedCounter[0];
    }
}
