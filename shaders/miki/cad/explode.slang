// cad/explode.slang — Explode view transform for CAD assembly visualization.
//
// Computes per-instance displacement for exploded assembly views.
// Each part moves radially outward from the assembly center (or a user-defined
// explosion origin) by a configurable factor.
//
// Supports:
// - Radial explosion (default): parts move along direction from center to part centroid.
// - Axial explosion: parts move along a specified axis (e.g. for shaft assemblies).
// - Group explosion: hierarchical explosion respecting assembly tree.
//
// Reference: CATIA Explode, SolidWorks Explode View, HOOPS assembly transform.

implementing miki_cad;

import miki_core;

/// Explode mode.
public enum ExplodeMode : uint {
    Radial = 0,  // Radial from center
    Axial = 1,   // Along a specified axis
};

/// Explode parameters.
public struct ExplodeParams {
    public float3 explosionCenter;   // World-space explosion origin
    public float3 explosionAxis;     // Axis for axial explosion (normalized)
    public float  explosionFactor;   // Displacement multiplier [0 = collapsed, 1 = normal, >1 = spread]
    public ExplodeMode mode;
};

/// Compute the explode displacement vector for a part.
/// partCentroid: world-space centroid of the part instance.
/// Returns the displacement vector to add to all vertices of this part.
public float3 computeExplodeDisplacement(float3 partCentroid, ExplodeParams params) {
    float3 direction;
    switch (params.mode) {
        case ExplodeMode::Radial: {
            direction = partCentroid - params.explosionCenter;
            float dist = length(direction);
            if (dist < 1e-6) return float3(0.0); // Part at center, no displacement
            direction = direction / dist; // Normalize
            return direction * dist * params.explosionFactor;
        }
        case ExplodeMode::Axial: {
            // Project part-to-center vector onto axis
            float3 offset = partCentroid - params.explosionCenter;
            float projDist = dot(offset, params.explosionAxis);
            return params.explosionAxis * projDist * params.explosionFactor;
        }
        default:
            return float3(0.0);
    }
}

/// Apply explode transform to a world-space position.
/// worldPos: original vertex position.
/// partCentroid: centroid of the containing part (per-instance data).
public float3 applyExplodeTransform(float3 worldPos, float3 partCentroid, ExplodeParams params) {
    float3 displacement = computeExplodeDisplacement(partCentroid, params);
    return worldPos + displacement;
}

/// Compute animated explode factor with smooth ease-in-out.
/// t: animation parameter [0, 1].
/// Returns smoothed factor.
public float animateExplodeFactor(float t) {
    // Smoothstep ease-in-out
    return t * t * (3.0 - 2.0 * t);
}
