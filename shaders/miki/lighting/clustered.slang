// lighting/clustered.slang — Clustered forward lighting (implementing miki_lighting).
//
// Divides the view frustum into 3D clusters (tiles × depth slices).
// Each cluster stores a list of affecting lights for O(1) light lookup.
// Cluster assignment runs as a compute pre-pass.

implementing miki_lighting;

import miki_core;

/// Link-time tunable: max lights per cluster.
public extern static const int kMaxClusterLights = 128;

/// Cluster grid dimensions.
public static const uint kClusterTileSize = 64u;
public static const uint kClusterDepthSlices = 24u;

/// Cluster light list entry.
public struct ClusterLightList {
    public uint offset;  // offset into global light index buffer
    public uint count;   // number of lights in this cluster
};

/// Compute cluster index from screen position and linear depth.
[ForceInline]
public uint3 computeClusterIndex(float2 screenPos, float linearDepth,
    float2 viewportSize, float nearZ, float farZ
) {
    uint2 tile = uint2(screenPos) / kClusterTileSize;
    // Logarithmic depth slicing for even distribution
    float logRatio = log2(farZ / nearZ);
    float logDepth = log2(linearDepth / nearZ);
    uint slice = uint(logDepth / logRatio * float(kClusterDepthSlices));
    slice = min(slice, kClusterDepthSlices - 1u);
    return uint3(tile, slice);
}

/// Linearize a cluster 3D index to a flat 1D index.
[ForceInline]
public uint linearizeClusterIndex(uint3 clusterIdx, uint numTilesX, uint numTilesY) {
    return clusterIdx.z * numTilesX * numTilesY + clusterIdx.y * numTilesX + clusterIdx.x;
}

/// Test if a point light affects a cluster AABB (sphere-AABB intersection).
[ForceInline]
public bool lightAffectsCluster(float3 lightPos, float lightRange, float3 clusterMin, float3 clusterMax) {
    float3 closest = clamp(lightPos, clusterMin, clusterMax);
    float3 diff = lightPos - closest;
    return dot(diff, diff) < lightRange * lightRange;
}
