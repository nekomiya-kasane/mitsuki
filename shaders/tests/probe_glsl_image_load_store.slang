// Probe: GLSL imageLoad / imageStore.
// Validates RWTexture2D maps to layout(...) image2D in GLSL 4.30.

[[vk::binding(0, 0)]]
Texture2D<float4> inputTex;

[[vk::binding(1, 0)]]
RWTexture2D<float4> outputTex;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float4 color = inputTex.Load(int3(tid.xy, 0));
    outputTex[tid.xy] = float4(1.0 - color.rgb, color.a);
}
