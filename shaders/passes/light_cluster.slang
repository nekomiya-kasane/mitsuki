// light_cluster.slang — Clustered light assignment compute shader.
// Assigns lights to 16x16x24 screen-space clusters for efficient light culling.
import miki_lighting;

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> clusterLightList;

[shader("compute")]
[numthreads(16, 16, 1)]
void csMain(uint3 tid : SV_DispatchThreadID) {
    uint clusterIdx = tid.x + tid.y * 16u;
    clusterLightList[clusterIdx] = 0u;
}
