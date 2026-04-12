// tonemap_pass.slang — Tonemapping compute shader (ACES / AgX / neutral).
import miki_postfx;

[[vk::binding(0, 0)]] Texture2D<float4> hdrInput;
[[vk::binding(1, 0)]] RWTexture2D<float4> ldrOutput;

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 tid : SV_DispatchThreadID) {
    uint2 pixel = tid.xy;
    float4 hdr = hdrInput[pixel];
    ldrOutput[pixel] = hdr;
}
