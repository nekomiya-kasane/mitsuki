// blit.slang — Blit / copy / format conversion utility shader.

struct VsOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 0)]] Texture2D<float4> sourceTexture;
[[vk::binding(1, 0)]] SamplerState linearSampler;

[shader("vertex")]
VsOutput vsMain(uint vertexId : SV_VertexID) {
    VsOutput output;
    float2 uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
    output.texCoord = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fsMain(VsOutput input) : SV_Target {
    return sourceTexture.Sample(linearSampler, input.texCoord);
}
