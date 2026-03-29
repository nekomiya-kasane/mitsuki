// Probe: subgroup clustered reduce (WaveActiveSum).
// Validates reduction across wave lanes.

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> data;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float val = data[tid.x];
    float sum = WaveActiveSum(val);
    data[tid.x] = sum;
}
