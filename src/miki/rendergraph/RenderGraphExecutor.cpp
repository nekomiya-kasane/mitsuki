/** @file RenderGraphExecutor.cpp
 *  @brief Thin orchestrator for render graph execution.
 *
 *  Delegates to:
 *    Phase 1: TransientResourceAllocator
 *    Phase 2: PassRecorder
 *    Phase 3: BatchSubmitter (SubmitBatches free function)
 *
 *  See: specs/04-render-graph.md §6
 */

#include "miki/rendergraph/RenderGraphExecutor.h"

#include "miki/rendergraph/RenderGraphBuilder.h"

namespace miki::rg {

    // =========================================================================
    // Constructor / Destructor / Move
    // =========================================================================

    RenderGraphExecutor::RenderGraphExecutor(const ExecutorConfig& config)
        : allocator_(
              TransientAllocatorConfig{
                  .enableHeapPooling = config.enableHeapPooling,
                  .enableBufferSuballocation = config.enableBufferSuballocation,
                  .heapPoolConfig = config.heapPoolConfig,
              }
          )
        , recorder_(
              PassRecorderConfig{
                  .maxRecordingThreads = config.maxRecordingThreads,
                  .enableParallelRecording = config.enableParallelRecording,
                  .enableDebugLabels = config.enableDebugLabels,
              }
          )
        , frameAllocator_(config.frameAllocatorCapacity) {}

    RenderGraphExecutor::~RenderGraphExecutor() = default;

    RenderGraphExecutor::RenderGraphExecutor(RenderGraphExecutor&&) noexcept = default;
    auto RenderGraphExecutor::operator=(RenderGraphExecutor&&) noexcept -> RenderGraphExecutor& = default;

    // =========================================================================
    // Execute — main entry point
    // =========================================================================

    auto RenderGraphExecutor::Execute(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
        rhi::DeviceHandle device, frame::CommandPoolAllocator& poolAllocator
    ) -> core::Result<void> {
        stats_ = {};
        frameAllocator_.Reset();
        ++frameNumber_;

        // Phase 1: Allocate transient resources
        auto allocResult = allocator_.Allocate(graph, builder, device, frameNumber_);
        if (!allocResult) {
            return allocResult;
        }
        stats_.allocation = allocator_.GetStats();

        // Phase 2: Record command buffers
        auto physicalTable = allocator_.GetPhysicalTable();
        auto recordResult = recorder_.Record(
            graph, builder, frame, device, poolAllocator, physicalTable, frameAllocator_, readbackRing_
        );
        if (!recordResult) {
            allocator_.DestroyTransients(device);
            return recordResult;
        }
        stats_.recording = recorder_.GetStats();

        // Phase 3: Submit batches
        auto submitResult = SubmitBatches(graph, recorder_.GetBatchRecordings(), device, &stats_.submission);
        if (!submitResult) {
            allocator_.DestroyTransients(device);
            return submitResult;
        }

        return {};
    }

    // =========================================================================
    // ExecuteAsync — offload Phase 1 + 2 to worker thread (E-7)
    // =========================================================================

    auto RenderGraphExecutor::ExecuteAsync(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
        rhi::DeviceHandle device, frame::CommandPoolAllocator& poolAllocator
    ) -> std::future<core::Result<void>> {
        stats_ = {};
        frameAllocator_.Reset();
        ++frameNumber_;

        return std::async(
            std::launch::async, [this, &graph, &builder, &frame, device, &poolAllocator]() -> core::Result<void> {
                // Phase 1
                auto allocResult = allocator_.Allocate(graph, builder, device, frameNumber_);
                if (!allocResult) {
                    return allocResult;
                }
                stats_.allocation = allocator_.GetStats();

                // Phase 2
                auto physicalTable = allocator_.GetPhysicalTable();
                auto recordResult = recorder_.Record(
                    graph, builder, frame, device, poolAllocator, physicalTable, frameAllocator_, readbackRing_
                );
                if (!recordResult) {
                    allocator_.DestroyTransients(device);
                    return recordResult;
                }
                stats_.recording = recorder_.GetStats();

                return {};
            }
        );
    }

    // =========================================================================
    // SubmitAfterAsync — Phase 3 on render thread
    // =========================================================================

    auto RenderGraphExecutor::SubmitAfterAsync(const CompiledRenderGraph& graph, rhi::DeviceHandle device)
        -> core::Result<void> {
        return SubmitBatches(graph, recorder_.GetBatchRecordings(), device, &stats_.submission);
    }

}  // namespace miki::rg
