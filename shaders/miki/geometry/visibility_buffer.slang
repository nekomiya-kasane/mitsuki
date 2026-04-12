// geometry/visibility_buffer.slang — Visibility buffer encode/decode (implementing miki_geometry).
//
// Visibility Buffer (VisBuffer) stores (instanceId, primitiveId) per-pixel
// instead of full g-buffer attributes. Material evaluation is deferred to
// a fullscreen pass that fetches vertex data on demand.
// Encoding: upper 12 bits = instanceId, lower 20 bits = primitiveId.

implementing miki_geometry;

import miki_core;

/// Maximum supported instance count (2^12 = 4096).
public static const uint kMaxInstances = 4096u;
/// Maximum supported primitives per instance (2^20 = 1M).
public static const uint kMaxPrimitivesPerInstance = 1048576u;

/// Encode instanceId + primitiveId into a single uint32 for the visibility buffer.
[ForceInline]
public uint encodeVisBuffer(uint instanceId, uint primitiveId) {
    return (instanceId << 20u) | (primitiveId & 0xFFFFFu);
}

/// Decode instanceId from a visibility buffer value.
[ForceInline]
public uint decodeInstanceId(uint visValue) {
    return visValue >> 20u;
}

/// Decode primitiveId from a visibility buffer value.
[ForceInline]
public uint decodePrimitiveId(uint visValue) {
    return visValue & 0xFFFFFu;
}

/// Check if visibility buffer value is valid (non-zero).
[ForceInline]
public bool isVisBufferValid(uint visValue) {
    return visValue != 0u;
}

/// Compute barycentric coordinates from screen position and triangle vertices.
/// Used in the material evaluation pass after VisBuffer read.
[ForceInline]
public float3 computeBarycentrics(float2 screenPos, float2 v0, float2 v1, float2 v2) {
    float2 e0 = v1 - v0;
    float2 e1 = v2 - v0;
    float2 e2 = screenPos - v0;

    float d00 = dot(e0, e0);
    float d01 = dot(e0, e1);
    float d11 = dot(e1, e1);
    float d20 = dot(e2, e0);
    float d21 = dot(e2, e1);

    float denom = d00 * d11 - d01 * d01;
    if (abs(denom) < 1e-8f) return float3(1.0f, 0.0f, 0.0f);

    float invDenom = 1.0f / denom;
    float u = (d11 * d20 - d01 * d21) * invDenom;
    float v = (d00 * d21 - d01 * d20) * invDenom;
    return float3(1.0f - u - v, u, v);
}
