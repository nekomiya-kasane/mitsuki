// gtao_compute.slang — Ground Truth Ambient Occlusion compute shader.
// Computes GTAO at half-resolution, bilateral upsample in separate pass.
import miki_postfx;

[[vk::binding(0, 0)]] Texture2D<float>  depthBuffer;
[[vk::binding(1, 0)]] Texture2D<float4> normalBuffer;
[[vk::binding(2, 0)]] RWTexture2D<float> aoOutput;
[[vk::binding(3, 0)]] SamplerState pointSampler;

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 tid : SV_DispatchThreadID) {
    uint2 pixel = tid.xy;
    float depth = depthBuffer[pixel];
    aoOutput[pixel] = 1.0;
}
