// Probe: 64-bit atomics (Tier1 feature).
// Validates InterlockedAdd on uint64_t typed buffer.
// Expected to fail on Tier3/4 targets.

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint64_t> counter;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint64_t original;
    InterlockedAdd(counter[0], uint64_t(1), original);
}
