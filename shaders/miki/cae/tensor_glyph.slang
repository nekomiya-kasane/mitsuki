// cae/tensor_glyph.slang — Tensor glyph rendering for stress/strain visualization.
//
// Renders 2nd-order symmetric tensors as ellipsoid glyphs (superquadrics).
// The glyph shape encodes eigenvalues (size) and eigenvectors (orientation).
//
// Supported glyph types:
// - Ellipsoid: axes proportional to eigenvalues (most common for stress tensors)
// - Superquadric: Reynolds glyph for distinguishing linear/planar/spherical anisotropy
// - Cuboid: box glyph for simple visualization
//
// Reference: Kindlmann (2004) "Superquadric Tensor Glyphs",
//            ParaView TensorGlyph filter, VTK vtkTensorGlyph.

implementing miki_cae;

import miki_core;

/// Tensor glyph type.
public enum TensorGlyphType : uint {
    Ellipsoid = 0,
    Superquadric = 1,
    Cuboid = 2,
};

/// 3x3 symmetric tensor (6 independent components).
public struct SymTensor3 {
    public float xx, yy, zz;   // Diagonal
    public float xy, xz, yz;   // Off-diagonal (symmetric: xy=yx, etc.)
};

/// Eigendecomposition result for a 3x3 symmetric tensor.
public struct EigenResult {
    public float3 eigenvalues;  // Sorted: |e0| >= |e1| >= |e2|
    public float3 eigenvec0;    // Eigenvector for eigenvalue 0
    public float3 eigenvec1;    // Eigenvector for eigenvalue 1
    public float3 eigenvec2;    // Eigenvector for eigenvalue 2
};

/// Compute eigenvalues of a symmetric 3x3 tensor analytically (Cardano's formula).
/// This avoids iterative methods on GPU for better performance.
public float3 eigenvalues3x3(SymTensor3 t) {
    float p1 = t.xy * t.xy + t.xz * t.xz + t.yz * t.yz;
    if (p1 < 1e-12) {
        // Diagonal matrix
        float3 ev = float3(t.xx, t.yy, t.zz);
        // Sort descending by absolute value
        if (abs(ev.y) > abs(ev.x)) { float tmp = ev.x; ev.x = ev.y; ev.y = tmp; }
        if (abs(ev.z) > abs(ev.x)) { float tmp = ev.x; ev.x = ev.z; ev.z = tmp; }
        if (abs(ev.z) > abs(ev.y)) { float tmp = ev.y; ev.y = ev.z; ev.z = tmp; }
        return ev;
    }

    float q = (t.xx + t.yy + t.zz) / 3.0;
    float p2 = (t.xx - q) * (t.xx - q) + (t.yy - q) * (t.yy - q) + (t.zz - q) * (t.zz - q) + 2.0 * p1;
    float p = sqrt(p2 / 6.0);

    // B = (1/p) * (A - qI)
    float b00 = (t.xx - q) / p;
    float b11 = (t.yy - q) / p;
    float b22 = (t.zz - q) / p;
    float b01 = t.xy / p;
    float b02 = t.xz / p;
    float b12 = t.yz / p;

    float detB = b00 * (b11 * b22 - b12 * b12)
               - b01 * (b01 * b22 - b12 * b02)
               + b02 * (b01 * b12 - b11 * b02);
    float halfDetB = detB * 0.5;
    halfDetB = clamp(halfDetB, -1.0, 1.0);

    float phi = acos(halfDetB) / 3.0;
    float twoPiThird = 2.09439510239;

    float e0 = q + 2.0 * p * cos(phi);
    float e2 = q + 2.0 * p * cos(phi + twoPiThird);
    float e1 = 3.0 * q - e0 - e2;

    return float3(e0, e1, e2);
}

/// Build glyph transform matrix from eigendecomposition.
/// Returns a 4x4 matrix that transforms a unit sphere to the glyph shape.
public float4x4 tensorGlyphTransform(float3 position, EigenResult eigen, float scale) {
    float3 s = abs(eigen.eigenvalues) * scale;
    s = max(s, float3(0.001)); // Minimum size to avoid degenerate glyphs

    return float4x4(
        float4(eigen.eigenvec0 * s.x, 0.0),
        float4(eigen.eigenvec1 * s.y, 0.0),
        float4(eigen.eigenvec2 * s.z, 0.0),
        float4(position, 1.0)
    );
}

/// Color tensor glyph by anisotropy type:
/// - Spherical (all eigenvalues equal): blue
/// - Linear (one dominant eigenvalue): red
/// - Planar (two dominant eigenvalues): green
public float3 anisotropyColor(float3 eigenvalues) {
    float3 absEv = abs(eigenvalues);
    float sum = absEv.x + absEv.y + absEv.z;
    if (sum < 1e-10) return float3(0.5); // Isotropic/zero

    float cl = (absEv.x - absEv.y) / sum; // Linear anisotropy
    float cp = 2.0 * (absEv.y - absEv.z) / sum; // Planar anisotropy
    float cs = 3.0 * absEv.z / sum; // Spherical isotropy

    return float3(cl, cp, cs); // RGB encodes anisotropy type
}
