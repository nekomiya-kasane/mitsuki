// geometry/vertex_pipeline.slang — Traditional vertex pipeline fallback (implementing miki_geometry).
//
// Used on Tier 2+ backends (OpenGL, WebGPU) that lack mesh shader support.
// Provides a CompatVertexPipeline that implements IGeometryPipeline using
// traditional vertex/index buffer draw calls.

implementing miki_geometry;

import miki_core;

/// Vertex input from a traditional vertex buffer.
public struct VertexInput {
    public float3 position;
    public float3 normal;
    public float2 uv;
    public float4 tangent;  // w = handedness sign
};

/// Fetch a vertex from a structured vertex buffer.
[ForceInline]
public VertexInput fetchVertexInput(StructuredBuffer<VertexInput> vertexBuffer, uint index) {
    return vertexBuffer[index];
}

/// Transform vertex from object space to clip space.
[ForceInline]
public float4 objectToClip(float3 position, float4x4 modelViewProj) {
    return mul(modelViewProj, float4(position, 1.0f));
}

/// Transform normal from object space to world space (using inverse transpose).
[ForceInline]
public float3 objectNormalToWorld(float3 normal, float3x3 normalMatrix) {
    return normalize(mul(normalMatrix, normal));
}

/// Pack vertex output for interpolation across the rasterizer.
[ForceInline]
public VertexOutput packVertexOutput(float4 clipPos, float3 worldPos, float3 worldNormal, float2 uv) {
    VertexOutput out;
    out.clipPos = clipPos;
    out.worldPos = worldPos;
    out.normal = worldNormal;
    out.uv = uv;
    return out;
}
