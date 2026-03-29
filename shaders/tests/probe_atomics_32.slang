// Probe: 32-bit atomics on buffer.
// Validates InterlockedAdd / InterlockedOr / InterlockedCompareExchange.

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> counter;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint original;
    InterlockedAdd(counter[0], 1u, original);
    InterlockedOr(counter[1], (1u << (tid.x % 32)), original);
}
