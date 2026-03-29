// Probe: WGSL push constant → UBO mapping.
// WGSL has no push constants — must map to a 256-byte uniform buffer
// at bind group 0 slot 0. Validates Slang emits a uniform for push_constant.

struct PushData {
    float4x4 modelMatrix;
    float4   tint;
    float    opacity;
    uint     instanceId;
    float    _pad0;
    float    _pad1;
};

[[vk::push_constant]]
PushData pc;

[[vk::binding(0, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[tid.x] = mul(pc.modelMatrix, pc.tint) * pc.opacity
                   + float4(float(pc.instanceId), 0, 0, 0);
}
