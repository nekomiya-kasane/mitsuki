// cad/boolean_preview.slang — Boolean operation preview for CAD.
//
// Implements real-time preview of CSG (Constructive Solid Geometry) operations
// using depth peeling and stencil-based visibility classification.
//
// Technique: Image-space boolean preview (Goldfeather algorithm variant):
// 1. Render tool body with front/back depth peeling into two layers.
// 2. For each fragment of the base body:
//    - Union: visible if either body is frontmost.
//    - Subtraction: visible if base is frontmost AND not inside tool volume.
//    - Intersection: visible if base is inside tool volume.
// 3. "Inside tool" test: fragment depth is between tool front and tool back depths.
//
// This does NOT produce exact geometry — it's a visualization-only preview.
// Exact boolean requires CPU/GPU CSG (Phase 7b kernel-level).
//
// Reference: Goldfeather (1989), HOOPS Visualize boolean preview.

implementing miki_cad;

import miki_core;

/// Boolean operation type.
public enum BooleanOp : uint {
    Union = 0,
    Subtraction = 1,
    Intersection = 2,
};

/// Test if a depth value is "inside" a tool volume (between front and back layers).
/// fragmentDepth: depth of the base body fragment.
/// toolFrontDepth: front depth of the tool body (nearest).
/// toolBackDepth: back depth of the tool body (farthest).
public bool isInsideToolVolume(float fragmentDepth, float toolFrontDepth, float toolBackDepth) {
    return fragmentDepth > toolFrontDepth && fragmentDepth < toolBackDepth;
}

/// Determine if a fragment should be visible for the given boolean operation.
/// baseDepth: depth of the base body fragment.
/// toolFront: front depth of the tool (from depth peeling pass).
/// toolBack: back depth of the tool (from depth peeling pass).
/// toolValid: true if the tool has valid depth at this pixel (tool covers this pixel).
public bool booleanPreviewVisible(
    BooleanOp op,
    float baseDepth,
    float toolFront,
    float toolBack,
    bool toolValid
) {
    if (!toolValid) {
        // No tool at this pixel
        return op != BooleanOp::Intersection; // Union/Sub: show base. Intersection: hide.
    }

    bool insideTool = isInsideToolVolume(baseDepth, toolFront, toolBack);

    switch (op) {
        case BooleanOp::Union:
            return true; // Always visible in union
        case BooleanOp::Subtraction:
            return !insideTool; // Visible only if NOT inside tool
        case BooleanOp::Intersection:
            return insideTool; // Visible only if inside tool
        default:
            return true;
    }
}

/// Color the boolean preview result:
/// - Base body visible: baseColor
/// - Tool surface visible (for union): toolColor
/// - Cut surface (for subtraction): capColor
public float4 booleanPreviewColor(
    BooleanOp op,
    bool baseVisible,
    bool toolCloser,
    float4 baseColor,
    float4 toolColor,
    float4 capColor
) {
    if (op == BooleanOp::Union && toolCloser) return toolColor;
    if (op == BooleanOp::Subtraction && !baseVisible) return capColor;
    return baseColor;
}
