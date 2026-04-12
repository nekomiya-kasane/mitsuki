// postfx/outline.slang — Object outline / selection highlight (implementing miki_postfx).
//
// Sobel-based edge detection on stencil/object-ID buffer.
// Used for CAD selection highlighting, wireframe overlay, silhouette edges.

implementing miki_postfx;

import miki_core;

/// Sobel edge detection on a uint ID buffer. Returns edge strength [0, 1].
[ForceInline]
public float sobelEdgeDetection(Texture2D<uint> idBuffer, int2 pos) {
    uint c = idBuffer.Load(int3(pos, 0)).r;
    uint n = idBuffer.Load(int3(pos + int2(0, -1), 0)).r;
    uint s = idBuffer.Load(int3(pos + int2(0, 1), 0)).r;
    uint e = idBuffer.Load(int3(pos + int2(1, 0), 0)).r;
    uint w = idBuffer.Load(int3(pos + int2(-1, 0), 0)).r;
    bool isEdge = (c != n) || (c != s) || (c != e) || (c != w);
    return isEdge ? 1.0f : 0.0f;
}

/// Dilated outline: returns 1 if any neighbor within radius has a different ID.
[ForceInline]
public float dilatedOutline(Texture2D<uint> idBuffer, int2 pos, uint targetId, int radius) {
    [ForceUnroll]
    for (int y = -radius; y <= radius; ++y) {
        [ForceUnroll]
        for (int x = -radius; x <= radius; ++x) {
            if (x == 0 && y == 0) continue;
            uint id = idBuffer.Load(int3(pos + int2(x, y), 0)).r;
            if (id == targetId) return 1.0f;
        }
    }
    return 0.0f;
}

/// Depth-based edge detection for wireframe/silhouette rendering.
[ForceInline]
public float depthEdge(Texture2D<float> depthBuffer, int2 pos, float threshold) {
    float c = depthBuffer.Load(int3(pos, 0)).r;
    float n = depthBuffer.Load(int3(pos + int2(0, -1), 0)).r;
    float s = depthBuffer.Load(int3(pos + int2(0, 1), 0)).r;
    float e = depthBuffer.Load(int3(pos + int2(1, 0), 0)).r;
    float w = depthBuffer.Load(int3(pos + int2(-1, 0), 0)).r;
    float maxDiff = max(max(abs(c - n), abs(c - s)), max(abs(c - e), abs(c - w)));
    return (maxDiff > threshold) ? 1.0f : 0.0f;
}
