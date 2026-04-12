// geometry/geometry_pipeline_interface.slang — IGeometryPipeline interface (§15.4.2).
//
// Abstract interface for geometry processing pipeline.
// Concrete implementations (MeshGeoPipeline, VertexGeoPipeline) are selected
// at link time via `extern struct GeoPipeline : IGeometryPipeline = <Impl>`.

implementing miki_geometry;

import miki_core;

/// Vertex data output from geometry processing. Implementations define content.
public struct VertexOutput {
    public float4 clipPos;
    public float3 worldPos;
    public float3 normal;
    public float2 uv;
};

/// IGeometryPipeline — dispatched statically at link time, zero runtime cost.
public interface IGeometryPipeline {
    /// Fetch vertex data for a given vertex index from the vertex buffer.
    VertexOutput fetchVertex(uint vertexIndex);

    /// Transform and project vertex to clip space.
    float4 transformToClip(VertexOutput v, float4x4 mvp);

    /// Encode primitive ID for visibility buffer (instanceId + primitiveId packed).
    uint encodePrimitiveId(uint instanceId, uint primitiveId);

    /// Frustum cull a meshlet bounding sphere against 6 frustum planes.
    bool cullMeshletFrustum(float3 center, float radius, float4 planes[6]);

    /// Cone cull a meshlet: reject if meshlet normal cone faces away from eye.
    bool cullMeshletCone(float3 coneApex, float3 coneAxis, float coneCutoff, float3 eyePos);

    /// Backface-cull a single triangle (degenerate emission pattern).
    bool cullTriangleBackface(float4 v0, float4 v1, float4 v2);

    /// Small-triangle cull: reject sub-pixel triangles.
    bool isSmallTriangle(float2 ndc0, float2 ndc1, float2 ndc2, float2 viewportSize);
};
