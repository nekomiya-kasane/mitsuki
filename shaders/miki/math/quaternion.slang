// math/quaternion.slang — Quaternion math (implementing miki_math).
//
// Compact quaternion operations for skeletal animation, camera control,
// and physics integration. Uses (x, y, z, w) storage convention.

implementing miki_math;

/// Quaternion stored as float4: (x, y, z, w) where w is the scalar part.
public typedef float4 Quaternion;

/// Identity quaternion.
[ForceInline]
public Quaternion quatIdentity() {
    return float4(0, 0, 0, 1);
}

/// Quaternion from axis-angle (axis must be normalized, angle in radians).
[ForceInline]
public Quaternion quatFromAxisAngle(float3 axis, float angle) {
    float halfAngle = angle * 0.5f;
    float s = sin(halfAngle);
    return float4(axis * s, cos(halfAngle));
}

/// Quaternion multiplication: q1 * q2.
[ForceInline]
public Quaternion quatMul(Quaternion q1, Quaternion q2) {
    return float4(
        q1.w * q2.xyz + q2.w * q1.xyz + cross(q1.xyz, q2.xyz),
        q1.w * q2.w - dot(q1.xyz, q2.xyz)
    );
}

/// Conjugate (inverse for unit quaternions).
[ForceInline]
public Quaternion quatConjugate(Quaternion q) {
    return float4(-q.xyz, q.w);
}

/// Rotate a vector by a unit quaternion: q * v * q^-1.
[ForceInline]
public float3 quatRotate(Quaternion q, float3 v) {
    // Optimized formula: v + 2 * q.w * cross(q.xyz, v) + 2 * cross(q.xyz, cross(q.xyz, v))
    float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

/// Normalize a quaternion (handles near-zero gracefully).
[ForceInline]
public Quaternion quatNormalize(Quaternion q) {
    float len2 = dot(q, q);
    if (len2 < 1e-12f) return quatIdentity();
    return q * rsqrt(len2);
}

/// Spherical linear interpolation (slerp).
/// Handles near-parallel and anti-parallel cases.
public Quaternion quatSlerp(Quaternion q0, Quaternion q1, float t) {
    float cosHalfTheta = dot(q0, q1);

    // Ensure shortest path
    Quaternion q1s = q1;
    if (cosHalfTheta < 0.0f) {
        q1s = -q1;
        cosHalfTheta = -cosHalfTheta;
    }

    // Near-parallel: fall back to nlerp
    if (cosHalfTheta > 0.9995f) {
        return quatNormalize(lerp(q0, q1s, t));
    }

    float halfTheta = acos(cosHalfTheta);
    float sinHalfTheta = sqrt(1.0f - cosHalfTheta * cosHalfTheta);
    float a = sin((1.0f - t) * halfTheta) / sinHalfTheta;
    float b = sin(t * halfTheta) / sinHalfTheta;
    return quatNormalize(q0 * a + q1s * b);
}

/// Normalized linear interpolation (faster than slerp for small angles).
[ForceInline]
public Quaternion quatNlerp(Quaternion q0, Quaternion q1, float t) {
    float d = dot(q0, q1);
    Quaternion q1s = (d < 0.0f) ? -q1 : q1;
    return quatNormalize(lerp(q0, q1s, t));
}

/// Convert quaternion to 3x3 rotation matrix.
[ForceInline]
public float3x3 quatToMatrix(Quaternion q) {
    float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
    float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    return float3x3(
        float3(1.0f - yy - zz, xy + wz, xz - wy),
        float3(xy - wz, 1.0f - xx - zz, yz + wx),
        float3(xz + wy, yz - wx, 1.0f - xx - yy)
    );
}
