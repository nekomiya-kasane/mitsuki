/** @brief Shared math utility functions for column-major float4x4 matrices.
 *
 * Header-only inline functions. Used by demos, tests, and engine internals.
 * Uses C++23 multi-dimensional operator[] (P2128R6) for clean matrix access:
 *   m[col]       -> float4& (column vector)
 *   m[row, col]  -> float&  (matrix element)
 *   v[i]         -> float&  (vector element)
 *
 * Convention: column-major. Vulkan clip space: Y-down, Z [0,1].
 * Namespace: miki::core
 */
#pragma once

#include "miki/core/Types.h"

#include <algorithm>
#include <cmath>

namespace miki::core {

    // ===========================================================================
    // float3 arithmetic
    // ===========================================================================

    [[nodiscard]] inline auto Vec3Add(float3 a, float3 b) -> float3 {
        return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
    }

    [[nodiscard]] inline auto Vec3Sub(float3 a, float3 b) -> float3 {
        return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
    }

    [[nodiscard]] inline auto Vec3Scale(float3 v, float s) -> float3 {
        return {.x = v.x * s, .y = v.y * s, .z = v.z * s};
    }

    [[nodiscard]] inline auto Vec3Dot(float3 a, float3 b) -> float {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    [[nodiscard]] inline auto Vec3Cross(float3 a, float3 b) -> float3 {
        return {.x = a.y * b.z - a.z * b.y, .y = a.z * b.x - a.x * b.z, .z = a.x * b.y - a.y * b.x};
    }

    [[nodiscard]] inline auto Vec3Length(float3 v) -> float {
        return std::sqrt(Vec3Dot(v, v));
    }

    [[nodiscard]] inline auto Vec3Normalize(float3 v) -> float3 {
        float len = Vec3Length(v);
        return (len < 1e-8f) ? v : Vec3Scale(v, 1.0f / len);
    }

    [[nodiscard]] inline auto Vec3Min(float3 a, float3 b) -> float3 {
        return {.x = std::min(a.x, b.x), .y = std::min(a.y, b.y), .z = std::min(a.z, b.z)};
    }

    [[nodiscard]] inline auto Vec3Max(float3 a, float3 b) -> float3 {
        return {.x = std::max(a.x, b.x), .y = std::max(a.y, b.y), .z = std::max(a.z, b.z)};
    }

    // ===========================================================================
    // float4x4 construction
    // ===========================================================================

    [[nodiscard]] inline auto MakeIdentity() -> float4x4 {
        float4x4 m{};
        m[0, 0] = 1.0f;
        m[1, 1] = 1.0f;
        m[2, 2] = 1.0f;
        m[3, 3] = 1.0f;
        return m;
    }

    [[nodiscard]] inline auto MakeTranslation(float x, float y, float z) -> float4x4 {
        auto m = MakeIdentity();
        m[0, 3] = x;
        m[1, 3] = y;
        m[2, 3] = z;
        return m;
    }

    [[nodiscard]] inline auto MakeScale(float s) -> float4x4 {
        auto m = MakeIdentity();
        m[0, 0] = s;
        m[1, 1] = s;
        m[2, 2] = s;
        return m;
    }

    [[nodiscard]] inline auto MakeRotateX(float rad) -> float4x4 {
        float c = std::cos(rad), s = std::sin(rad);
        auto m = MakeIdentity();
        m[1, 1] = c;
        m[1, 2] = -s;
        m[2, 1] = s;
        m[2, 2] = c;
        return m;
    }

    [[nodiscard]] inline auto MakeRotateY(float rad) -> float4x4 {
        float c = std::cos(rad), s = std::sin(rad);
        auto m = MakeIdentity();
        m[0, 0] = c;
        m[0, 2] = s;
        m[2, 0] = -s;
        m[2, 2] = c;
        return m;
    }

    [[nodiscard]] inline auto MakeRotateZ(float rad) -> float4x4 {
        float c = std::cos(rad), s = std::sin(rad);
        auto m = MakeIdentity();
        m[0, 0] = c;
        m[0, 1] = -s;
        m[1, 0] = s;
        m[1, 1] = c;
        return m;
    }

    [[nodiscard]] inline auto MakeRotateAxis(float3 axis, float rad) -> float4x4 {
        auto a = Vec3Normalize(axis);
        float c = std::cos(rad), s = std::sin(rad), t = 1.0f - c;
        auto m = MakeIdentity();
        m[0, 0] = t * a.x * a.x + c;
        m[0, 1] = t * a.x * a.y - s * a.z;
        m[0, 2] = t * a.x * a.z + s * a.y;
        m[1, 0] = t * a.x * a.y + s * a.z;
        m[1, 1] = t * a.y * a.y + c;
        m[1, 2] = t * a.y * a.z - s * a.x;
        m[2, 0] = t * a.x * a.z - s * a.y;
        m[2, 1] = t * a.y * a.z + s * a.x;
        m[2, 2] = t * a.z * a.z + c;
        return m;
    }

    // ===========================================================================
    // float4x4 multiply
    // ===========================================================================

    [[nodiscard]] inline auto Mul4x4(const float4x4& a, const float4x4& b) -> float4x4 {
        float4x4 r{};
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += a[row, k] * b[k, col];
                }
                r[row, col] = sum;
            }
        }
        return r;
    }

    [[nodiscard]] inline auto MulVec4(const float4x4& m, float4 v) -> float4 {
        float4 r{};
        for (int row = 0; row < 4; ++row) {
            r[row] = m[row, 0] * v[0] + m[row, 1] * v[1] + m[row, 2] * v[2] + m[row, 3] * v[3];
        }
        return r;
    }

    // ===========================================================================
    // float4x4 inverse (cofactor expansion)
    // ===========================================================================

    [[nodiscard]] inline auto Inverse4x4(const float4x4& m) -> float4x4 {
        // Shorthand: m[row,]ol) reads element

        float a2323 = m[2, 2] * m[3, 3] - m[2, 3] * m[3, 2];
        float a1323 = m[2, 1] * m[3, 3] - m[2, 3] * m[3, 1];
        float a1223 = m[2, 1] * m[3, 2] - m[2, 2] * m[3, 1];
        float a0323 = m[2, 0] * m[3, 3] - m[2, 3] * m[3, 0];
        float a0223 = m[2, 0] * m[3, 2] - m[2, 2] * m[3, 0];
        float a0123 = m[2, 0] * m[3, 1] - m[2, 1] * m[3, 0];
        float a2313 = m[1, 2] * m[3, 3] - m[1, 3] * m[3, 2];
        float a1313 = m[1, 1] * m[3, 3] - m[1, 3] * m[3, 1];
        float a1213 = m[1, 1] * m[3, 2] - m[1, 2] * m[3, 1];
        float a2312 = m[1, 2] * m[2, 3] - m[1, 3] * m[2, 2];
        float a1312 = m[1, 1] * m[2, 3] - m[1, 3] * m[2, 1];
        float a1212 = m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1];
        float a0313 = m[1, 0] * m[3, 3] - m[1, 3] * m[3, 0];
        float a0213 = m[1, 0] * m[3, 2] - m[1, 2] * m[3, 0];
        float a0312 = m[1, 0] * m[2, 3] - m[1, 3] * m[2, 0];
        float a0212 = m[1, 0] * m[2, 2] - m[1, 2] * m[2, 0];
        float a0113 = m[1, 0] * m[3, 1] - m[1, 1] * m[3, 0];
        float a0112 = m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0];

        float det = m[0, 0] * (m[1, 1] * a2323 - m[1, 2] * a1323 + m[1, 3] * a1223)
                    - m[0, 1] * (m[1, 0] * a2323 - m[1, 2] * a0323 + m[1, 3] * a0223)
                    + m[0, 2] * (m[1, 0] * a1323 - m[1, 1] * a0323 + m[1, 3] * a0123)
                    - m[0, 3] * (m[1, 0] * a1223 - m[1, 1] * a0223 + m[1, 2] * a0123);

        float invDet = 1.0f / det;
        float4x4 r{};
        r[0, 0] = invDet * (m[1, 1] * a2323 - m[1, 2] * a1323 + m[1, 3] * a1223);
        r[0, 1] = invDet * -(m[0, 1] * a2323 - m[0, 2] * a1323 + m[0, 3] * a1223);
        r[0, 2] = invDet * (m[0, 1] * a2313 - m[0, 2] * a1313 + m[0, 3] * a1213);
        r[0, 3] = invDet * -(m[0, 1] * a2312 - m[0, 2] * a1312 + m[0, 3] * a1212);
        r[1, 0] = invDet * -(m[1, 0] * a2323 - m[1, 2] * a0323 + m[1, 3] * a0223);
        r[1, 1] = invDet * (m[0, 0] * a2323 - m[0, 2] * a0323 + m[0, 3] * a0223);
        r[1, 2] = invDet * -(m[0, 0] * a2313 - m[0, 2] * a0313 + m[0, 3] * a0213);
        r[1, 3] = invDet * (m[0, 0] * a2312 - m[0, 2] * a0312 + m[0, 3] * a0212);
        r[2, 0] = invDet * (m[1, 0] * a1323 - m[1, 1] * a0323 + m[1, 3] * a0123);
        r[2, 1] = invDet * -(m[0, 0] * a1323 - m[0, 1] * a0323 + m[0, 3] * a0123);
        r[2, 2] = invDet * (m[0, 0] * a1313 - m[0, 1] * a0313 + m[0, 3] * a0113);
        r[2, 3] = invDet * -(m[0, 0] * a1312 - m[0, 1] * a0312 + m[0, 3] * a0112);
        r[3, 0] = invDet * -(m[1, 0] * a1223 - m[1, 1] * a0223 + m[1, 2] * a0123);
        r[3, 1] = invDet * (m[0, 0] * a1223 - m[0, 1] * a0223 + m[0, 2] * a0123);
        r[3, 2] = invDet * -(m[0, 0] * a1213 - m[0, 1] * a0213 + m[0, 2] * a0113);
        r[3, 3] = invDet * (m[0, 0] * a1212 - m[0, 1] * a0212 + m[0, 2] * a0112);
        return r;
    }

    // ===========================================================================
    // Camera / projection
    // ===========================================================================

    [[nodiscard]] inline auto MakeLookAt(float3 eye, float3 target, float3 up) -> float4x4 {
        auto f = Vec3Normalize(Vec3Sub(target, eye));
        auto s = Vec3Normalize(Vec3Cross(f, up));
        auto u = Vec3Cross(s, f);
        auto m = MakeIdentity();
        m[0, 0] = s.x;
        m[1, 0] = u.x;
        m[2, 0] = -f.x;
        m[0, 1] = s.y;
        m[1, 1] = u.y;
        m[2, 1] = -f.y;
        m[0, 2] = s.z;
        m[1, 2] = u.z;
        m[2, 2] = -f.z;
        m[0, 3] = -Vec3Dot(s, eye);
        m[1, 3] = -Vec3Dot(u, eye);
        m[2, 3] = Vec3Dot(f, eye);
        return m;
    }

    /** @brief Perspective projection (Vulkan clip space: Y-down, Z [0,1]). */
    [[nodiscard]] inline auto MakePerspective(float fovY, float aspect, float nearZ, float farZ) -> float4x4 {
        const float t = std::tan(fovY * 0.5f);
        float4x4 m{};
        m[0, 0] = 1.0f / (aspect * t);
        m[1, 1] = -1.0f / t;  // Vulkan Y-flip
        m[2, 2] = farZ / (nearZ - farZ);
        m[3, 2] = -1.0f;
        m[2, 3] = (nearZ * farZ) / (nearZ - farZ);
        return m;
    }

    /** @brief Perspective projection (WebGPU clip space: Y-up, Z [0,1]). */
    [[nodiscard]] inline auto MakePerspectiveWebGPU(float fovY, float aspect, float nearZ, float farZ) -> float4x4 {
        const float t = std::tan(fovY * 0.5f);
        float4x4 m{};
        m[0, 0] = 1.0f / (aspect * t);
        m[1, 1] = 1.0f / t;  // No Y-flip for WebGPU (Y-up)
        m[2, 2] = farZ / (nearZ - farZ);
        m[3, 2] = -1.0f;
        m[2, 3] = (nearZ * farZ) / (nearZ - farZ);
        return m;
    }

    /** @brief Orthographic projection (Vulkan clip space: Z [0,1]). */
    [[nodiscard]] inline auto MakeOrtho(float l, float r, float b, float t, float n, float f) -> float4x4 {
        float4x4 m{};
        m[0, 0] = 2.0f / (r - l);
        m[1, 1] = 2.0f / (t - b);
        m[2, 2] = 1.0f / (n - f);
        m[0, 3] = -(r + l) / (r - l);
        m[1, 3] = -(t + b) / (t - b);
        m[2, 3] = n / (n - f);
        m[3, 3] = 1.0f;
        return m;
    }

}  // namespace miki::core
