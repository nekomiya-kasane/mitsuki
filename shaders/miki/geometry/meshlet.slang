// geometry/meshlet.slang — Meshlet data structures and access (implementing miki_geometry).
//
// Meshlet is the fundamental unit of mesh shader geometry. Each meshlet contains
// up to kMeshletMaxVertices vertices and kMeshletMaxPrimitives triangles.
// Sizing is vendor-adaptive via link-time constants (NV: 64V/126T, AMD: 128V/256T).

implementing miki_geometry;

import miki_core;

/// Link-time tunable meshlet sizing constants (§15.9).
/// Override at link time per vendor: NVidia 64/126, AMD RDNA3+ 128/256.
public extern static const int kMeshletMaxVertices = 64;
public extern static const int kMeshletMaxPrimitives = 126;
public extern static const int kMeshShaderWorkgroupSize = 64;
public extern static const int kTaskShaderMeshletCount = 32;

/// Meshlet descriptor: indices into the global vertex/index buffers.
public struct MeshletDescriptor {
    public uint vertexOffset;
    public uint triangleOffset;
    public uint vertexCount;
    public uint triangleCount;
};

/// Meshlet bounding data for GPU culling.
public struct MeshletBounds {
    public float3 center;
    public float radius;
    public float3 coneApex;
    public float3 coneAxis;
    public float coneCutoff;  // cos(angle/2 + pi/2), if < 0 the cone is > 180deg
};

/// Task shader payload passed to mesh shader (within 16KB AMD limit).
/// Budget: 136 bytes << 16KB.
public struct MeshPayload {
    public uint meshletIndices[32];  // kTaskShaderMeshletCount
    public uint meshletCount;
    public uint instanceId;
    public float4x4 worldToClip;
};

/// Fetch a meshlet descriptor from the global meshlet buffer.
[ForceInline]
public MeshletDescriptor fetchMeshlet(StructuredBuffer<MeshletDescriptor> meshletBuffer, uint meshletIndex) {
    return meshletBuffer[meshletIndex];
}

/// Fetch meshlet bounds for culling.
[ForceInline]
public MeshletBounds fetchMeshletBounds(StructuredBuffer<MeshletBounds> boundsBuffer, uint meshletIndex) {
    return boundsBuffer[meshletIndex];
}
