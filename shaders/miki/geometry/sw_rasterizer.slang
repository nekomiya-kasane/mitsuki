// geometry/sw_rasterizer.slang — Software rasterizer fallback (implementing miki_geometry).
//
// Handles sub-pixel triangles that HW rasterizer may drop (Nanite approach).
// Rasterizes directly to a UAV framebuffer using atomicMin on depth + payload.
// Only used for triangles that fail the small-triangle cull threshold but
// are still visible (typically < 2 pixels).

implementing miki_geometry;

import miki_core;

/// Packed SW rasterizer output: depth (upper 32 bits) + visibility value (lower 32 bits).
/// Uses uint64 atomic operations for correct depth ordering.
public struct SWRasterOutput {
    public uint depth;   // quantized depth as uint (higher = farther for reversed-Z comparison)
    public uint visId;   // encodeVisBuffer result
};

/// Pack depth + visibility into a uint64 for atomic comparison.
/// Reversed-Z: smaller float depth = closer = should win atomicMin on packed uint64.
/// We negate and map to uint so that atomicMin picks the CLOSEST fragment.
[ForceInline]
public uint packSWDepthPayload(float depth, uint visId) {
    // Map [0,1] reversed-Z depth to [0, 2^32-1] where CLOSER = SMALLER uint.
    // In reversed-Z: depth=1 is near plane, depth=0 is far plane.
    // We want near (depth=1) to be smallest uint → invert.
    uint quantized = uint((1.0f - depth) * 4294967295.0f);
    return quantized;  // Use separate buffer for visId to avoid 64-bit atomics on some HW
}

/// Rasterize a single point into the UAV depth buffer using atomicMin.
/// Returns true if this fragment won the depth test.
[ForceInline]
public bool rasterizePoint(
    RWTexture2D<uint> depthUAV, uint2 pixelCoord, uint packedDepth
) {
    uint original;
    InterlockedMin(depthUAV[pixelCoord], packedDepth, original);
    return packedDepth <= original;
}
