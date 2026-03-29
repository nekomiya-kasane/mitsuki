/** @file DeferredDestructor.h
 *  @brief Per-frame deferred GPU resource destruction.
 *
 *  Resources deleted mid-frame may still be referenced by in-flight GPU work.
 *  DeferredDestructor queues destruction commands and executes them only after
 *  the frame that referenced them has completed on the GPU.
 *
 *  Architecture:
 *    - Ring of N bins (one per frame-in-flight slot).
 *    - DestroyDeferred<Handle>(handle) enqueues into the CURRENT frame's bin.
 *    - DrainBin(frameIndex) is called by FrameManager::BeginFrame after the
 *      CPU wait confirms the GPU is done with that slot.
 *    - DrainAll() is called during shutdown / surface detach.
 *
 *  Type-erased: stores {HandleType, uint64_t rawValue} pairs to avoid
 *  templating the internal storage. The DeviceHandle dispatch executes
 *  the correct Destroy* call based on HandleType.
 *
 *  See: specs/01-window-manager.md §6.1, specs/03-sync.md §4.3
 *  Namespace: miki::frame
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"

namespace miki::frame {

    class DeferredDestructor {
       public:
        static constexpr uint32_t kMaxBins = 3;

        ~DeferredDestructor();

        DeferredDestructor(const DeferredDestructor&) = delete;
        auto operator=(const DeferredDestructor&) -> DeferredDestructor& = delete;
        DeferredDestructor(DeferredDestructor&&) noexcept;
        auto operator=(DeferredDestructor&&) noexcept -> DeferredDestructor&;

        /// @brief Create a DeferredDestructor bound to a device.
        /// @param iDevice     Device handle for executing destroy calls.
        /// @param iBinCount   Number of bins (== framesInFlight). Clamped to [1, kMaxBins].
        [[nodiscard]] static auto Create(rhi::DeviceHandle iDevice, uint32_t iBinCount) -> DeferredDestructor;

        // ── Deferred destroy ────────────────────────────────────────

        /// @brief Enqueue a buffer for deferred destruction.
        auto Destroy(rhi::BufferHandle iHandle) -> void;
        /// @brief Enqueue a texture for deferred destruction.
        auto Destroy(rhi::TextureHandle iHandle) -> void;
        /// @brief Enqueue a texture view for deferred destruction.
        auto Destroy(rhi::TextureViewHandle iHandle) -> void;
        /// @brief Enqueue a sampler for deferred destruction.
        auto Destroy(rhi::SamplerHandle iHandle) -> void;
        /// @brief Enqueue a pipeline for deferred destruction.
        auto Destroy(rhi::PipelineHandle iHandle) -> void;
        /// @brief Enqueue a descriptor set for deferred destruction.
        auto Destroy(rhi::DescriptorSetHandle iHandle) -> void;
        /// @brief Enqueue a shader module for deferred destruction.
        auto Destroy(rhi::ShaderModuleHandle iHandle) -> void;

        // ── Bin management ──────────────────────────────────────────

        /// @brief Set the current bin index (called by FrameManager at BeginFrame).
        auto SetCurrentBin(uint32_t iBinIndex) -> void;

        /// @brief Drain a specific bin — destroy all resources queued in it.
        /// Called by FrameManager::BeginFrame after GPU completion is confirmed.
        auto DrainBin(uint32_t iBinIndex) -> void;

        /// @brief Drain ALL bins — used during shutdown.
        auto DrainAll() -> void;

        /// @brief Get the number of pending resources across all bins.
        [[nodiscard]] auto PendingCount() const noexcept -> uint32_t;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit DeferredDestructor(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::frame
