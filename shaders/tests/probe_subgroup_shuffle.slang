// Probe: subgroup shuffle (WaveReadLaneAt).
// Validates cross-lane data exchange.

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> data;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float myVal = data[tid.x];
    float otherVal = WaveReadLaneAt(myVal, (tid.x + 1) % WaveGetLaneCount());
    data[tid.x] = myVal + otherVal;
}
