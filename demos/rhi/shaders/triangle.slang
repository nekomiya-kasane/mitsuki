// Simple fullscreen triangle — no vertex buffer needed.
// Positions and colors are generated from vertex index.

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR;
};

static const float2 kPositions[3] = {
    float2( 0.0,  0.6),
    float2(-0.6, -0.6),
    float2( 0.6, -0.6),
};

static const float3 kColors[3] = {
    float3(1.0, 0.2, 0.2),
    float3(0.2, 1.0, 0.2),
    float3(0.2, 0.2, 1.0),
};

[shader("vertex")]
VSOutput vs_main(uint vid : SV_VertexID) {
    VSOutput o;
    o.position = float4(kPositions[vid], 0.0, 1.0);
    o.color    = kColors[vid];
    return o;
}

[shader("fragment")]
float4 fs_main(VSOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
