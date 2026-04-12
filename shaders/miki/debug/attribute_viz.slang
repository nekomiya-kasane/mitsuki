// debug/attribute_viz.slang — Surface attribute visualization.
//
// Visualizes normals, tangents, bitangents, UVs, vertex colors, primitive IDs,
// instance IDs, and material IDs as false-color outputs.
// Used in debug render passes for shader/geometry validation.
//
// All functions produce linear-space RGB output suitable for direct framebuffer write.

implementing miki_debug;

import miki_core;

/// Debug visualization mode selector.
public enum DebugVizMode : uint {
    None = 0,
    WorldNormal = 1,
    FaceNormal = 2,
    Tangent = 3,
    Bitangent = 4,
    UV0 = 5,
    UV1 = 6,
    VertexColor = 7,
    PrimitiveID = 8,
    InstanceID = 9,
    MaterialID = 10,
    MeshletID = 11,
    Depth = 12,
    MotionVector = 13,
    Roughness = 14,
    Metallic = 15,
};

/// Remap a unit direction vector [-1,1]^3 to RGB [0,1]^3 for visualization.
public float3 normalToColor(float3 n) {
    return n * 0.5 + 0.5;
}

/// Visualize UV coordinates as RG color with blue=0.
public float3 uvToColor(float2 uv) {
    return float3(frac(uv), 0.0);
}

/// Generate a pseudorandom color from an integer ID (e.g. primitiveID, instanceID).
/// Uses Wang hash for perceptually distinct colors.
public float3 idToColor(uint id) {
    uint h = id;
    h = (h ^ 61u) ^ (h >> 16u);
    h = h + (h << 3u);
    h = h ^ (h >> 4u);
    h = h * 0x27d4eb2du;
    h = h ^ (h >> 15u);
    float r = float((h >> 0u) & 0xFFu) / 255.0;
    float g = float((h >> 8u) & 0xFFu) / 255.0;
    float b = float((h >> 16u) & 0xFFu) / 255.0;
    return float3(r, g, b);
}

/// Visualize linear depth as grayscale. nearPlane/farPlane for normalization.
public float3 depthToColor(float linearDepth, float nearPlane, float farPlane) {
    float normalized = saturate((linearDepth - nearPlane) / (farPlane - nearPlane));
    return float3(normalized);
}

/// Visualize 2D motion vector as color: R=horizontal, G=vertical, B=magnitude.
public float3 motionVectorToColor(float2 mv) {
    float mag = length(mv);
    return float3(mv * 0.5 + 0.5, saturate(mag * 10.0));
}

/// Visualize a scalar value [0,1] as a turbo colormap (better than jet).
/// Approximation of the Turbo colormap by Anton Mikhailov (Google Research).
public float3 turboColormap(float t) {
    t = saturate(t);
    float4 v4 = float4(1.0, t, t * t, t * t * t);
    float2 v2 = v4.zw * v4.z;
    float r = dot(v4, float4(0.13572138, 4.61539260, -42.66032258, 132.13108234))
            + dot(v2, float2(-152.94239396, 59.28637943));
    float g = dot(v4, float4(0.09140261, 2.19418839, 4.84296658, -14.18503333))
            + dot(v2, float2(4.27729857, 2.82956604));
    float b = dot(v4, float4(0.10667330, 12.64194608, -60.58204836, 110.36276771))
            + dot(v2, float2(-89.90310912, 27.34824973));
    return saturate(float3(r, g, b));
}

/// Dispatch debug visualization based on mode. Returns RGB output.
public float3 visualizeAttribute(
    DebugVizMode mode,
    float3 worldNormal,
    float3 faceNormal,
    float3 tangent,
    float3 bitangent,
    float2 uv0,
    float2 uv1,
    float4 vertexColor,
    uint primitiveID,
    uint instanceID,
    uint materialID,
    uint meshletID,
    float linearDepth,
    float2 motionVector,
    float roughness,
    float metallic,
    float nearPlane,
    float farPlane
) {
    switch (mode) {
        case DebugVizMode::WorldNormal:  return normalToColor(worldNormal);
        case DebugVizMode::FaceNormal:   return normalToColor(faceNormal);
        case DebugVizMode::Tangent:      return normalToColor(tangent);
        case DebugVizMode::Bitangent:    return normalToColor(bitangent);
        case DebugVizMode::UV0:          return uvToColor(uv0);
        case DebugVizMode::UV1:          return uvToColor(uv1);
        case DebugVizMode::VertexColor:  return vertexColor.rgb;
        case DebugVizMode::PrimitiveID:  return idToColor(primitiveID);
        case DebugVizMode::InstanceID:   return idToColor(instanceID);
        case DebugVizMode::MaterialID:   return idToColor(materialID);
        case DebugVizMode::MeshletID:    return idToColor(meshletID);
        case DebugVizMode::Depth:        return depthToColor(linearDepth, nearPlane, farPlane);
        case DebugVizMode::MotionVector: return motionVectorToColor(motionVector);
        case DebugVizMode::Roughness:    return turboColormap(roughness);
        case DebugVizMode::Metallic:     return float3(metallic);
        default:                         return float3(1.0, 0.0, 1.0); // Magenta = unknown
    }
}
