// Probe: WGSL storage buffer alignment.
// WGSL requires 16-byte alignment for storage buffer members.
// Validates that struct layout satisfies WGSL alignment rules.

struct AlignedData {
    float4 position;       // 16 bytes, 16-byte aligned
    float4 velocity;       // 16 bytes, 16-byte aligned
    float  mass;           // 4 bytes
    float  _pad0;          // padding
    float  _pad1;
    float  _pad2;          // pad to 16-byte boundary
};

[[vk::binding(0, 0)]]
StructuredBuffer<AlignedData> input;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    AlignedData d = input[tid.x];
    output[tid.x] = d.position + d.velocity * d.mass;
}
