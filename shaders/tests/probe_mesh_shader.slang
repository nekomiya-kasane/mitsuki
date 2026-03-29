// Probe: mesh shader (Tier1 feature).
// Validates mesh shader entry point with output primitives.
// Expected to compile on SPIR-V (with mesh shader extension),
// may fail on targets without mesh shader support.

struct MeshOutput {
    float4 position : SV_Position;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
[shader("mesh")]
void main(out vertices MeshOutput verts[3], out indices uint3 tris[1])
{
    SetMeshOutputCounts(3, 1);

    verts[0].position = float4(-0.5, -0.5, 0.0, 1.0);
    verts[1].position = float4( 0.5, -0.5, 0.0, 1.0);
    verts[2].position = float4( 0.0,  0.5, 0.0, 1.0);

    tris[0] = uint3(0, 1, 2);
}
