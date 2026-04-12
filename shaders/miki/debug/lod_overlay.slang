// debug/lod_overlay.slang — Nanite-style LOD and meshlet overlay visualization.
//
// Colorizes geometry by LOD level, meshlet ID, cluster group, or screen-space
// error metric. Essential for validating Nanite-style continuous LOD selection
// and meshlet boundary correctness.
//
// Reference: UE5 Nanite debug views (LOD Coloring, Triangle Density, Cluster View).

implementing miki_debug;

import miki_core;

/// LOD overlay mode.
public enum LodOverlayMode : uint {
    LodLevel = 0,        // Color by discrete LOD level
    MeshletBoundary = 1, // Highlight meshlet boundaries
    ScreenError = 2,     // Color by screen-space error metric
    TriangleDensity = 3, // Color by triangles-per-pixel
    ClusterGroup = 4,    // Color by cluster group ID
};

/// Color a LOD level using a fixed palette (0=green, max=red).
/// maxLod: maximum LOD count for normalization (e.g. 8, 12, 25).
public float3 lodLevelColor(uint lodLevel, uint maxLod) {
    float t = saturate(float(lodLevel) / float(max(maxLod, 1u)));
    // Green (LOD0/finest) -> Yellow -> Red (coarsest)
    return float3(t, 1.0 - t, 0.0);
}

/// Highlight meshlet boundaries by drawing border pixels of each meshlet in white.
/// meshletLocalVertex: vertex index within current meshlet [0, maxVertices).
/// maxVertices: meshlet max vertex count (link-time constant, e.g. 64 or 128).
/// Returns 1.0 for border vertices, 0.0 for interior.
public float meshletBorderFactor(uint meshletLocalVertex, uint maxVertices) {
    // First and last vertices in meshlet strip are typically on boundary
    float t = float(meshletLocalVertex) / float(max(maxVertices - 1u, 1u));
    return (t < 0.02 || t > 0.98) ? 1.0 : 0.0;
}

/// Visualize screen-space error as heatmap.
/// screenError: projected error in pixels from LOD selection.
/// threshold: LOD switch threshold in pixels (e.g. 1.0).
public float3 screenErrorColor(float screenError, float threshold) {
    float ratio = screenError / max(threshold, 1e-6);
    // Below threshold: green. At threshold: yellow. Above: red.
    return float3(saturate(ratio), saturate(2.0 - ratio), 0.0);
}

/// Compute approximate triangle density (triangles per pixel) from screen-space area.
/// Uses ddx/ddy of position to estimate pixel footprint.
/// screenPos: clip-space XY of the fragment.
public float triangleDensity(float2 screenPos) {
    float2 dx = ddx(screenPos);
    float2 dy = ddy(screenPos);
    float pixelArea = abs(dx.x * dy.y - dx.y * dy.x);
    return 1.0 / max(pixelArea, 1e-10);
}

/// Colorize triangle density using turbo colormap.
/// density: triangles per pixel. logScale: map [1, 10^logScale] to [0, 1].
public float3 triangleDensityColor(float density, float logScale) {
    float t = saturate(log2(max(density, 1.0)) / logScale);
    // Reuse turbo colormap from attribute_viz
    return float3(t, 1.0 - abs(t - 0.5) * 2.0, 1.0 - t);
}

/// Combined LOD overlay dispatcher.
public float3 lodOverlay(
    LodOverlayMode mode,
    uint lodLevel,
    uint maxLod,
    uint meshletID,
    uint meshletLocalVertex,
    uint maxVertices,
    uint clusterGroupID,
    float screenError,
    float lodThreshold,
    float2 screenPos,
    float densityLogScale
) {
    switch (mode) {
        case LodOverlayMode::LodLevel:
            return lodLevelColor(lodLevel, maxLod);
        case LodOverlayMode::MeshletBoundary: {
            float3 meshColor = idToColor(meshletID);
            float border = meshletBorderFactor(meshletLocalVertex, maxVertices);
            return lerp(meshColor, float3(1.0), border);
        }
        case LodOverlayMode::ScreenError:
            return screenErrorColor(screenError, lodThreshold);
        case LodOverlayMode::TriangleDensity:
            return triangleDensityColor(triangleDensity(screenPos), densityLogScale);
        case LodOverlayMode::ClusterGroup:
            return idToColor(clusterGroupID);
        default:
            return float3(1.0, 0.0, 1.0);
    }
}
