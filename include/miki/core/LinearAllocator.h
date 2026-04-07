/** @file LinearAllocator.h
 *  @brief Bump/linear allocator for arena-style memory management.
 *
 *  LinearAllocator owns a contiguous memory block and provides O(1)
 *  allocation by advancing a pointer. Individual deallocations are not
 *  supported — the entire arena is freed at once via Reset().
 *
 *  Primary use case: per-frame / per-graph temporary storage where many
 *  small, short-lived allocations are made and then discarded together.
 *
 *  Namespace: miki::core
 */
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace miki::core {

    /// @brief Simple bump allocator with a single contiguous backing buffer.
    ///
    /// Thread-safety: None. Intended for single-threaded graph construction.
    class LinearAllocator {
    public:
        explicit LinearAllocator(size_t capacityBytes)
            : buffer_(std::make_unique<std::byte[]>(capacityBytes)), capacity_(capacityBytes) {}

        ~LinearAllocator() = default;

        LinearAllocator(const LinearAllocator&) = delete;
        auto operator=(const LinearAllocator&) -> LinearAllocator& = delete;

        LinearAllocator(LinearAllocator&& other) noexcept
            : buffer_(std::move(other.buffer_)), capacity_(other.capacity_), offset_(other.offset_) {
            other.capacity_ = 0;
            other.offset_ = 0;
        }

        auto operator=(LinearAllocator&& other) noexcept -> LinearAllocator& {
            if (this != &other) {
                buffer_ = std::move(other.buffer_);
                capacity_ = other.capacity_;
                offset_ = other.offset_;
                other.capacity_ = 0;
                other.offset_ = 0;
            }
            return *this;
        }

        /// @brief Allocate raw bytes with the given alignment.
        /// Returns nullptr if the arena is exhausted.
        [[nodiscard]] auto Allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) noexcept -> void* {
            size_t aligned = AlignUp(offset_, alignment);
            if (aligned + bytes > capacity_) return nullptr;
            void* ptr = buffer_.get() + aligned;
            offset_ = aligned + bytes;
            return ptr;
        }

        /// @brief Allocate storage for N objects of type T (uninitialized).
        /// Returns a span over the allocated region, or empty span on failure.
        template <typename T>
        [[nodiscard]] auto AllocateArray(size_t count) noexcept -> std::span<T> {
            if (count == 0) return {};
            void* ptr = Allocate(count * sizeof(T), alignof(T));
            if (!ptr) return {};
            return {static_cast<T*>(ptr), count};
        }

        /// @brief Construct a single object of type T in arena memory.
        template <typename T, typename... Args>
        [[nodiscard]] auto Emplace(Args&&... args) -> T* {
            void* ptr = Allocate(sizeof(T), alignof(T));
            if (!ptr) return nullptr;
            return ::new (ptr) T(std::forward<Args>(args)...);
        }

        /// @brief Copy a range of elements into the arena and return a span.
        template <typename T>
        [[nodiscard]] auto CopyToArena(std::span<const T> src) noexcept -> std::span<T>
            requires std::is_trivially_copyable_v<T>
        {
            auto dst = AllocateArray<T>(src.size());
            if (!dst.empty()) {
                std::memcpy(dst.data(), src.data(), src.size_bytes());
            }
            return dst;
        }

        /// @brief Reset the allocator, reclaiming all memory. Does NOT call destructors.
        void Reset() noexcept { offset_ = 0; }

        [[nodiscard]] auto GetUsedBytes() const noexcept -> size_t { return offset_; }
        [[nodiscard]] auto GetCapacity() const noexcept -> size_t { return capacity_; }
        [[nodiscard]] auto GetRemainingBytes() const noexcept -> size_t { return capacity_ - offset_; }

    private:
        [[nodiscard]] static constexpr auto AlignUp(size_t value, size_t alignment) noexcept -> size_t {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        std::unique_ptr<std::byte[]> buffer_;
        size_t capacity_ = 0;
        size_t offset_ = 0;
    };

}  // namespace miki::core
