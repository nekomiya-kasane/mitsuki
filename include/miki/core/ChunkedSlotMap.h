// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace miki::core {

    /// @brief Generational handle for ChunkedSlotMap lookups.
    struct SlotHandle {
        uint32_t id = 0;
        uint16_t generation = 0;

        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return id != 0; }
        constexpr auto operator==(const SlotHandle&) const noexcept -> bool = default;
    };

    /// @brief Cache-friendly chunked slot map with generational handles.
    /// @tparam T         Value type stored in each slot.
    /// @tparam ChunkSize Number of slots per contiguous chunk (default 16).
    ///
    /// Properties:
    /// - O(1) lookup by (slot index, generation)
    /// - O(1) amortized insert (free-list + dynamic chunk allocation)
    /// - No hardcoded capacity limit — new chunks allocated on demand
    /// - Generation counter prevents ABA/stale-handle bugs
    /// - Slot index 0 is reserved (never allocated) so that id==0 means "invalid"
    template <typename T, uint32_t ChunkSize = 16>
    class ChunkedSlotMap {
       public:
        ChunkedSlotMap() {
            // Allocate first chunk; slot 0 is reserved (sentinel).
            AllocateChunk();
            auto& sentinel = GetSlot(0);
            sentinel.occupied = true;  // permanently occupied, never returned
            sentinel.generation = 0;
            size_ = 0;  // sentinel doesn't count
        }

        /// @brief Allocate a slot and construct T in-place.
        /// @return Handle with unique id + generation.
        template <typename... Args>
        [[nodiscard]] auto Emplace(Args&&... args) -> SlotHandle {
            uint32_t idx = AllocateSlot();
            auto& slot = GetSlot(idx);
            slot.occupied = true;
            std::construct_at(&slot.Storage(), std::forward<Args>(args)...);
            ++size_;
            return SlotHandle{idx, slot.generation};
        }

        /// @brief Free the slot identified by handle. Increments generation.
        /// @return true if the slot was valid and freed; false if stale/invalid.
        auto Free(SlotHandle iHandle) -> bool {
            if (!iHandle.IsValid()) {
                return false;
            }
            auto* slot = TryGetSlot(iHandle);
            if (!slot) {
                return false;
            }

            std::destroy_at(&slot->Storage());
            slot->occupied = false;
            // Increment generation; if it wraps to 0, bump to 1 so generation 0 is never live.
            ++slot->generation;
            if (slot->generation == 0) {
                slot->generation = 1;
            }
            freeList_.push_back(iHandle.id);
            --size_;
            return true;
        }

        /// @brief Get a pointer to the stored value, or nullptr if handle is stale/invalid.
        [[nodiscard]] auto Get(SlotHandle iHandle) -> T* {
            auto* slot = TryGetSlot(iHandle);
            return slot ? &slot->Storage() : nullptr;
        }

        [[nodiscard]] auto Get(SlotHandle iHandle) const -> const T* {
            auto* slot = TryGetSlot(iHandle);
            return slot ? &slot->Storage() : nullptr;
        }

        /// @brief Check if a handle refers to a live slot.
        [[nodiscard]] auto IsAlive(SlotHandle iHandle) const -> bool { return TryGetSlot(iHandle) != nullptr; }

        /// @brief Number of occupied slots (excluding sentinel).
        [[nodiscard]] auto Size() const noexcept -> uint32_t { return size_; }

        /// @brief Total allocated slot capacity (excluding sentinel).
        [[nodiscard]] auto Capacity() const noexcept -> uint32_t {
            return static_cast<uint32_t>(chunks_.size()) * ChunkSize - 1;
        }

        /// @brief Iterate over all live (occupied) slots. Callable signature: void(SlotHandle, T&).
        template <typename Fn>
        auto ForEach(Fn& fn) -> void {
            uint32_t total = static_cast<uint32_t>(chunks_.size()) * ChunkSize;
            for (uint32_t i = 1; i < total; ++i) {  // skip sentinel at 0
                auto& slot = GetSlot(i);
                if (slot.occupied) {
                    fn(SlotHandle{i, slot.generation}, slot.Storage());
                }
            }
        }

        template <typename Fn>
        auto ForEach(Fn& fn) const -> void {
            uint32_t total = static_cast<uint32_t>(chunks_.size()) * ChunkSize;
            for (uint32_t i = 1; i < total; ++i) {
                auto& slot = GetSlot(i);
                if (slot.occupied) {
                    fn(SlotHandle{i, slot.generation}, slot.Storage());
                }
            }
        }

       private:
        struct Slot {
            alignas(T) std::array<unsigned char, sizeof(T)> storage{};
            uint16_t generation = 1;  // start at 1 so first alloc gets gen=1
            bool occupied = false;

            auto Storage() -> T& { return *std::launder(reinterpret_cast<T*>(storage.data())); }
            auto Storage() const -> const T& { return *std::launder(reinterpret_cast<const T*>(storage.data())); }
        };

        struct Chunk {
            std::array<Slot, ChunkSize> slots{};
        };

        std::vector<std::unique_ptr<Chunk>> chunks_;
        std::vector<uint32_t> freeList_;
        uint32_t nextSlot_ = 1;  // next uninitialized slot index (0 = sentinel, skip it)
        uint32_t size_ = 0;

        auto AllocateChunk() -> void { chunks_.push_back(std::make_unique<Chunk>()); }

        auto AllocateSlot() -> uint32_t {
            if (!freeList_.empty()) {
                uint32_t idx = freeList_.back();
                freeList_.pop_back();
                return idx;
            }
            uint32_t totalCapacity = static_cast<uint32_t>(chunks_.size()) * ChunkSize;
            if (nextSlot_ >= totalCapacity) {
                AllocateChunk();
            }
            return nextSlot_++;
        }

        auto GetSlot(uint32_t idx) -> Slot& {
            uint32_t chunk = idx / ChunkSize;
            uint32_t local = idx % ChunkSize;
            assert(chunk < chunks_.size());
            return chunks_[chunk]->slots[local];
        }

        auto GetSlot(uint32_t idx) const -> const Slot& {
            uint32_t chunk = idx / ChunkSize;
            uint32_t local = idx % ChunkSize;
            assert(chunk < chunks_.size());
            return chunks_[chunk]->slots[local];
        }

        auto TryGetSlot(SlotHandle iHandle) -> Slot* {
            if (!iHandle.IsValid()) {
                return nullptr;
            }
            uint32_t totalCapacity = static_cast<uint32_t>(chunks_.size()) * ChunkSize;
            if (iHandle.id >= totalCapacity) {
                return nullptr;
            }
            auto& slot = GetSlot(iHandle.id);
            if (!slot.occupied || slot.generation != iHandle.generation) {
                return nullptr;
            }
            return &slot;
        }

        auto TryGetSlot(SlotHandle iHandle) const -> const Slot* {
            if (!iHandle.IsValid()) {
                return nullptr;
            }
            uint32_t totalCapacity = static_cast<uint32_t>(chunks_.size()) * ChunkSize;
            if (iHandle.id >= totalCapacity) {
                return nullptr;
            }
            auto& slot = GetSlot(iHandle.id);
            if (!slot.occupied || slot.generation != iHandle.generation) {
                return nullptr;
            }
            return &slot;
        }
    };

}  // namespace miki::core
