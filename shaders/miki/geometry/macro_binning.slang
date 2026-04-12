// geometry/macro_binning.slang — Macro binning for tile-based rendering (implementing miki_geometry).
//
// Assigns meshlets/draw calls to coarse screen-space tiles (e.g. 16x16 or 32x32).
// Reduces overdraw by enabling per-tile draw list sorting and early depth rejection.

implementing miki_geometry;

import miki_core;

/// Macro bin tile configuration.
public static const uint kMacroBinTileSize = 16u;

/// Compute the macro bin index for a given screen-space position.
[ForceInline]
public uint2 computeBinIndex(float2 screenPos, uint tileSize) {
    return uint2(screenPos) / tileSize;
}

/// Compute a linear bin ID from 2D bin coordinates.
[ForceInline]
public uint linearBinId(uint2 binIndex, uint numBinsX) {
    return binIndex.y * numBinsX + binIndex.x;
}

/// Test if a screen-space AABB overlaps a macro bin tile.
[ForceInline]
public bool aabbOverlapsTile(float2 aabbMin, float2 aabbMax, uint2 tileMin, uint2 tileMax) {
    return !(aabbMax.x < float(tileMin.x) || aabbMin.x > float(tileMax.x)
          || aabbMax.y < float(tileMin.y) || aabbMin.y > float(tileMax.y));
}

/// Compute the range of macro bin tiles that an AABB overlaps.
[ForceInline]
public void computeBinRange(
    float2 aabbMin, float2 aabbMax, uint tileSize, uint numBinsX, uint numBinsY,
    out uint2 binMin, out uint2 binMax
) {
    binMin = uint2(clamp(aabbMin, float2(0.0f), float2(numBinsX * tileSize - 1, numBinsY * tileSize - 1))) / tileSize;
    binMax = uint2(clamp(aabbMax, float2(0.0f), float2(numBinsX * tileSize - 1, numBinsY * tileSize - 1))) / tileSize;
}
