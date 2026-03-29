// Probe: GLSL shared memory.
// Validates that groupshared maps to 'shared' keyword in GLSL 4.30.

static const uint BLOCK_SIZE = 128;

groupshared float sharedData[BLOCK_SIZE];

[[vk::binding(0, 0)]]
StructuredBuffer<float> input;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(BLOCK_SIZE, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint lid : SV_GroupIndex)
{
    sharedData[lid] = input[tid.x];
    GroupMemoryBarrierWithGroupSync();

    float sum = 0.0;
    for (uint i = 0; i < BLOCK_SIZE; ++i)
        sum += sharedData[i];

    output[tid.x] = sum / float(BLOCK_SIZE);
}
