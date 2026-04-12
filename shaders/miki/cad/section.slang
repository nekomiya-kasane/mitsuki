// cad/section.slang — Section plane and section volume for CAD.
//
// Implements GPU-accelerated cross-section rendering:
// - Single/multiple section planes with per-plane enable and cap coloring.
// - Section box (6-plane intersection volume) for region isolation.
// - Capping fill: fills the cross-section area with a solid color or hatch pattern.
//
// Technique: Fragment shader discard for clipped regions. Capping uses a
// stencil-based second pass or depth peeling to fill the cut surface.
//
// Reference: HOOPS Visualize cutting planes, OpenCascade AIS_Plane.
// Performance: < 0.1ms overhead per section plane at 1080p.

implementing miki_cad;

import miki_core;

/// Maximum number of simultaneous section planes.
public static const uint kMaxSectionPlanes = 8u;

/// Section plane definition (Hesse normal form: dot(normal, p) + distance = 0).
public struct SectionPlane {
    public float3 normal;    // Plane normal (outward = clipped away)
    public float  distance;  // Signed distance from origin
    public float4 capColor;  // Color for the capping fill (RGBA)
    public uint   flags;     // Bit 0: enabled, Bit 1: show cap, Bit 2: invert
};

/// Section volume (intersection of up to 6 planes forming a box).
public struct SectionBox {
    public SectionPlane planes[6]; // 6 planes defining the box
    public uint activePlaneCount;  // How many planes are active [0,6]
};

/// Test if a world-space point should be clipped by a section plane.
/// Returns true if the point should be DISCARDED.
public bool isClippedByPlane(float3 worldPos, SectionPlane plane) {
    if ((plane.flags & 1u) == 0u) return false; // Plane disabled
    float dist = dot(plane.normal, worldPos) + plane.distance;
    bool clipped = dist > 0.0;
    if ((plane.flags & 4u) != 0u) clipped = !clipped; // Invert
    return clipped;
}

/// Test if a point is clipped by any of the active section planes.
/// Returns true if the point should be DISCARDED.
public bool isClippedByPlanes(float3 worldPos, SectionPlane planes[kMaxSectionPlanes], uint planeCount) {
    for (uint i = 0; i < min(planeCount, kMaxSectionPlanes); i++) {
        if (isClippedByPlane(worldPos, planes[i])) return true;
    }
    return false;
}

/// Test if a point is inside a section box (intersection of all active planes).
/// Returns true if the point should be DISCARDED (outside the box).
public bool isClippedByBox(float3 worldPos, SectionBox box) {
    for (uint i = 0; i < min(box.activePlaneCount, 6u); i++) {
        if (isClippedByPlane(worldPos, box.planes[i])) return true;
    }
    return false;
}

/// Compute signed distance from point to section plane (for cap rendering).
/// Positive = clipped side, negative = visible side.
public float signedDistanceToPlane(float3 worldPos, SectionPlane plane) {
    return dot(plane.normal, worldPos) + plane.distance;
}

/// Generate a hatch pattern for section cap fill.
/// localPos: position projected onto the section plane (2D).
/// spacing: distance between hatch lines in world units.
/// lineWidth: width of each hatch line in world units.
/// angle: hatch angle in radians.
public float hatchPattern(float2 localPos, float spacing, float lineWidth, float angle) {
    float ca = cos(angle);
    float sa = sin(angle);
    float projDist = localPos.x * ca + localPos.y * sa;
    float phase = fmod(abs(projDist), spacing);
    return phase < lineWidth ? 1.0 : 0.0;
}
