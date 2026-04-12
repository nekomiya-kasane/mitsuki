// math/matrix_utils.slang — Matrix utility functions (implementing miki_math).
//
// Inverse, transpose, normal matrix, projection helpers.
// Avoids redundant computation with cofactor-based inverse.

implementing miki_math;

/// Compute the 3x3 normal matrix (transpose of inverse of upper-left 3x3).
/// Input: model or modelView matrix.
[ForceInline]
public float3x3 normalMatrix(float4x4 m) {
    float3x3 upper = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    // For orthonormal matrices, transpose == inverse. For non-uniform scaling,
    // we need proper cofactor-based transpose-inverse.
    float3 c0 = cross(upper[1], upper[2]);
    float3 c1 = cross(upper[2], upper[0]);
    float3 c2 = cross(upper[0], upper[1]);
    float det = dot(upper[0], c0);
    float invDet = (abs(det) > 1e-10f) ? (1.0f / det) : 0.0f;
    return float3x3(c0 * invDet, c1 * invDet, c2 * invDet);
}

/// 4x4 matrix inverse using cofactor expansion.
/// Returns zero matrix if singular (det < epsilon).
public float4x4 inverse4x4(float4x4 m) {
    float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3];
    float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3];
    float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3];
    float a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3];

    float b00 = a00 * a11 - a01 * a10;
    float b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10;
    float b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11;
    float b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30;
    float b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30;
    float b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31;
    float b11 = a22 * a33 - a23 * a32;

    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (abs(det) < 1e-10f) return float4x4(0);

    float invDet = 1.0f / det;
    float4x4 result;
    result[0] = float4(
        ( a11 * b11 - a12 * b10 + a13 * b09) * invDet,
        (-a01 * b11 + a02 * b10 - a03 * b09) * invDet,
        ( a31 * b05 - a32 * b04 + a33 * b03) * invDet,
        (-a21 * b05 + a22 * b04 - a23 * b03) * invDet
    );
    result[1] = float4(
        (-a10 * b11 + a12 * b08 - a13 * b07) * invDet,
        ( a00 * b11 - a02 * b08 + a03 * b07) * invDet,
        (-a30 * b05 + a32 * b02 - a33 * b01) * invDet,
        ( a20 * b05 - a22 * b02 + a23 * b01) * invDet
    );
    result[2] = float4(
        ( a10 * b10 - a11 * b08 + a13 * b06) * invDet,
        (-a00 * b10 + a01 * b08 - a03 * b06) * invDet,
        ( a30 * b04 - a31 * b02 + a33 * b00) * invDet,
        (-a20 * b04 + a21 * b02 - a23 * b00) * invDet
    );
    result[3] = float4(
        (-a10 * b09 + a11 * b07 - a12 * b06) * invDet,
        ( a00 * b09 - a01 * b07 + a02 * b06) * invDet,
        (-a30 * b03 + a31 * b01 - a32 * b00) * invDet,
        ( a20 * b03 - a21 * b01 + a22 * b00) * invDet
    );
    return result;
}

/// Extract depth linearization parameters from projection matrix.
/// Returns float2(near, far) for perspective projection.
[ForceInline]
public float2 extractNearFar(float4x4 proj) {
    // For standard perspective: proj[2][2] = -f/(f-n), proj[3][2] = -fn/(f-n)
    // => n = proj[3][2] / proj[2][2], f = proj[3][2] / (proj[2][2] + 1)
    float A = proj[2][2];
    float B = proj[3][2];
    float n = B / A;
    float f = B / (A + 1.0f);
    return float2(n, f);
}

/// Linearize a depth buffer value (reverse-Z aware).
/// depthSample in [0,1], nearFar from extractNearFar().
[ForceInline]
public float linearizeDepth(float depthSample, float2 nearFar) {
    float n = nearFar.x;
    float f = nearFar.y;
    return (n * f) / (f + depthSample * (n - f));
}

/// Construct an infinite reverse-Z perspective projection matrix.
/// fovY in radians, aspect = width/height.
[ForceInline]
public float4x4 infiniteReverseZPerspective(float fovY, float aspect, float nearPlane) {
    float f = 1.0f / tan(fovY * 0.5f);
    float4x4 m = float4x4(0);
    m[0][0] = f / aspect;
    m[1][1] = f;
    m[2][2] = 0.0f;
    m[2][3] = -1.0f;
    m[3][2] = nearPlane;
    return m;
}
