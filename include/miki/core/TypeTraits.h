// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace miki::core {

    /// @brief Concept: type is GPU-uploadable.
    /// Requirements:
    ///   - Trivially copyable (memcpy-safe)
    ///   - Standard layout (predictable memory layout)
    ///   - Alignment >= 4 bytes (GPU minimum alignment)
    template <typename T>
    concept GpuUploadable = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && (alignof(T) >= 4);

    /// @brief Concept: type is a miki vector type (float2/3/4, uint2/3/4, int2/3/4).
    /// Checks for .x member and subscript operator.
    template <typename T>
    concept MikiVector = requires(T v) {
        { v.x };
        { v[0] };
        requires std::is_trivially_copyable_v<T>;
        requires std::is_standard_layout_v<T>;
    };

    /// @brief Concept: type is a miki matrix type (float2x2/3x3/4x4).
    /// Checks for column access and element access via [row, col] (C++23).
    template <typename T>
    concept MikiMatrix = requires(T m) {
        { m[0] };       // Column access
        { m[0, 0] };    // Element access (C++23 multidimensional subscript)
        { m.columns };  // Column count
        requires std::is_trivially_copyable_v<T>;
        requires std::is_standard_layout_v<T>;
    };

    /// @brief Concept: type is a scalar numeric type suitable for GPU.
    template <typename T>
    concept GpuScalar
        = std::is_arithmetic_v<T> && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

    /// @brief Concept: type is a 32-bit float (most common GPU type).
    template <typename T>
    concept Float32 = std::same_as<T, float>;

    /// @brief Concept: type is a 16-bit float (half precision).
    template <typename T>
    concept Float16 = sizeof(T) == 2 && std::is_trivially_copyable_v<T>;

    /// @brief Concept: type is suitable for uniform buffer.
    /// Must be 16-byte aligned per std140/std430 rules.
    template <typename T>
    concept UniformBufferCompatible = GpuUploadable<T> && (alignof(T) >= 16 || sizeof(T) <= 16);

    /// @brief Concept: type is suitable for storage buffer.
    /// Less strict than uniform buffer - 4-byte alignment is sufficient.
    template <typename T>
    concept StorageBufferCompatible = GpuUploadable<T>;

    /// @brief Concept: type is suitable for vertex attribute.
    template <typename T>
    concept VertexAttributeCompatible = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

    /// @brief Concept: type is suitable for push constants.
    /// Must fit within 128-256 bytes (backend-dependent limit).
    template <typename T>
    concept PushConstantCompatible = GpuUploadable<T> && (sizeof(T) <= 256);

    /// @brief Helper to check if a type has a specific alignment.
    template <typename T, std::size_t Alignment>
    concept HasAlignment = (alignof(T) == Alignment);

    /// @brief Helper to check if a type has at least a specific alignment.
    template <typename T, std::size_t MinAlignment>
    concept HasMinAlignment = (alignof(T) >= MinAlignment);

    /// @brief Helper to check if a type fits within a size limit.
    template <typename T, std::size_t MaxSize>
    concept FitsWithin = (sizeof(T) <= MaxSize);

    /// @brief Type trait to get the number of components in a vector type.
    template <typename T>
    struct VectorComponents {
        static constexpr std::size_t value = 1;  // Scalar default
    };

    template <typename T>
        requires requires {
            T::x;
            T::y;
        } && (!requires { T::z; })
    struct VectorComponents<T> {
        static constexpr std::size_t value = 2;
    };

    template <typename T>
        requires requires {
            T::x;
            T::y;
            T::z;
        } && (!requires { T::w; })
    struct VectorComponents<T> {
        static constexpr std::size_t value = 3;
    };

    template <typename T>
        requires requires {
            T::x;
            T::y;
            T::z;
            T::w;
        }
    struct VectorComponents<T> {
        static constexpr std::size_t value = 4;
    };

    template <typename T>
    inline constexpr std::size_t kVectorComponents = VectorComponents<T>::value;

    /// @brief Type trait to get the scalar type of a vector.
    template <typename T>
    struct VectorScalar {
        using type = T;  // Scalar default
    };

    template <typename T>
        requires requires { typename std::remove_cvref_t<decltype(std::declval<T>().x)>; }
    struct VectorScalar<T> {
        using type = std::remove_cvref_t<decltype(std::declval<T>().x)>;
    };

    template <typename T>
    using VectorScalarT = typename VectorScalar<T>::type;

    /// @brief Compile-time check for GPU struct padding.
    /// Helps catch alignment issues before runtime.
    template <typename T>
    consteval auto ValidateGpuStruct() -> bool {
        static_assert(std::is_trivially_copyable_v<T>, "GPU struct must be trivially copyable");
        static_assert(std::is_standard_layout_v<T>, "GPU struct must have standard layout");
        static_assert(alignof(T) >= 4, "GPU struct must have at least 4-byte alignment");
        return true;
    }

/// @brief Macro to validate GPU struct at compile time.
#define MIKI_VALIDATE_GPU_STRUCT(T) static_assert(::miki::core::ValidateGpuStruct<T>())

/// @brief Macro to ensure struct has expected size (catches padding issues).
#define MIKI_ASSERT_STRUCT_SIZE(T, expected_size) static_assert(sizeof(T) == (expected_size), "Unexpected struct size")

/// @brief Macro to ensure struct has expected alignment.
#define MIKI_ASSERT_STRUCT_ALIGN(T, expected_align) \
    static_assert(alignof(T) == (expected_align), "Unexpected struct alignment")

}  // namespace miki::core
