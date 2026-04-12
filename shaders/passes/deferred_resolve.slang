// deferred_resolve.slang — Deferred shading resolve fragment shader.
// Reads G-buffer and computes final lighting per pixel.
import miki_brdf;
import miki_lighting;

struct VsOutput {
    float4 clipPos : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 0)]] Texture2D<float4> gBufferAlbedo;
[[vk::binding(1, 0)]] Texture2D<float4> gBufferNormal;
[[vk::binding(2, 0)]] Texture2D<float>  gBufferDepth;
[[vk::binding(3, 0)]] SamplerState pointSampler;

[shader("fragment")]
float4 fsMain(VsOutput input) : SV_Target {
    float4 albedo = gBufferAlbedo.Sample(pointSampler, input.texCoord);
    float4 normal = gBufferNormal.Sample(pointSampler, input.texCoord);
    return float4(albedo.rgb, 1.0);
}
