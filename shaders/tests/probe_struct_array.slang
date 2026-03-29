// Probe: nested struct arrays with padding.
// Validates correct layout of struct arrays in SSBO.

struct Inner {
    float3 pos;
    float  weight;
};

struct Outer {
    Inner items[4];
    float scale;
    uint  count;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Outer> inputData;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    Outer o = inputData[tid.x];
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        sum += o.items[i].weight;
    }
    output[tid.x] = sum * o.scale;
}
