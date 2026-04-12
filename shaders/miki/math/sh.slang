// math/sh.slang — Spherical Harmonics L0-L2 (implementing miki_math).
//
// 9-coefficient SH for irradiance and radiance transfer.
// Band 0: 1 coeff, Band 1: 3 coeffs, Band 2: 5 coeffs.
// Reference: Ramamoorthi & Hanrahan 2001, "An Efficient Representation
// for Irradiance Environment Maps".

implementing miki_math;

// SH basis constants (pre-divided by sqrt(4*pi))
static const float kSH_C0 = 0.282095f;              // 1 / (2*sqrt(pi))
static const float kSH_C1 = 0.488603f;              // sqrt(3) / (2*sqrt(pi))
static const float kSH_C2_0 = 1.092548f;            // sqrt(15) / (2*sqrt(pi))
static const float kSH_C2_1 = 0.315392f;            // sqrt(5) / (4*sqrt(pi))
static const float kSH_C2_2 = 0.546274f;            // sqrt(15) / (4*sqrt(pi))

/// SH9 coefficient storage (3 bands, RGB).
public struct SH9 {
    float3 coeffs[9];
};

/// Project a direction to L2 SH basis functions (9 coefficients).
[ForceInline]
public float[9] shBasis(float3 dir) {
    float[9] b;
    b[0] = kSH_C0;
    b[1] = kSH_C1 * dir.y;
    b[2] = kSH_C1 * dir.z;
    b[3] = kSH_C1 * dir.x;
    b[4] = kSH_C2_0 * dir.x * dir.y;
    b[5] = kSH_C2_0 * dir.y * dir.z;
    b[6] = kSH_C2_1 * (3.0f * dir.z * dir.z - 1.0f);
    b[7] = kSH_C2_0 * dir.x * dir.z;
    b[8] = kSH_C2_2 * (dir.x * dir.x - dir.y * dir.y);
    return b;
}

/// Evaluate L2 SH at a given direction (dot product with basis).
[ForceInline]
public float3 shEvaluate(SH9 sh, float3 dir) {
    float[9] basis = shBasis(dir);
    float3 result = float3(0);
    [ForceUnroll]
    for (int i = 0; i < 9; ++i) {
        result += sh.coeffs[i] * basis[i];
    }
    return result;
}

/// Accumulate a radiance sample into SH coefficients.
[ForceInline]
public void shAccumulate(inout SH9 sh, float3 dir, float3 radiance, float weight) {
    float[9] basis = shBasis(dir);
    [ForceUnroll]
    for (int i = 0; i < 9; ++i) {
        sh.coeffs[i] += radiance * basis[i] * weight;
    }
}

/// Scale all SH coefficients.
[ForceInline]
public SH9 shScale(SH9 sh, float s) {
    SH9 result;
    [ForceUnroll]
    for (int i = 0; i < 9; ++i) {
        result.coeffs[i] = sh.coeffs[i] * s;
    }
    return result;
}
