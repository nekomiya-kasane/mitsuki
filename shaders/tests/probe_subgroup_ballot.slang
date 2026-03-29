// Probe: subgroup ballot operation.
// Validates WavePrefixCountBits / WaveActiveBallot.

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    bool active = (tid.x % 2) == 0;
    uint4 ballot = WaveActiveBallot(active);
    output[tid.x] = ballot.x;
}
