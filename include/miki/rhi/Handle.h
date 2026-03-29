/** @file Handle.h
 *  @brief 64-bit typed opaque handles and HandlePool for GPU resource management.
 *
 *  Handle layout: [generation:16 | index:32 | type:8 | backend:8]
 *  - Generation prevents use-after-free (ABA-safe).
 *  - Index is the slot in the HandlePool.
 *  - Type tag identifies the resource kind (compile-time via Tag).
 *  - Backend tag identifies the originating backend.
 *
 *  HandlePool: O(1) alloc/free/lookup with mutex-protected free-list.
 *  Lookup is lock-free (read-only generation check on stable slots).
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <utility>

namespace miki::rhi {

    // =========================================================================
    // Handle — 64-bit typed opaque handle
    // =========================================================================

    template <typename Tag>
    struct Handle {
        static constexpr uint64_t kGenerationBits = 16;
        static constexpr uint64_t kIndexBits = 32;
        static constexpr uint64_t kTypeBits = 8;
        static constexpr uint64_t kBackendBits = 8;

        static constexpr uint64_t kGenerationShift = 48;
        static constexpr uint64_t kIndexShift = 16;
        static constexpr uint64_t kTypeShift = 8;
        static constexpr uint64_t kBackendShift = 0;

        static constexpr uint64_t kIndexMask = (uint64_t{1} << kIndexBits) - 1;
        static constexpr uint64_t kTypeMask = (uint64_t{1} << kTypeBits) - 1;
        static constexpr uint64_t kBackendMask = (uint64_t{1} << kBackendBits) - 1;

        uint64_t value = 0;

        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return value != 0; }
        constexpr auto operator<=>(const Handle&) const = default;

        [[nodiscard]] constexpr auto GetGeneration() const noexcept -> uint16_t {
            return static_cast<uint16_t>(value >> kGenerationShift);
        }
        [[nodiscard]] constexpr auto GetIndex() const noexcept -> uint32_t {
            return static_cast<uint32_t>((value >> kIndexShift) & kIndexMask);
        }
        [[nodiscard]] constexpr auto GetTypeTag() const noexcept -> uint8_t {
            return static_cast<uint8_t>((value >> kTypeShift) & kTypeMask);
        }
        [[nodiscard]] constexpr auto GetBackendTag() const noexcept -> uint8_t {
            return static_cast<uint8_t>((value >> kBackendShift) & kBackendMask);
        }

        static constexpr auto Pack(uint16_t gen, uint32_t idx, uint8_t type, uint8_t backend) noexcept -> Handle {
            return Handle{
                (static_cast<uint64_t>(gen) << kGenerationShift) | (static_cast<uint64_t>(idx) << kIndexShift)
                | (static_cast<uint64_t>(type) << kTypeShift) | (static_cast<uint64_t>(backend) << kBackendShift)
            };
        }
    };

    // =========================================================================
    // Handle type aliases (spec §3.1)
    // =========================================================================

    using BufferHandle = Handle<struct BufferTag>;
    using TextureHandle = Handle<struct TextureTag>;
    using TextureViewHandle = Handle<struct TextureViewTag>;
    using SamplerHandle = Handle<struct SamplerTag>;
    using PipelineHandle = Handle<struct PipelineTag>;
    using PipelineLayoutHandle = Handle<struct PipelineLayoutTag>;
    using PipelineCacheHandle = Handle<struct PipelineCacheTag>;
    using PipelineLibraryPartHandle = Handle<struct PipelineLibraryPartTag>;
    using ShaderModuleHandle = Handle<struct ShaderModuleTag>;
    using FenceHandle = Handle<struct FenceTag>;
    using SemaphoreHandle = Handle<struct SemaphoreTag>;
    using QueryPoolHandle = Handle<struct QueryPoolTag>;
    using AccelStructHandle = Handle<struct AccelStructTag>;
    using SwapchainHandle = Handle<struct SwapchainTag>;
    using DeviceMemoryHandle = Handle<struct DeviceMemoryTag>;
    using DescriptorLayoutHandle = Handle<struct DescriptorLayoutTag>;
    using DescriptorSetHandle = Handle<struct DescriptorSetTag>;
    using CommandBufferHandle = Handle<struct CommandBufferTag>;

    // =========================================================================
    // Pool capacities — sized for 10B triangle CAD/CAE workloads
    // Shared by all backends (Vulkan, D3D12, WebGPU, OpenGL)
    // =========================================================================

    inline constexpr size_t kMaxBuffers = 65536;
    inline constexpr size_t kMaxTextures = 16384;
    inline constexpr size_t kMaxTextureViews = 32768;
    inline constexpr size_t kMaxSamplers = 2048;
    inline constexpr size_t kMaxShaderModules = 4096;
    inline constexpr size_t kMaxFences = 256;
    inline constexpr size_t kMaxSemaphores = 512;
    inline constexpr size_t kMaxPipelines = 8192;
    inline constexpr size_t kMaxPipelineLayouts = 4096;
    inline constexpr size_t kMaxDescriptorLayouts = 4096;
    inline constexpr size_t kMaxDescriptorSets = 32768;
    inline constexpr size_t kMaxPipelineCaches = 16;
    inline constexpr size_t kMaxPipelineLibraryParts = 4096;
    inline constexpr size_t kMaxQueryPools = 128;
    inline constexpr size_t kMaxAccelStructs = 8192;
    inline constexpr size_t kMaxSwapchains = 16;
    inline constexpr size_t kMaxCommandBuffers = 512;
    inline constexpr size_t kMaxDeviceMemory = 1024;

    // =========================================================================
    // HandlePool — fixed-capacity slot array with free-list
    // =========================================================================

    /** @brief Thread-safe handle pool with O(1) alloc/free/lookup.
     *
     *  Alloc/Free are mutex-protected (not hot path — O(100)/frame).
     *  Lookup is lock-free (generation check on stable slot, read-only).
     *
     *  @tparam T        The payload type stored per slot.
     *  @tparam Tag      The handle tag type (for type-safe handles).
     *  @tparam Capacity Maximum number of simultaneous live handles.
     */
    template <typename T, typename Tag, size_t Capacity>
    class HandlePool {
        static_assert(Capacity <= std::numeric_limits<uint32_t>::max(), "Capacity exceeds uint32_t index range");

       public:
        using HandleType = Handle<Tag>;

        HandlePool() {
            for (size_t i = 0; i < Capacity; ++i) {
                slots_[i].nextFree = static_cast<uint32_t>(i + 1);
                slots_[i].generation = 1;  // Start at 1: Pack(1,0,0,0)!=0, so IsValid() is never false for slot 0
                slots_[i].alive = false;
            }
            slots_[Capacity - 1].nextFree = kInvalidIndex;
            freeListHead_ = 0;
            freeCount_ = static_cast<uint32_t>(Capacity);
        }

        /** @brief Allocate a slot, return handle + pointer to payload.
         *  @param typeTag  Resource type tag (for handle encoding).
         *  @param backend  Backend tag (for handle encoding).
         */
        [[nodiscard]] auto Allocate(uint8_t typeTag = 0, uint8_t backend = 0) -> std::pair<HandleType, T*> {
            std::lock_guard lock(mutex_);
            if (freeListHead_ == kInvalidIndex) {
                return {{}, nullptr};
            }

            uint32_t idx = freeListHead_;
            auto& slot = slots_[idx];
            freeListHead_ = slot.nextFree;
            --freeCount_;

            slot.alive = true;
            auto* obj = ::new (&slot.storage) T{};  // Placement-new: construct in-place
            auto handle = HandleType::Pack(slot.generation, idx, typeTag, backend);
            return {handle, obj};
        }

        /** @brief Immediate free: destroy payload, increment generation, return slot to free list.
         *  Use when deferred destruction is NOT needed (e.g., shutdown path).
         */
        void Free(HandleType handle) {
            if (!handle.IsValid()) {
                return;
            }
            std::lock_guard lock(mutex_);
            uint32_t idx = handle.GetIndex();
            if (idx >= Capacity) {
                return;
            }

            auto& slot = slots_[idx];
            if (!slot.alive || slot.generation != handle.GetGeneration()) {
                return;
            }

            slot.GetObject().~T();
            slot.alive = false;
            slot.dead = false;
            ++slot.generation;
            slot.nextFree = freeListHead_;
            freeListHead_ = idx;
            ++freeCount_;
        }

        /** @brief Phase 1 of deferred destruction: mark slot as dead.
         *  Generation is incremented so Lookup() fails immediately (prevents use-after-free).
         *  Slot payload remains alive for the backend to access during deferred drain.
         *  Returns raw index for Reclaim() — caller stores this in the destruction queue.
         *  @return Slot index, or kInvalidIndex if handle was already invalid/stale.
         */
        auto MarkDead(HandleType handle) -> uint32_t {
            if (!handle.IsValid()) {
                return kInvalidIndex;
            }
            std::lock_guard lock(mutex_);
            uint32_t idx = handle.GetIndex();
            if (idx >= Capacity) {
                return kInvalidIndex;
            }

            auto& slot = slots_[idx];
            if (!slot.alive || slot.generation != handle.GetGeneration()) {
                return kInvalidIndex;
            }

            slot.alive = false;  // Lookup() will fail
            slot.dead = true;    // Payload still constructed, awaiting Reclaim
            ++slot.generation;   // Invalidate all existing handles
            return idx;
        }

        /** @brief Phase 2 of deferred destruction: destroy payload and return slot to free list.
         *  Called by DeferredDestructor::Drain() after GPU has finished referencing the resource.
         *  @param slotIndex  Raw index returned by MarkDead().
         */
        void Reclaim(uint32_t slotIndex) {
            if (slotIndex >= Capacity) {
                return;
            }
            std::lock_guard lock(mutex_);
            auto& slot = slots_[slotIndex];
            if (!slot.dead) {
                return;  // Already reclaimed or was never marked dead
            }

            slot.GetObject().~T();
            slot.dead = false;
            slot.nextFree = freeListHead_;
            freeListHead_ = slotIndex;
            ++freeCount_;
        }

        /** @brief Access payload of a dead-but-not-yet-reclaimed slot.
         *  Used by DeferredDestructor to retrieve native handles for actual API destruction.
         *  @param slotIndex  Raw index returned by MarkDead().
         *  @return Pointer to payload, or nullptr if slot is not in dead state.
         */
        [[nodiscard]] auto LookupDead(uint32_t slotIndex) -> T* {
            if (slotIndex >= Capacity) {
                return nullptr;
            }
            auto& slot = slots_[slotIndex];
            if (!slot.dead) {
                return nullptr;
            }
            return &slot.GetObject();
        }

        [[nodiscard]] auto LookupDead(uint32_t slotIndex) const -> const T* {
            if (slotIndex >= Capacity) {
                return nullptr;
            }
            const auto& slot = slots_[slotIndex];
            if (!slot.dead) {
                return nullptr;
            }
            return &slot.GetObject();
        }

        // NOTE: Lookup is lock-free (read-only generation check on stable slots).
        // On weakly-ordered architectures (ARM), the writer's mutex release does NOT
        // guarantee visibility to a concurrent lock-free reader. This is acceptable
        // for now because Lookup is only called after a synchronization point
        // (e.g., fence wait, frame boundary). If concurrent Alloc+Lookup becomes
        // a requirement, alive/generation must become std::atomic with acquire/release.

        /** @brief Lock-free const lookup. Returns nullptr if handle is stale or invalid. */
        [[nodiscard]] auto Lookup(HandleType handle) const -> const T* {
            if (!handle.IsValid()) {
                return nullptr;
            }
            uint32_t idx = handle.GetIndex();
            if (idx >= Capacity) {
                return nullptr;
            }
            const auto& slot = slots_[idx];
            if (!slot.alive || slot.generation != handle.GetGeneration()) {
                return nullptr;
            }
            return &slot.GetObject();
        }

        /** @brief Lock-free mutable lookup. */
        [[nodiscard]] auto Lookup(HandleType handle) -> T* {
            if (!handle.IsValid()) {
                return nullptr;
            }
            uint32_t idx = handle.GetIndex();
            if (idx >= Capacity) {
                return nullptr;
            }
            auto& slot = slots_[idx];
            if (!slot.alive || slot.generation != handle.GetGeneration()) {
                return nullptr;
            }
            return &slot.GetObject();
        }

        static constexpr auto InvalidIndex() noexcept -> uint32_t { return kInvalidIndex; }

        [[nodiscard]] auto FreeCount() const noexcept -> uint32_t { return freeCount_; }
        [[nodiscard]] auto LiveCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(Capacity) - freeCount_;
        }
        static constexpr auto GetCapacity() noexcept -> size_t { return Capacity; }

       private:
        static constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

        struct Slot {
            alignas(T) std::byte storage[sizeof(T)];  // Uninitialized storage
            uint32_t nextFree = kInvalidIndex;
            uint16_t generation = 0;
            bool alive = false;
            bool dead = false;  // MarkDead'd but not yet Reclaim'd (payload still constructed)

            auto GetObject() noexcept -> T& { return *std::launder(reinterpret_cast<T*>(storage)); }
            auto GetObject() const noexcept -> const T& { return *std::launder(reinterpret_cast<const T*>(storage)); }
        };

        mutable std::mutex mutex_;
        uint32_t freeListHead_ = 0;
        uint32_t freeCount_ = 0;
        std::array<Slot, Capacity> slots_;
    };

}  // namespace miki::rhi
