// Probe: GLSL 32-bit atomics.
// Validates atomicAdd / atomicOr on buffer in GLSL 4.30.
// Note: GLSL 4.30 only supports 32-bit atomics (no 64-bit).

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> counter;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint original;
    InterlockedAdd(counter[0], 1u, original);
    InterlockedOr(counter[1], (1u << (tid.x % 32)), original);
    InterlockedMax(counter[2], tid.x, original);
}
