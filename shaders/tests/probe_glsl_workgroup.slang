// Probe: GLSL workgroup size.
// Validates that [numthreads(X,Y,Z)] maps to layout(local_size_x/y/z) in GLSL.

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(16, 8, 2)]
void main(uint3 tid : SV_DispatchThreadID, uint lid : SV_GroupIndex)
{
    output[tid.x + tid.y * 16 + tid.z * 16 * 8] = lid;
}
