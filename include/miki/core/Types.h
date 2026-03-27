/** @brief GPU-compatible math and geometry types for the miki renderer.
 *
 * All types are value types with explicit alignment and padding for
 * GPU struct compatibility. static_assert enforces size invariants.
 *
 * Namespace: miki::core
 */
#pragma once

#include <cstdint>

namespace miki::core {

    // ---------------------------------------------------------------------------
    // Vector types
    // ---------------------------------------------------------------------------

    /** @brief 2-component float vector. */
    struct alignas(8) float2 {
        float x = 0.0f;
        float y = 0.0f;

        [[nodiscard]] constexpr auto operator[](int i) -> float& { return (&x)[i]; }
        [[nodiscard]] constexpr auto operator[](int i) const -> float { return (&x)[i]; }
    };
    static_assert(sizeof(float2) == 8);
    static_assert(alignof(float2) == 8);

    /** @brief 3-component float vector with explicit padding for GPU alignment.
     *
     * Padded to 16 bytes so arrays of float3 are GPU-friendly.
     * Use _pad for explicit padding — no implicit gaps.
     * operator[] accesses x/y/z (indices 0-2). Index 3 accesses _pad.
     */
    struct alignas(16) float3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float _pad = 0.0f;

        [[nodiscard]] constexpr auto operator[](int i) -> float& { return (&x)[i]; }
        [[nodiscard]] constexpr auto operator[](int i) const -> float { return (&x)[i]; }
    };
    static_assert(sizeof(float3) == 16);
    static_assert(alignof(float3) == 16);

    /** @brief 4-component float vector. */
    struct alignas(16) float4 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        [[nodiscard]] constexpr auto operator[](int i) -> float& { return (&x)[i]; }
        [[nodiscard]] constexpr auto operator[](int i) const -> float { return (&x)[i]; }
    };
    static_assert(sizeof(float4) == 16);
    static_assert(alignof(float4) == 16);

    /** @brief 2x2 column-major matrix. */
    struct alignas(8) float2x2 {
        float2 columns[2] = {};

        [[nodiscard]] constexpr auto operator[](int col) -> float2& { return columns[col]; }
        [[nodiscard]] constexpr auto operator[](int col) const -> float2 const& { return columns[col]; }
        [[nodiscard]] constexpr auto operator[](int row, int col) -> float& { return columns[col][row]; }
        [[nodiscard]] constexpr auto operator[](int row, int col) const -> float { return columns[col][row]; }
    };
    static_assert(sizeof(float2x2) == 16);
    static_assert(alignof(float2x2) == 8);

    /** @brief 3x3 column-major matrix (padded columns — each column is float3 = 16B). */
    struct alignas(16) float3x3 {
        float3 columns[3] = {};

        [[nodiscard]] constexpr auto operator[](int col) -> float3& { return columns[col]; }
        [[nodiscard]] constexpr auto operator[](int col) const -> float3 const& { return columns[col]; }
        [[nodiscard]] constexpr auto operator[](int row, int col) -> float& { return columns[col][row]; }
        [[nodiscard]] constexpr auto operator[](int row, int col) const -> float { return columns[col][row]; }
    };
    static_assert(sizeof(float3x3) == 48);
    static_assert(alignof(float3x3) == 16);

    /** @brief 4x4 column-major matrix.
     *
     * Single subscript: m[col] returns float4& (column vector).
     * Dual subscript (C++23): m[row, col] returns float& (matrix element).
     */
    struct alignas(16) float4x4 {
        float4 columns[4] = {};

        [[nodiscard]] constexpr auto operator[](int col) -> float4& { return columns[col]; }
        [[nodiscard]] constexpr auto operator[](int col) const -> float4 const& { return columns[col]; }
        [[nodiscard]] constexpr auto operator[](int row, int col) -> float& { return columns[col][row]; }
        [[nodiscard]] constexpr auto operator[](int row, int col) const -> float { return columns[col][row]; }
    };
    static_assert(sizeof(float4x4) == 64);
    static_assert(alignof(float4x4) == 16);

    // ---------------------------------------------------------------------------
    // Signed integer vector types
    // ---------------------------------------------------------------------------

    /** @brief 2-component signed integer vector. */
    struct alignas(8) int2 {
        int32_t x = 0;
        int32_t y = 0;

        [[nodiscard]] constexpr auto operator[](int i) -> int32_t& { return (&x)[i]; }
        [[nodiscard]] constexpr auto operator[](int i) const -> int32_t { return (&x)[i]; }
    };
    static_assert(sizeof(int2) == 8);
    static_assert(alignof(int2) == 8);

    // ---------------------------------------------------------------------------
    // Unsigned integer vector types
    // ---------------------------------------------------------------------------

    /** @brief 2-component unsigned integer vector. */
    struct alignas(8) uint2 {
        uint32_t x = 0;
        uint32_t y = 0;

        [[nodiscard]] constexpr auto operator[](int i) -> uint32_t& { return (&x)[i]; }
        [[nodiscard]] constexpr auto operator[](int i) const -> uint32_t { return (&x)[i]; }
    };
    static_assert(sizeof(uint2) == 8);
    static_assert(alignof(uint2) == 8);

    /** @brief 3-component unsigned integer vector with explicit padding. */
    struct alignas(16) uint3 {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t z = 0;
        uint32_t _pad = 0;

        [[nodiscard]] constexpr auto operator[](int i) -> uint32_t& { return (&x)[i]; }
        [[nodiscard]] constexpr auto operator[](int i) const -> uint32_t { return (&x)[i]; }
    };
    static_assert(sizeof(uint3) == 16);
    static_assert(alignof(uint3) == 16);

    /** @brief 4-component unsigned integer vector. */
    struct alignas(16) uint4 {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t z = 0;
        uint32_t w = 0;

        [[nodiscard]] constexpr auto operator[](int i) -> uint32_t& { return (&x)[i]; }
        [[nodiscard]] constexpr auto operator[](int i) const -> uint32_t { return (&x)[i]; }
    };
    static_assert(sizeof(uint4) == 16);
    static_assert(alignof(uint4) == 16);

    // ---------------------------------------------------------------------------
    // Geometry types
    // ---------------------------------------------------------------------------

    /** @brief Axis-aligned bounding box. */
    struct alignas(16) AABB {
        float3 min = {};
        float3 max = {};
    };
    static_assert(sizeof(AABB) == 32);
    static_assert(alignof(AABB) == 16);

    /** @brief Bounding sphere packed into 16 bytes.
     *
     * Radius is stored in the padding slot of float3 (center._pad = radius).
     * This gives sizeof == 16 while maintaining alignas(16).
     */
    struct alignas(16) BoundingSphere {
        float3 center = {};

        /** @brief Set the sphere radius (stored in center._pad). */
        constexpr void SetRadius(float iRadius) { center._pad = iRadius; }

        /** @brief Get the sphere radius. */
        [[nodiscard]] constexpr auto GetRadius() const -> float { return center._pad; }
    };
    static_assert(sizeof(BoundingSphere) == 16);
    static_assert(alignof(BoundingSphere) == 16);

    /** @brief Six frustum planes (left, right, bottom, top, near, far). */
    struct alignas(16) FrustumPlanes {
        float4 planes[6] = {};
    };
    static_assert(sizeof(FrustumPlanes) == 96);
    static_assert(alignof(FrustumPlanes) == 16);

    /** @brief Ray with origin and direction. */
    struct alignas(16) Ray {
        float3 origin = {};
        float3 direction = {};
    };
    static_assert(sizeof(Ray) == 32);
    static_assert(alignof(Ray) == 16);

    /** @brief Plane defined by normal + distance from origin.
     *
     * Distance is stored in the padding slot of float3 (normal._pad = distance).
     * This gives sizeof == 16 while maintaining alignas(16).
     */
    struct alignas(16) Plane {
        float3 normal = {};

        /** @brief Set the plane distance from origin (stored in normal._pad). */
        constexpr void SetDistance(float iDistance) { normal._pad = iDistance; }

        /** @brief Get the plane distance from origin. */
        [[nodiscard]] constexpr auto GetDistance() const -> float { return normal._pad; }
    };
    static_assert(sizeof(Plane) == 16);
    static_assert(alignof(Plane) == 16);

}  // namespace miki::core
