// Task + Mesh shader Blinn-Phong lighting — torus + cube meshlet demo.
// Task shader amplifies meshlets, mesh shader emits triangles from storage buffers.
//
// Layout:
//   Push constants: MVP, model, diffuse color, meshlet count
//   Set 0, binding 0: StructuredBuffer<Vertex>  (position + normal)
//   Set 0, binding 1: StructuredBuffer<uint>    (index buffer)
//   Set 0, binding 2: StructuredBuffer<Meshlet> (meshlet descriptors)

static const uint MAX_VERTICES  = 64;
static const uint MAX_PRIMITIVES = 124;

struct Vertex {
    float3 position;
    float3 normal;
};

struct Meshlet {
    uint vertexOffset;    // offset into global index buffer
    uint triangleCount;   // number of triangles in this meshlet
    uint vertexCount;     // number of unique vertices (for SetMeshOutputCounts)
};

struct PushConst {
    float4x4 mvp;
    float4x4 model;
    float3   diffuseColor;
    uint     meshletCount;
};

[[vk::push_constant]] ConstantBuffer<PushConst> pc;

[[vk::binding(0, 0)]] StructuredBuffer<Vertex>  vertices;
[[vk::binding(1, 0)]] StructuredBuffer<uint>    indices;
[[vk::binding(2, 0)]] StructuredBuffer<Meshlet> meshlets;

// ============================================================================
// Mesh output
// ============================================================================

struct MeshOutput {
    float4 clipPos   : SV_Position;
    float3 worldNorm : NORMAL;
    float3 worldPos  : TEXCOORD0;
};

// ============================================================================
// Task shader — one workgroup per batch of 32 meshlets
// ============================================================================

struct TaskPayload {
    uint meshletIndices[32];
    uint count;
};

[shader("amplification")]
[numthreads(32, 1, 1)]
void ts_main(uint dtid : SV_DispatchThreadID,
             uint gtid : SV_GroupThreadID,
             uint gid  : SV_GroupID) {
    // Each task workgroup handles up to 32 meshlets
    uint meshletIndex = gid * 32 + gtid;
    bool valid = meshletIndex < pc.meshletCount;

    // Count valid meshlets in this workgroup
    // Use simple approach: group thread 0 dispatches
    GroupMemoryBarrierWithGroupSync();

    TaskPayload payload;
    uint baseIndex = gid * 32;
    uint remaining = pc.meshletCount > baseIndex ? pc.meshletCount - baseIndex : 0;
    payload.count = min(remaining, 32);
    for (uint i = 0; i < payload.count; i++) {
        payload.meshletIndices[i] = baseIndex + i;
    }

    DispatchMesh(payload.count, 1, 1, payload);
}

// ============================================================================
// Mesh shader — one workgroup per meshlet
// ============================================================================

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
[shader("mesh")]
void ms_main(uint gtid      : SV_GroupThreadID,
             uint gid       : SV_GroupIndex,
             in payload TaskPayload taskPayload,
             OutputVertices<MeshOutput, MAX_VERTICES> verts,
             OutputIndices<uint3, MAX_PRIMITIVES> tris) {
    uint meshletIndex = taskPayload.meshletIndices[gid];
    if (gid >= taskPayload.count) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    Meshlet m = meshlets[meshletIndex];
    uint vertCount = min(m.vertexCount, MAX_VERTICES);
    uint triCount  = min(m.triangleCount, MAX_PRIMITIVES);
    SetMeshOutputCounts(vertCount, triCount);

    // Emit vertices — each thread handles multiple vertices if needed
    for (uint v = gtid; v < vertCount; v += 128) {
        uint globalIdx = indices[m.vertexOffset + v];
        Vertex vtx = vertices[globalIdx];

        MeshOutput o;
        o.clipPos   = mul(pc.mvp, float4(vtx.position, 1.0));
        float4 wp   = mul(pc.model, float4(vtx.position, 1.0));
        o.worldPos  = wp.xyz / wp.w;
        o.worldNorm = normalize(mul((float3x3)pc.model, vtx.normal));
        verts[v] = o;
    }

    // Emit triangles — each thread handles multiple triangles
    for (uint t = gtid; t < triCount; t += 128) {
        uint baseIdx = m.vertexOffset + vertCount + t * 3;
        uint3 tri;
        tri.x = indices[baseIdx + 0];
        tri.y = indices[baseIdx + 1];
        tri.z = indices[baseIdx + 2];
        tris[t] = tri;
    }
}

// ============================================================================
// Fragment shader — Blinn-Phong
// ============================================================================

[shader("fragment")]
float4 fs_main(MeshOutput input) : SV_Target {
    float3 N = normalize(input.worldNorm);
    float3 lightDir = normalize(float3(0.5, 1.0, 0.8));
    float3 viewDir  = normalize(float3(0.0, 0.0, 3.0) - input.worldPos);
    float3 H = normalize(lightDir + viewDir);

    float NdotL = max(dot(N, lightDir), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float spec  = pow(NdotH, 64.0);

    float3 ambient  = float3(0.08, 0.08, 0.12);
    float3 diffuse  = pc.diffuseColor * NdotL;
    float3 specular = float3(1.0, 1.0, 1.0) * spec * 0.5;

    return float4(ambient + diffuse + specular, 1.0);
}
