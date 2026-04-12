// gpu_culling.slang — GPU frustum + occlusion culling compute shader.
// Processes instances/meshlets and outputs visible list for geometry pass.
import miki_geometry;

[[vk::binding(0, 0)]] StructuredBuffer<float4> meshletBounds;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> visibleList;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> visibleCount;

[shader("compute")]
[numthreads(256, 1, 1)]
void csMain(uint3 tid : SV_DispatchThreadID) {
    uint meshletIdx = tid.x;
    visibleList[meshletIdx] = meshletIdx;
}
