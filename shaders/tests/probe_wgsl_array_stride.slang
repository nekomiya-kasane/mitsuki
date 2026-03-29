// Probe: WGSL array stride constraints.
// WGSL requires explicit stride for runtime-sized arrays.
// Validates that array<T> in structured buffers compiles correctly.

struct Element {
    float4 data;
    uint   index;
    uint   _pad0;
    uint   _pad1;
    uint   _pad2;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Element> elements;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    Element e = elements[tid.x];
    output[tid.x] = e.data * float(e.index);
}
