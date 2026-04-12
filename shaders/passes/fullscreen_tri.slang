// fullscreen_tri.slang — Fullscreen triangle vertex shader utility.
// Generates a single fullscreen triangle without vertex buffer.
// SV_VertexID 0,1,2 maps to the three corners of a triangle that covers the viewport.

struct VsOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[shader("vertex")]
VsOutput vsMain(uint vertexId : SV_VertexID) {
    VsOutput output;
    // Generate fullscreen triangle from vertex ID (0, 1, 2)
    float2 uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
    output.texCoord = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
