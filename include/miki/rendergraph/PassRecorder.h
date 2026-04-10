/** @file PassRecorder.h
 *  @brief Phase 2 of render graph execution: command buffer recording.
 *
 *  Records compiled passes into command buffers via two paths:
 *    - Single-threaded: one primary cmd buf per batch, barriers + pass lambdas inline
 *    - Parallel (E-5/E-6): barriers in primary, pass lambdas in per-thread secondaries
 *
 *  Handles merged render pass group bracketing (CmdBeginRendering/CmdEndRendering),
 *  rendering attachment construction, and debug label emission.
 *
 *  Output: a vector of BatchRecording consumed by BatchSubmitter (Phase 3).
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "miki/core/LinearAllocator.h"
#include "miki/core/Result.h"
#include "miki/frame/CommandPoolAllocator.h"
#include "miki/frame/FrameContext.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rendergraph/RenderPassContext.h"
#include "miki/rendergraph/TransientResourceAllocator.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"

namespace miki::resource {
    class ReadbackRing;
}

namespace miki::rg {

    // =========================================================================
    // BatchRecording — output of Phase 2, input to Phase 3
    // =========================================================================

    struct BatchRecording {
        RGQueueType queue = RGQueueType::Graphics;
        std::vector<rhi::CommandBufferHandle> commandBuffers;
    };

    // =========================================================================
    // Recording statistics
    // =========================================================================

    struct RecordingStats {
        uint32_t barriersEmitted = 0;
        uint32_t aliasingBarriersEmitted = 0;
        uint32_t passesRecorded = 0;
        uint32_t secondaryCmdBufsUsed = 0;
    };

    // =========================================================================
    // PassRecorder configuration
    // =========================================================================

    struct PassRecorderConfig {
        uint32_t maxRecordingThreads = 1;
        bool enableParallelRecording = false;
        bool enableDebugLabels = true;
    };

    // =========================================================================
    // PassRecorder
    // =========================================================================

    class PassRecorder {
       public:
        explicit PassRecorder(const PassRecorderConfig& config = {});
        ~PassRecorder();

        PassRecorder(const PassRecorder&) = delete;
        auto operator=(const PassRecorder&) -> PassRecorder& = delete;
        PassRecorder(PassRecorder&&) noexcept;
        auto operator=(PassRecorder&&) noexcept -> PassRecorder&;

        /// @brief Record all passes in the compiled graph into command buffers.
        /// Chooses single-threaded or parallel path based on config.
        [[nodiscard]] auto Record(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
            rhi::DeviceHandle device, frame::CommandPoolAllocator& poolAllocator,
            const PhysicalResourceTable& physicalTable, core::LinearAllocator& frameAllocator,
            resource::ReadbackRing* readbackRing
        ) -> core::Result<void>;

        /// @brief Get the recorded batches (consumed by BatchSubmitter).
        [[nodiscard]] auto GetBatchRecordings() const noexcept -> std::span<const BatchRecording> {
            return batchRecordings_;
        }

        /// @brief Get recording statistics.
        [[nodiscard]] auto GetStats() const noexcept -> const RecordingStats& { return stats_; }

       private:
        // Per-frame invariant context, set once at Record() entry.
        // Eliminates parameter pass-through across all private methods.
        struct RecordingSession {
            const CompiledRenderGraph* graph = nullptr;
            RenderGraphBuilder* builder = nullptr;
            const frame::FrameContext* frame = nullptr;
            frame::CommandPoolAllocator* poolAllocator = nullptr;
            const PhysicalResourceTable* physicalTable = nullptr;
            core::LinearAllocator* frameAllocator = nullptr;
            resource::ReadbackRing* readbackRing = nullptr;
        };

        // Merged group membership info for a compiled pass
        struct MergedGroupMembership {
            uint32_t groupIndex = UINT32_MAX;
            uint32_t subpassPosition = 0;
            bool isFirst = false;
            bool isLast = false;
        };

        // Single-threaded recording path
        auto RecordSingleThreaded() -> core::Result<void>;

        // Parallel recording path (E-5/E-6)
        auto RecordParallel() -> core::Result<void>;

        // Record a single pass into a command list
        void RecordSinglePass(
            rhi::CommandListHandle& cmdList, uint32_t compiledPassIndex, const CompiledPassInfo& compiledPass,
            RGPassNode& passNode, bool emitBarriers, const MergedGroupMembership* mergedMembership = nullptr
        );

        // Build rendering attachments for a single pass
        static void BuildRenderingAttachments(
            const RGPassNode& passNode, std::span<const rhi::TextureViewHandle> physicalTextureViews,
            std::vector<rhi::RenderingAttachment>& outColor, rhi::RenderingAttachment& outDepth, bool& hasDepth
        );

        // Build combined attachments for a merged group
        void BuildMergedGroupAttachments(
            const MergedRenderPassGroup& group, std::vector<rhi::RenderingAttachment>& outColor,
            rhi::RenderingAttachment& outDepth, bool& hasDepth
        );

        // Build lookup table: compiledPassIndex -> merged group membership
        static auto BuildMergedGroupLookup(const CompiledRenderGraph& graph) -> std::vector<MergedGroupMembership>;

        // --- Config & State ---
        PassRecorderConfig config_;
        RecordingStats stats_;
        RecordingSession session_;
        std::vector<BatchRecording> batchRecordings_;
    };

}  // namespace miki::rg
