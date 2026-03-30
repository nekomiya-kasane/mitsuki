/** @file CommandListArena.h
 *  @brief Fixed-capacity arena with bitmask-based index allocation (§19.9).
 *
 *  CommandListArena manages up to kMaxSlots (128) typed slots using a 2×uint64_t
 *  bitmask for O(1) acquire/release via std::countr_zero. Slots beyond 128 fall
 *  back to dynamic growth with a diagnostic warning.
 *
 *  Thread safety: NONE — designed for single-threaded-per-queue usage.
 *
 *  Namespace: miki::frame
 */
#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <vector>

#include "miki/debug/StructuredLogger.h"

namespace miki::frame {

    /** @brief Fixed-capacity slot arena with bitmask-based O(1) acquire/release.
     *  @tparam T Stored element type. Must be default-constructible and movable.
     */
    template <typename T>
    class CommandListArena {
       public:
        static constexpr uint32_t kMaxSlots = 128;
        static constexpr uint32_t kBitsPerWord = 64;
        static constexpr uint32_t kNumWords = kMaxSlots / kBitsPerWord;  // 2

        struct AcquireResult {
            T* ptr = nullptr;
            uint32_t index = UINT32_MAX;
        };

        CommandListArena() { freeMask_.fill(UINT64_MAX); }

        /** @brief Pre-allocate storage for the expected number of slots. */
        void Reserve(uint32_t hint) {
            if (hint > kMaxSlots) {
                hint = kMaxSlots;
            }
            if (slots_.size() < hint) {
                slots_.resize(hint);
                initialized_.resize(hint, false);
            }
        }

        /** @brief Acquire a free slot. Returns {ptr, index} or {nullptr, UINT32_MAX} if full.
         *  Fast path: scans 2×uint64_t bitmask with countr_zero (~2 cycles).
         *  Overflow path: grows into dynamic overflow_ vector with diagnostic warning.
         */
        [[nodiscard]] auto Acquire() -> AcquireResult {
            for (uint32_t w = 0; w < kNumWords; ++w) {
                if (freeMask_[w] != 0) {
                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(freeMask_[w]));
                    uint32_t index = w * kBitsPerWord + bit;
                    freeMask_[w] &= ~(uint64_t{1} << bit);
                    acquiredCount_++;

                    if (index >= slots_.size()) {
                        slots_.resize(index + 1);
                        initialized_.resize(index + 1, false);
                    }
                    return {&slots_[index], index};
                }
            }

            // Overflow: all 128 slots consumed — fallback to dynamic growth
            MIKI_LOG_WARN(
                ::miki::debug::LogCategory::Rhi,
                "CommandListArena overflow: {} slots exhausted, falling back to dynamic allocation", kMaxSlots
            );
            uint32_t index = kMaxSlots + static_cast<uint32_t>(overflow_.size());
            overflow_.emplace_back();
            acquiredCount_++;
            return {&overflow_.back(), index};
        }

        /** @brief Release a previously acquired slot by index. */
        void Release(uint32_t index) {
            assert(acquiredCount_ > 0 && "Release called with no acquired slots");
            if (index < kMaxSlots) {
                uint32_t w = index / kBitsPerWord;
                uint32_t bit = index % kBitsPerWord;
                assert((freeMask_[w] & (uint64_t{1} << bit)) == 0 && "Double-release detected");
                freeMask_[w] |= (uint64_t{1} << bit);
            }
            // Overflow slots: no bitmask tracking, just decrement count
            acquiredCount_--;
        }

        /** @brief Reset all slots to free. Does NOT destroy stored objects. */
        void ResetAll() {
            freeMask_.fill(UINT64_MAX);
            overflow_.clear();
            acquiredCount_ = 0;
        }

        /** @brief Look up a slot by index. Returns nullptr if out of range. */
        [[nodiscard]] auto Lookup(uint32_t index) -> T* {
            if (index < kMaxSlots) {
                return index < slots_.size() ? &slots_[index] : nullptr;
            }
            uint32_t oi = index - kMaxSlots;
            return oi < overflow_.size() ? &overflow_[oi] : nullptr;
        }

        [[nodiscard]] auto Lookup(uint32_t index) const -> const T* {
            if (index < kMaxSlots) {
                return index < slots_.size() ? &slots_[index] : nullptr;
            }
            uint32_t oi = index - kMaxSlots;
            return oi < overflow_.size() ? &overflow_[oi] : nullptr;
        }

        [[nodiscard]] auto GetAcquiredCount() const noexcept -> uint32_t { return acquiredCount_; }
        [[nodiscard]] auto GetCapacity() const noexcept -> uint32_t {
            return kMaxSlots + static_cast<uint32_t>(overflow_.capacity());
        }
        [[nodiscard]] auto GetAllocatedCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(slots_.size()) + static_cast<uint32_t>(overflow_.size());
        }
        [[nodiscard]] auto HasOverflow() const noexcept -> bool { return !overflow_.empty(); }

       private:
        std::array<uint64_t, kNumWords> freeMask_{};
        std::vector<T> slots_;
        std::vector<bool> initialized_;
        std::vector<T> overflow_;
        uint32_t acquiredCount_ = 0;
    };

}  // namespace miki::frame
