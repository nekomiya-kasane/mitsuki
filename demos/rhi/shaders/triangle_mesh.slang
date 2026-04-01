// Mesh shader triangle — no vertex shader, Vulkan 1.4 / D3D12 mesh shader path.
// Emits a single triangle primitive with 3 vertices directly from mesh shader.

struct MeshOutput {
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

[outputtopology("triangle")]
[numthreads(3, 1, 1)]
[shader("mesh")]
void ms_main(uint tid : SV_GroupThreadID,
             OutputVertices<MeshOutput, 3> verts,
             OutputIndices<uint3, 1> tris) {
    SetMeshOutputCounts(3, 1);
    verts[tid] = { float4(kPositions[tid], 0.0, 1.0), kColors[tid] };
    if (tid == 0) {
        tris[0] = uint3(0, 1, 2);
    }
}

[shader("fragment")]
float4 fs_mesh_main(MeshOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
