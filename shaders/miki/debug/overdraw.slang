// debug/overdraw.slang — Overdraw and pixel complexity heatmap.
//
// Counts how many fragments are shaded per pixel using an atomic UAV counter.
// Visualizes the count as a heatmap to identify overdraw hotspots.
//
// Technique: Each fragment atomically increments a per-pixel counter in a
// R32_UINT UAV. A fullscreen resolve pass converts counts to heatmap colors.
//
// Reference: RenderDoc overdraw visualization, UE5 Shader Complexity view.

implementing miki_debug;

import miki_core;

/// Increment the overdraw counter at the given pixel coordinate.
/// overdrawBuffer: RWTexture2D<uint> bound as UAV, same dimensions as render target.
public void incrementOverdraw(RWTexture2D<uint> overdrawBuffer, int2 pixelCoord) {
    uint original;
    InterlockedAdd(overdrawBuffer[pixelCoord], 1u, original);
}

/// Convert overdraw count to a heatmap color.
/// count: number of fragments shaded at this pixel.
/// maxExpected: expected maximum for normalization (e.g. 8 for forward, 16 for transparent).
public float3 overdrawHeatmap(uint count, uint maxExpected) {
    if (count == 0u) return float3(0.0);
    if (count == 1u) return float3(0.0, 0.0, 0.5); // Dark blue: ideal (1 fragment)

    float t = saturate(float(count - 1u) / float(max(maxExpected - 1u, 1u)));
    // Blue(1) -> Green(2-3) -> Yellow(4-6) -> Red(7+)
    float r = smoothstep(0.4, 0.7, t);
    float g = 1.0 - abs(t - 0.4) * 2.5;
    float b = 1.0 - smoothstep(0.0, 0.4, t);
    return saturate(float3(r, max(g, 0.0), b));
}

/// Shader complexity estimator: approximate ALU cost of current fragment.
/// Uses instruction count heuristic based on ddx/ddy divergence.
/// baseColor: the shaded output to measure complexity of.
public float shaderComplexity(float4 baseColor) {
    // Proxy: sum of partial derivative magnitudes correlates with texture/ALU cost
    float4 dx = ddx(baseColor);
    float4 dy = ddy(baseColor);
    return length(dx) + length(dy);
}

/// Convert shader complexity value to heatmap.
/// complexity: output of shaderComplexity().
/// maxComplexity: normalization ceiling (tune per scene).
public float3 complexityHeatmap(float complexity, float maxComplexity) {
    float t = saturate(complexity / max(maxComplexity, 1e-6));
    return float3(t, 1.0 - t, 0.0);
}
