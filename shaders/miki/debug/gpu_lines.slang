// debug/gpu_lines.slang — GPU-driven debug line rendering.
//
// Provides data structures and utility functions for a GPU-driven debug line
// rendering system. Debug lines can be emitted from any shader stage by
// appending to an append buffer, then rendered as a separate pass.
//
// Architecture:
//   1. Any shader calls appendDebugLine() to push a line to the append buffer.
//   2. A draw-indirect compute pass sets IndirectArgs.vertexCount = lineCount * 2.
//   3. A VS reads the line buffer and expands each line to a screen-space quad
//      (or just draws GL_LINES / line topology).
//
// Reference: Gijs Kaerts "Implementing a GPU-driven debug line renderer" (2023).

implementing miki_debug;

import miki_core;

/// A single debug line segment with color.
public struct DebugLine {
    public float3 startPos;  // World-space start
    public uint   startColor; // RGBA8 packed
    public float3 endPos;    // World-space end
    public uint   endColor;  // RGBA8 packed
};

/// Pack float4 color [0,1]^4 into RGBA8 uint.
public uint packColorRGBA8(float4 color) {
    uint r = uint(saturate(color.r) * 255.0);
    uint g = uint(saturate(color.g) * 255.0);
    uint b = uint(saturate(color.b) * 255.0);
    uint a = uint(saturate(color.a) * 255.0);
    return (a << 24u) | (b << 16u) | (g << 8u) | r;
}

/// Unpack RGBA8 uint to float4 color.
public float4 unpackColorRGBA8(uint packed) {
    float r = float(packed & 0xFFu) / 255.0;
    float g = float((packed >> 8u) & 0xFFu) / 255.0;
    float b = float((packed >> 16u) & 0xFFu) / 255.0;
    float a = float((packed >> 24u) & 0xFFu) / 255.0;
    return float4(r, g, b, a);
}

/// Append a debug line to the GPU append buffer.
/// lineBuffer: RWStructuredBuffer<DebugLine> with atomic counter at element 0.
/// counterBuffer: RWStructuredBuffer<uint> for atomic append index.
/// line: the debug line to append.
/// maxLines: capacity of the line buffer (prevents overflow).
public void appendDebugLine(
    RWStructuredBuffer<DebugLine> lineBuffer,
    RWStructuredBuffer<uint> counterBuffer,
    DebugLine line,
    uint maxLines
) {
    uint index;
    InterlockedAdd(counterBuffer[0], 1u, index);
    if (index < maxLines) {
        lineBuffer[index] = line;
    }
}

/// Helper: create a solid-color debug line.
public DebugLine makeDebugLine(float3 start, float3 end, float4 color) {
    DebugLine l;
    l.startPos = start;
    l.endPos = end;
    uint c = packColorRGBA8(color);
    l.startColor = c;
    l.endColor = c;
    return l;
}

/// Helper: create a debug line with per-vertex colors.
public DebugLine makeDebugLineGradient(float3 start, float3 end, float4 startColor, float4 endColor) {
    DebugLine l;
    l.startPos = start;
    l.endPos = end;
    l.startColor = packColorRGBA8(startColor);
    l.endColor = packColorRGBA8(endColor);
    return l;
}

/// Emit a wireframe AABB as 12 debug lines.
public void emitDebugAABB(
    RWStructuredBuffer<DebugLine> lineBuffer,
    RWStructuredBuffer<uint> counterBuffer,
    float3 bboxMin,
    float3 bboxMax,
    float4 color,
    uint maxLines
) {
    float3 c0 = bboxMin;
    float3 c1 = float3(bboxMax.x, bboxMin.y, bboxMin.z);
    float3 c2 = float3(bboxMax.x, bboxMax.y, bboxMin.z);
    float3 c3 = float3(bboxMin.x, bboxMax.y, bboxMin.z);
    float3 c4 = float3(bboxMin.x, bboxMin.y, bboxMax.z);
    float3 c5 = float3(bboxMax.x, bboxMin.y, bboxMax.z);
    float3 c6 = bboxMax;
    float3 c7 = float3(bboxMin.x, bboxMax.y, bboxMax.z);
    // Bottom face
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c0, c1, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c1, c2, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c2, c3, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c3, c0, color), maxLines);
    // Top face
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c4, c5, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c5, c6, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c6, c7, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c7, c4, color), maxLines);
    // Vertical edges
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c0, c4, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c1, c5, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c2, c6, color), maxLines);
    appendDebugLine(lineBuffer, counterBuffer, makeDebugLine(c3, c7, color), maxLines);
}
