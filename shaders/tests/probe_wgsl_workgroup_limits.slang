// Probe: WGSL workgroup size limits.
// WGSL maxComputeInvocationsPerWorkgroup = 256 (typical).
// Validates a common workgroup size compiles for WGSL target.

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint lid : SV_GroupIndex)
{
    output[tid.x] = float(lid) / 256.0;
}
