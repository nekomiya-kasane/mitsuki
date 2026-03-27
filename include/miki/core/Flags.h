// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <bit>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <type_traits>

namespace miki::core {

    /// @brief Type-safe bitfield wrapper for scoped enums.
    /// @tparam E Scoped enum type with power-of-2 values.
    /// @example
    ///   enum class MyFlags : uint32_t { None = 0, A = 1, B = 2, C = 4 };
    ///   using MyFlagsSet = EnumFlags<MyFlags>;
    ///   MyFlagsSet flags = MyFlags::A | MyFlags::B;
    ///   if (flags.Has(MyFlags::A)) { ... }
    template <typename E>
        requires std::is_enum_v<E>
    class EnumFlags {
       public:
        using EnumType = E;
        using UnderlyingType = std::underlying_type_t<E>;

        constexpr EnumFlags() noexcept = default;
        constexpr EnumFlags(E value) noexcept : bits_(static_cast<UnderlyingType>(value)) {}
        constexpr explicit EnumFlags(UnderlyingType bits) noexcept : bits_(bits) {}

        [[nodiscard]] constexpr auto Has(E flag) const noexcept -> bool {
            auto f = static_cast<UnderlyingType>(flag);
            return (bits_ & f) == f;
        }

        [[nodiscard]] constexpr auto HasAny(EnumFlags other) const noexcept -> bool {
            return (bits_ & other.bits_) != 0;
        }

        [[nodiscard]] constexpr auto HasAll(EnumFlags other) const noexcept -> bool {
            return (bits_ & other.bits_) == other.bits_;
        }

        constexpr auto Set(E flag) noexcept -> EnumFlags& {
            bits_ |= static_cast<UnderlyingType>(flag);
            return *this;
        }

        constexpr auto Clear(E flag) noexcept -> EnumFlags& {
            bits_ &= ~static_cast<UnderlyingType>(flag);
            return *this;
        }

        constexpr auto Toggle(E flag) noexcept -> EnumFlags& {
            bits_ ^= static_cast<UnderlyingType>(flag);
            return *this;
        }

        constexpr auto ClearAll() noexcept -> EnumFlags& {
            bits_ = 0;
            return *this;
        }

        [[nodiscard]] constexpr auto IsEmpty() const noexcept -> bool { return bits_ == 0; }
        [[nodiscard]] constexpr auto GetRaw() const noexcept -> UnderlyingType { return bits_; }
        [[nodiscard]] constexpr auto PopCount() const noexcept -> int { return std::popcount(bits_); }

        constexpr auto operator|=(EnumFlags other) noexcept -> EnumFlags& {
            bits_ |= other.bits_;
            return *this;
        }

        constexpr auto operator&=(EnumFlags other) noexcept -> EnumFlags& {
            bits_ &= other.bits_;
            return *this;
        }

        constexpr auto operator^=(EnumFlags other) noexcept -> EnumFlags& {
            bits_ ^= other.bits_;
            return *this;
        }

        [[nodiscard]] friend constexpr auto operator|(EnumFlags a, EnumFlags b) noexcept -> EnumFlags {
            return EnumFlags{static_cast<UnderlyingType>(a.bits_ | b.bits_)};
        }

        [[nodiscard]] friend constexpr auto operator&(EnumFlags a, EnumFlags b) noexcept -> EnumFlags {
            return EnumFlags{static_cast<UnderlyingType>(a.bits_ & b.bits_)};
        }

        [[nodiscard]] friend constexpr auto operator^(EnumFlags a, EnumFlags b) noexcept -> EnumFlags {
            return EnumFlags{static_cast<UnderlyingType>(a.bits_ ^ b.bits_)};
        }

        [[nodiscard]] friend constexpr auto operator~(EnumFlags a) noexcept -> EnumFlags {
            return EnumFlags{static_cast<UnderlyingType>(~a.bits_)};
        }

        [[nodiscard]] friend constexpr auto operator==(EnumFlags a, EnumFlags b) noexcept -> bool {
            return a.bits_ == b.bits_;
        }

        [[nodiscard]] friend constexpr auto operator!=(EnumFlags a, EnumFlags b) noexcept -> bool {
            return a.bits_ != b.bits_;
        }

        // Enable: MyFlags::A | MyFlags::B
        [[nodiscard]] friend constexpr auto operator|(E a, E b) noexcept -> EnumFlags {
            return EnumFlags{a} | EnumFlags{b};
        }

        /// @brief Iterator for range-for over set bits.
        class Iterator {
           public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = E;
            using difference_type = std::ptrdiff_t;
            using pointer = const E*;
            using reference = E;

            constexpr Iterator() noexcept = default;
            constexpr explicit Iterator(UnderlyingType bits) noexcept : remaining_(bits) { Advance(); }

            [[nodiscard]] constexpr auto operator*() const noexcept -> E { return static_cast<E>(current_); }

            constexpr auto operator++() noexcept -> Iterator& {
                remaining_ &= ~current_;
                Advance();
                return *this;
            }

            constexpr auto operator++(int) noexcept -> Iterator {
                auto tmp = *this;
                ++(*this);
                return tmp;
            }

            [[nodiscard]] friend constexpr auto operator==(const Iterator& a, const Iterator& b) noexcept -> bool {
                return a.remaining_ == b.remaining_;
            }

            [[nodiscard]] friend constexpr auto operator!=(const Iterator& a, const Iterator& b) noexcept -> bool {
                return a.remaining_ != b.remaining_;
            }

           private:
            constexpr void Advance() noexcept {
                if (remaining_ != 0) {
                    current_ = remaining_ & (~remaining_ + 1);  // Isolate lowest set bit
                } else {
                    current_ = 0;
                }
            }

            UnderlyingType remaining_ = 0;
            UnderlyingType current_ = 0;
        };

        [[nodiscard]] constexpr auto begin() const noexcept -> Iterator { return Iterator{bits_}; }
        [[nodiscard]] constexpr auto end() const noexcept -> Iterator { return Iterator{0}; }

       private:
        UnderlyingType bits_ = 0;
    };

/// @brief Macro to enable bitwise operators for a scoped enum.
/// @example MIKI_ENABLE_ENUM_FLAGS(MyFlags);
#define MIKI_ENABLE_ENUM_FLAGS(EnumType)                                                    \
    [[nodiscard]] inline constexpr auto operator|(EnumType a, EnumType b) noexcept          \
        -> ::miki::core::EnumFlags<EnumType> {                                              \
        return ::miki::core::EnumFlags<EnumType>{a} | ::miki::core::EnumFlags<EnumType>{b}; \
    }

}  // namespace miki::core
