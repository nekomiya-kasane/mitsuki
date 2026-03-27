// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace miki::core {

    // FNV-1a constants
    inline constexpr uint64_t kFnv1aOffset64 = 14695981039346656037ULL;
    inline constexpr uint64_t kFnv1aPrime64 = 1099511628211ULL;
    inline constexpr uint32_t kFnv1aOffset32 = 2166136261U;
    inline constexpr uint32_t kFnv1aPrime32 = 16777619U;

    /// @brief FNV-1a 64-bit hash (compile-time capable).
    /// @param str String to hash.
    /// @return 64-bit hash value.
    [[nodiscard]] constexpr auto Fnv1a64(std::string_view str) noexcept -> uint64_t {
        uint64_t hash = kFnv1aOffset64;
        for (char c : str) {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            hash *= kFnv1aPrime64;
        }
        return hash;
    }

    /// @brief FNV-1a 64-bit hash for byte span.
    /// @param data Byte span to hash.
    /// @return 64-bit hash value.
    [[nodiscard]] constexpr auto Fnv1a64(std::span<const std::byte> data) noexcept -> uint64_t {
        uint64_t hash = kFnv1aOffset64;
        for (std::byte b : data) {
            hash ^= static_cast<uint64_t>(b);
            hash *= kFnv1aPrime64;
        }
        return hash;
    }

    /// @brief FNV-1a 32-bit hash (compile-time capable).
    /// @param str String to hash.
    /// @return 32-bit hash value.
    [[nodiscard]] constexpr auto Fnv1a32(std::string_view str) noexcept -> uint32_t {
        uint32_t hash = kFnv1aOffset32;
        for (char c : str) {
            hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
            hash *= kFnv1aPrime32;
        }
        return hash;
    }

    /// @brief Combine two hash values (boost::hash_combine equivalent).
    /// Uses the golden ratio-based mixing from boost.
    /// @param seed Current hash seed.
    /// @param value Hash value to combine.
    /// @return Combined hash value.
    [[nodiscard]] constexpr auto HashCombine(uint64_t seed, uint64_t value) noexcept -> uint64_t {
        // Golden ratio constant for 64-bit
        constexpr uint64_t kGoldenRatio = 0x9e3779b97f4a7c15ULL;
        seed ^= value + kGoldenRatio + (seed << 12) + (seed >> 4);
        return seed;
    }

    /// @brief 32-bit hash combine.
    [[nodiscard]] constexpr auto HashCombine32(uint32_t seed, uint32_t value) noexcept -> uint32_t {
        constexpr uint32_t kGoldenRatio = 0x9e3779b9U;
        seed ^= value + kGoldenRatio + (seed << 6) + (seed >> 2);
        return seed;
    }

    /// @brief Hash for trivially copyable types (GPU types like float3, float4x4, AABB, etc.).
    /// @tparam T Trivially copyable type.
    /// @param value Value to hash.
    /// @return 64-bit hash value.
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] constexpr auto HashTrivial(const T& value) noexcept -> uint64_t {
        if constexpr (sizeof(T) == 0) {
            return kFnv1aOffset64;
        } else if (std::is_constant_evaluated()) {
            // Compile-time: byte-by-byte via bit_cast to array
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
            uint64_t hash = kFnv1aOffset64;
            for (std::byte b : bytes) {
                hash ^= static_cast<uint64_t>(b);
                hash *= kFnv1aPrime64;
            }
            return hash;
        } else {
            // Runtime: direct memory access
            const auto* ptr = reinterpret_cast<const std::byte*>(&value);
            return Fnv1a64(std::span<const std::byte>{ptr, sizeof(T)});
        }
    }

    /// @brief Hash multiple values together.
    /// @tparam Args Trivially copyable types.
    /// @param args Values to hash.
    /// @return Combined 64-bit hash.
    template <typename... Args>
        requires(std::is_trivially_copyable_v<Args> && ...)
    [[nodiscard]] constexpr auto HashMultiple(const Args&... args) noexcept -> uint64_t {
        uint64_t seed = kFnv1aOffset64;
        ((seed = HashCombine(seed, HashTrivial(args))), ...);
        return seed;
    }

    /// @brief Compile-time string hash literal operator.
    /// @example constexpr auto hash = "hello"_hash;
    [[nodiscard]] consteval auto operator""_hash(const char* str, std::size_t len) noexcept -> uint64_t {
        return Fnv1a64(std::string_view{str, len});
    }

    /// @brief 32-bit compile-time string hash literal operator.
    [[nodiscard]] consteval auto operator""_hash32(const char* str, std::size_t len) noexcept -> uint32_t {
        return Fnv1a32(std::string_view{str, len});
    }

    /// @brief Murmur3-style finalizer for better avalanche.
    /// Use when hash quality matters more than speed.
    [[nodiscard]] constexpr auto MurmurFinalize64(uint64_t h) noexcept -> uint64_t {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }

    /// @brief Murmur3-style 32-bit finalizer.
    [[nodiscard]] constexpr auto MurmurFinalize32(uint32_t h) noexcept -> uint32_t {
        h ^= h >> 16;
        h *= 0x85ebca6bU;
        h ^= h >> 13;
        h *= 0xc2b2ae35U;
        h ^= h >> 16;
        return h;
    }

    /// @brief std::hash-compatible hasher for use with unordered containers.
    /// @tparam T Type to hash (must be trivially copyable).
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    struct TrivialHasher {
        [[nodiscard]] constexpr auto operator()(const T& value) const noexcept -> std::size_t {
            if constexpr (sizeof(std::size_t) == 8) {
                return static_cast<std::size_t>(HashTrivial(value));
            } else {
                return static_cast<std::size_t>(HashTrivial(value) & 0xFFFFFFFFULL);
            }
        }
    };

    /// @brief String view hasher for use with unordered containers.
    struct StringViewHasher {
        using is_transparent = void;  // Enable heterogeneous lookup

        [[nodiscard]] constexpr auto operator()(std::string_view str) const noexcept -> std::size_t {
            if constexpr (sizeof(std::size_t) == 8) {
                return static_cast<std::size_t>(Fnv1a64(str));
            } else {
                return static_cast<std::size_t>(Fnv1a32(str));
            }
        }
    };

}  // namespace miki::core
