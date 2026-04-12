// material_resolve.slang — Deferred material resolve compute shader.
// Reads VisBuffer, reconstructs surface data, evaluates BRDF + lighting.
import miki_brdf;
import miki_lighting;

[[vk::binding(0, 0)]] Texture2D<uint> visBuffer;
[[vk::binding(1, 0)]] RWTexture2D<float4> outputColor;

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 tid : SV_DispatchThreadID) {
    uint2 pixel = tid.xy;
    uint visData = visBuffer[pixel];
    outputColor[pixel] = float4(0.0, 0.0, 0.0, 1.0);
}
