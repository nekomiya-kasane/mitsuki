// bloom_pass.slang — Multi-pass bloom compute shader (downsample + upsample).
import miki_postfx;

[[vk::binding(0, 0)]] Texture2D<float4> inputTexture;
[[vk::binding(1, 0)]] RWTexture2D<float4> outputTexture;
[[vk::binding(2, 0)]] SamplerState linearSampler;

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 tid : SV_DispatchThreadID) {
    uint2 pixel = tid.xy;
    float4 color = inputTexture[pixel];
    outputTexture[pixel] = color;
}
