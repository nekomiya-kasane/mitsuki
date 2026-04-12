// geometry/lod.slang — LOD selection (implementing miki_geometry).
//
// Screen-space error metric for continuous LOD selection.
// Works with meshoptimizer's simplification error bounds.

implementing miki_geometry;

import miki_core;

/// LOD selection result.
public struct LODResult {
    public uint lodLevel;
    public float screenError;
};

/// Compute screen-space error for a given geometric error at a given distance.
/// errorWorld is the max geometric error of a LOD level (from meshoptimizer).
/// Returns screen-space error in pixels.
[ForceInline]
public float computeScreenError(float errorWorld, float distance, float cotHalfFov, float viewportHeight) {
    if (distance <= 0.0f) return 1e6f;  // infinitely close → use highest LOD
    return (errorWorld / distance) * cotHalfFov * viewportHeight * 0.5f;
}

/// Select the coarsest LOD whose screen error is below the threshold.
/// lodErrors: array of N LOD error values (ascending, [0]=highest detail).
/// Returns the LOD index to use.
[ForceInline]
public uint selectLOD(
    float distance, float cotHalfFov, float viewportHeight,
    float lodErrors[8], uint lodCount, float errorThreshold
) {
    uint selected = 0;
    [ForceUnroll]
    for (uint i = 0; i < 8; ++i) {
        if (i >= lodCount) break;
        float screenErr = computeScreenError(lodErrors[i], distance, cotHalfFov, viewportHeight);
        if (screenErr < errorThreshold) {
            selected = i;
        } else {
            break;
        }
    }
    return selected;
}
