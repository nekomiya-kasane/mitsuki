// Probe: WGSL 64-bit atomics (expected failure).
// WGSL does NOT support 64-bit atomics — this probe should FAIL compilation
// when targeting WGSL. Used to verify expected-failure handling.

[[vk::binding(0, 0)]]
RWStructuredBuffer<int64_t> counter;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    int64_t original;
    InterlockedAdd(counter[0], int64_t(1), original);
}
