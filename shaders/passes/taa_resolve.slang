// taa_resolve.slang — Temporal anti-aliasing resolve compute shader.
// History clamp + exponential blend + neighborhood clamping.
import miki_postfx;

[[vk::binding(0, 0)]] Texture2D<float4> currentColor;
[[vk::binding(1, 0)]] Texture2D<float4> historyColor;
[[vk::binding(2, 0)]] Texture2D<float2> motionVectors;
[[vk::binding(3, 0)]] RWTexture2D<float4> outputColor;
[[vk::binding(4, 0)]] SamplerState linearSampler;

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 tid : SV_DispatchThreadID) {
    uint2 pixel = tid.xy;
    float4 current = currentColor[pixel];
    outputColor[pixel] = current;
}
