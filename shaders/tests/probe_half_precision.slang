// Probe: half-precision (float16_t / half) support.
// Validates min16float / half type usage.

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    half a = half(1.5);
    half b = half(2.5);
    half c = a + b;
    output[tid.x] = float(c);
}
