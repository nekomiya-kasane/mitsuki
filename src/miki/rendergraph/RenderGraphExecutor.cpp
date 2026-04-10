/** @file RenderGraphExecutor.cpp
 *  @brief Runtime execution engine for compiled render graphs.
 *
 *  Implements the three-phase execution pipeline:
 *    Phase 1: AllocateTransients — heap pool + placed resource creation
 *    Phase 2: RecordPasses — barrier emission + pass lambda invocation
 *    Phase 3: SubmitBatches — queue submission with timeline sync
 *
 *  See: specs/04-render-graph.md §6
 */

#include "miki/rendergraph/RenderGraphExecutor.h"

#include "miki/resource/ReadbackRing.h"
#include "miki/rhi/backend/AllBackends.h"
#include "miki/rhi/backend/AllCommandBuffers.h"

#include <array>
#include <cassert>
#include <cstring>
#include <utility>

namespace miki::rg {

    // =========================================================================
    // ResourceAccess → rhi::TextureUsage inference
    // =========================================================================

    namespace {

        /// @brief Infer RHI TextureUsage flags from combined ResourceAccess across all pass accesses.
        [[nodiscard]] constexpr auto InferTextureUsage(ResourceAccess combined) noexcept -> rhi::TextureUsage {
            auto usage = rhi::TextureUsage{0};
            if ((combined & ResourceAccess::ShaderReadOnly) != ResourceAccess::None) {
                usage = usage | rhi::TextureUsage::Sampled;
            }
            if ((combined & ResourceAccess::ShaderWrite) != ResourceAccess::None) {
                usage = usage | rhi::TextureUsage::Storage;
            }
            if ((combined & ResourceAccess::ColorAttachWrite) != ResourceAccess::None) {
                usage = usage | rhi::TextureUsage::ColorAttachment;
            }
            if ((combined & (ResourceAccess::DepthStencilWrite | ResourceAccess::DepthReadOnly))
                != ResourceAccess::None) {
                usage = usage | rhi::TextureUsage::DepthStencil;
            }
            if ((combined & ResourceAccess::TransferSrc) != ResourceAccess::None) {
                usage = usage | rhi::TextureUsage::TransferSrc;
            }
            if ((combined & ResourceAccess::TransferDst) != ResourceAccess::None) {
                usage = usage | rhi::TextureUsage::TransferDst;
            }
            if ((combined & ResourceAccess::InputAttachment) != ResourceAccess::None) {
                usage = usage | rhi::TextureUsage::InputAttachment;
            }
            if ((combined & ResourceAccess::ShadingRateRead) != ResourceAccess::None) {
                usage = usage | rhi::TextureUsage::ShadingRate;
            }
            // Ensure at least Sampled for general usability
            if (static_cast<uint32_t>(usage) == 0) {
                usage = rhi::TextureUsage::Sampled;
            }
            return usage;
        }

        /// @brief Infer RHI BufferUsage flags from combined ResourceAccess.
        [[nodiscard]] constexpr auto InferBufferUsage(ResourceAccess combined) noexcept -> rhi::BufferUsage {
            auto usage = rhi::BufferUsage{0};
            if ((combined & ResourceAccess::ShaderReadOnly) != ResourceAccess::None) {
                usage = usage | rhi::BufferUsage::Storage;
            }
            if ((combined & ResourceAccess::ShaderWrite) != ResourceAccess::None) {
                usage = usage | rhi::BufferUsage::Storage;
            }
            if ((combined & ResourceAccess::IndirectBuffer) != ResourceAccess::None) {
                usage = usage | rhi::BufferUsage::Indirect;
            }
            if ((combined & ResourceAccess::TransferSrc) != ResourceAccess::None) {
                usage = usage | rhi::BufferUsage::TransferSrc;
            }
            if ((combined & ResourceAccess::TransferDst) != ResourceAccess::None) {
                usage = usage | rhi::BufferUsage::TransferDst;
            }
            if ((combined & ResourceAccess::AccelStructRead) != ResourceAccess::None) {
                usage = usage | rhi::BufferUsage::AccelStructInput;
            }
            if ((combined & ResourceAccess::AccelStructWrite) != ResourceAccess::None) {
                usage = usage | rhi::BufferUsage::AccelStructStorage;
            }
            if (static_cast<uint32_t>(usage) == 0) {
                usage = rhi::BufferUsage::Storage;
            }
            return usage;
        }

        /// @brief Compute combined ResourceAccess for a resource across all passes.
        [[nodiscard]] auto ComputeCombinedAccess(uint16_t resourceIndex, const RenderGraphBuilder& builder) noexcept
            -> ResourceAccess {
            auto combined = ResourceAccess::None;
            for (auto& pass : builder.GetPasses()) {
                for (auto& r : pass.reads) {
                    if (r.handle.GetIndex() == resourceIndex) {
                        combined = combined | r.access;
                    }
                }
                for (auto& w : pass.writes) {
                    if (w.handle.GetIndex() == resourceIndex) {
                        combined = combined | w.access;
                    }
                }
            }
            return combined;
        }

        /// @brief Queue-type-based debug label colors for PIX/RenderDoc/NSight integration.
        /// Graphics=blue, AsyncCompute=green, Transfer=orange.
        constexpr auto GetQueueDebugColor(RGQueueType queue) noexcept -> std::array<float, 4> {
            switch (queue) {
                case RGQueueType::Graphics: return {0.2f, 0.4f, 0.9f, 1.0f};
                case RGQueueType::AsyncCompute: return {0.2f, 0.8f, 0.3f, 1.0f};
                case RGQueueType::Transfer: return {0.9f, 0.6f, 0.1f, 1.0f};
                default: return {0.5f, 0.5f, 0.5f, 1.0f};
            }
        }

        /// @brief Emit aliasing barriers for a specific topo-order position.
        /// Scans the AliasingLayout for barriers matching this position and emits them.
        void EmitAliasingBarriersForPass(
            rhi::CommandListHandle& cmd, uint32_t topoPosition, const AliasingLayout& aliasing,
            std::span<const rhi::TextureHandle> physicalTextures, std::span<const rhi::BufferHandle> physicalBuffers,
            std::span<const RGResourceNode> resources
        ) {
            for (size_t i = 0; i < aliasing.aliasingBarriers.size(); ++i) {
                if (i >= aliasing.aliasingBarrierPassPos.size()) {
                    break;
                }
                if (aliasing.aliasingBarrierPassPos[i] != topoPosition) {
                    continue;
                }

                auto& bc = aliasing.aliasingBarriers[i];
                if (bc.resourceIndex >= resources.size()) {
                    continue;
                }

                auto dstMapping = ResolveBarrierCombined(bc.dstAccess);
                auto dstLayout = bc.dstLayout != rhi::TextureLayout::Undefined ? bc.dstLayout : dstMapping.layout;
                auto dstStage
                    = dstMapping.stage != rhi::PipelineStage::None ? dstMapping.stage : rhi::PipelineStage::AllCommands;

                if (resources[bc.resourceIndex].kind == RGResourceKind::Texture) {
                    auto texHandle = bc.resourceIndex < physicalTextures.size() ? physicalTextures[bc.resourceIndex]
                                                                                : rhi::TextureHandle{};
                    if (!texHandle.IsValid()) {
                        continue;
                    }

                    rhi::TextureBarrierDesc desc{
                        .srcStage = rhi::PipelineStage::None,
                        .dstStage = dstStage,
                        .srcAccess = rhi::AccessFlags::None,
                        .dstAccess = dstMapping.access,
                        .oldLayout = rhi::TextureLayout::Undefined,
                        .newLayout = dstLayout,
                        .subresource = {},
                    };
                    cmd.Dispatch([&](auto& c) { c.CmdTextureBarrier(texHandle, desc); });
                } else {
                    auto bufHandle = bc.resourceIndex < physicalBuffers.size() ? physicalBuffers[bc.resourceIndex]
                                                                               : rhi::BufferHandle{};
                    if (!bufHandle.IsValid()) {
                        continue;
                    }

                    rhi::BufferBarrierDesc desc{
                        .srcStage = rhi::PipelineStage::None,
                        .dstStage = dstStage,
                        .srcAccess = rhi::AccessFlags::None,
                        .dstAccess = dstMapping.access,
                    };
                    cmd.Dispatch([&](auto& c) { c.CmdBufferBarrier(bufHandle, desc); });
                }
            }
        }

    }  // anonymous namespace

    // =========================================================================
    // Constructor / Destructor / Move
    // =========================================================================

    RenderGraphExecutor::RenderGraphExecutor(const ExecutorConfig& config)
        : config_(config), frameAllocator_(config.frameAllocatorCapacity), heapPool_(config.heapPoolConfig) {}

    RenderGraphExecutor::~RenderGraphExecutor() = default;

    RenderGraphExecutor::RenderGraphExecutor(RenderGraphExecutor&&) noexcept = default;
    auto RenderGraphExecutor::operator=(RenderGraphExecutor&&) noexcept -> RenderGraphExecutor& = default;

    // =========================================================================
    // Execute — main entry point
    // =========================================================================

    auto RenderGraphExecutor::Execute(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
        rhi::DeviceHandle device, frame::SyncScheduler& scheduler, frame::CommandPoolAllocator& poolAllocator
    ) -> core::Result<void> {
        // Reset per-frame state
        stats_ = {};
        frameAllocator_.Reset();
        batchRecordings_.clear();
        ++frameNumber_;

        // Phase 1: Allocate transient resources (with heap pooling + buffer suballocation)
        auto allocResult = AllocateTransients(graph, builder, device);
        if (!allocResult) {
            return allocResult;
        }

        // Propagate heap pool stats to executor stats
        auto& poolStats = heapPool_.GetStats();
        stats_.heapsReused = poolStats.heapsReused;
        stats_.heapsEvicted = poolStats.heapsEvicted;
        stats_.bufferSuballocations = poolStats.bufferSuballocations;
        stats_.bufferSuballocBytes = poolStats.bufferSuballocBytes;

        // Phase 2: Record command buffers (choose single-threaded or parallel path)
        auto recordResult = (config_.enableParallelRecording && config_.maxRecordingThreads > 1)
                                ? RecordPassesParallel(graph, builder, frame, device, poolAllocator)
                                : RecordPasses(graph, builder, frame, device, poolAllocator);
        if (!recordResult) {
            DestroyTransients(device);
            return recordResult;
        }

        // Phase 3: Submit batches
        auto submitResult = SubmitBatches(graph, device, scheduler);
        if (!submitResult) {
            DestroyTransients(device);
            return submitResult;
        }

        // Transient resources will be destroyed on next Execute() or destructor
        // (deferred destruction via FrameManager ensures GPU completion)
        return {};
    }

    // =========================================================================
    // ExecuteAsync — offload Phase 1 + 2 to worker thread (E-7)
    // =========================================================================

    auto RenderGraphExecutor::ExecuteAsync(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
        rhi::DeviceHandle device, frame::CommandPoolAllocator& poolAllocator
    ) -> std::future<core::Result<void>> {
        // Reset per-frame state on calling thread (safe: no concurrent access yet)
        stats_ = {};
        frameAllocator_.Reset();
        batchRecordings_.clear();

        return std::async(
            std::launch::async, [this, &graph, &builder, &frame, device, &poolAllocator]() -> core::Result<void> {
                // Phase 1: Allocate transient resources
                auto allocResult = AllocateTransients(graph, builder, device);
                if (!allocResult) {
                    return allocResult;
                }

                // Phase 2: Record command buffers
                auto recordResult = (config_.enableParallelRecording && config_.maxRecordingThreads > 1)
                                        ? RecordPassesParallel(graph, builder, frame, device, poolAllocator)
                                        : RecordPasses(graph, builder, frame, device, poolAllocator);
                if (!recordResult) {
                    DestroyTransients(device);
                    return recordResult;
                }

                return {};
            }
        );
    }

    // =========================================================================
    // SubmitAfterAsync — Phase 3 on render thread after async completes
    // =========================================================================

    auto RenderGraphExecutor::SubmitAfterAsync(
        const CompiledRenderGraph& graph, rhi::DeviceHandle device, frame::SyncScheduler& scheduler
    ) -> core::Result<void> {
        return SubmitBatches(graph, device, scheduler);
    }

    // =========================================================================
    // Phase 1: AllocateTransients
    // =========================================================================

    auto RenderGraphExecutor::AllocateTransients(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, rhi::DeviceHandle device
    ) -> core::Result<void> {
        auto& resources = builder.GetResources();
        auto resourceCount = static_cast<uint16_t>(resources.size());

        // Resize physical handle tables
        physicalTextures_.assign(resourceCount, {});
        physicalBuffers_.assign(resourceCount, {});
        physicalTextureViews_.assign(resourceCount, {});

        // Clean up previous frame's transient resources (textures/views/buffers)
        // Heaps are NOT destroyed here — they persist in the pool for cross-frame reuse.
        DestroyTransients(device);

        auto& aliasing = graph.aliasing;
        heapAllocations_.clear();
        bufferSuballocs_.clear();

        // --- Step 1: Acquire heaps from pool (§5.6.5 cross-frame reuse) ---
        if (config_.enableHeapPooling) {
            // Query backend capabilities for D3D12 Tier1 mixed-heap fallback
            bool useMixed = device.Dispatch([](auto& dev) {
                using RT = decltype(dev.GetCapabilities().resourceHeapTier);
                return dev.GetCapabilities().resourceHeapTier == RT::Tier1;
            });

            auto heapsResult = heapPool_.AcquireHeaps(aliasing, device, frameNumber_, useMixed);
            if (!heapsResult) {
                return std::unexpected(core::ErrorCode::OutOfMemory);
            }
            activeHeaps_ = *heapsResult;

            // Build heapAllocations_ from activeHeaps_ for backward compatibility
            for (uint32_t g = 0; g < kHeapGroupCount; ++g) {
                if (activeHeaps_[g].IsValid()) {
                    uint64_t heapSize = aliasing.heapGroupSizes[g];
                    heapAllocations_.push_back({
                        .group = static_cast<HeapGroupType>(g),
                        .heap = activeHeaps_[g],
                        .size = heapSize,
                    });
                    stats_.heapsCreated += (heapPool_.GetStats().heapsAllocated > 0) ? 1 : 0;
                    stats_.transientMemoryBytes += heapSize;
                }
            }
        } else {
            // Fallback: per-frame heap allocation (no pooling)
            for (uint32_t g = 0; g < kHeapGroupCount; ++g) {
                uint64_t heapSize = aliasing.heapGroupSizes[g];
                if (heapSize == 0) {
                    continue;
                }

                auto groupType = static_cast<HeapGroupType>(g);
                auto heapResult = device.Dispatch([&](auto& dev) {
                    return dev.CreateMemoryHeap(
                        rhi::MemoryHeapDesc{
                            .size = heapSize,
                            .memory = rhi::MemoryLocation::GpuOnly,
                            .groupHint = TransientHeapPool::ToGroupHint(groupType),
                            .debugName = "RG Transient Heap",
                        }
                    );
                });
                if (!heapResult) {
                    return std::unexpected(core::ErrorCode::OutOfMemory);
                }

                heapAllocations_.push_back({
                    .group = groupType,
                    .heap = *heapResult,
                    .size = heapSize,
                });
                activeHeaps_[g] = *heapResult;
                stats_.heapsCreated++;
                stats_.transientMemoryBytes += heapSize;
            }
        }

        // --- Step 2: Buffer suballocation (§5.6.7) ---
        // If enabled, transient buffers share a single parent buffer instead of individual placed resources
        bool useSuballoc
            = config_.enableBufferSuballocation && activeHeaps_[static_cast<uint32_t>(HeapGroupType::Buffer)].IsValid();
        if (useSuballoc) {
            auto subResult = heapPool_.PrepareBufferSuballocations(
                activeHeaps_[static_cast<uint32_t>(HeapGroupType::Buffer)], resources, aliasing, device
            );
            if (subResult) {
                bufferSuballocs_ = std::move(*subResult);
            }
            // Failure is non-fatal: fall back to individual buffer creation below
        }

        // --- Step 3: Create physical resources ---
        // Build a set of suballocated buffer indices for O(1) lookup
        std::vector<bool> isSuballocated(resourceCount, false);
        for (auto& sub : bufferSuballocs_) {
            if (sub.resourceIndex < resourceCount) {
                isSuballocated[sub.resourceIndex] = true;
                // Suballocated buffers share the parent buffer handle
                physicalBuffers_[sub.resourceIndex] = heapPool_.GetParentBuffer();
            }
        }

        for (uint16_t ri = 0; ri < resourceCount; ++ri) {
            auto& resNode = resources[ri];

            if (resNode.imported) {
                // Imported resources: use existing physical handles
                if (resNode.kind == RGResourceKind::Texture) {
                    physicalTextures_[ri] = resNode.importedTexture;
                    // Create a default view for imported textures
                    if (resNode.importedTexture.IsValid()) {
                        auto viewResult = device.Dispatch([&](auto& dev) {
                            return dev.CreateTextureView(
                                rhi::TextureViewDesc{
                                    .texture = resNode.importedTexture,
                                }
                            );
                        });
                        if (viewResult) {
                            physicalTextureViews_[ri] = *viewResult;
                            transientTextures_.push_back({ri, {}, *viewResult});  // track view for cleanup
                        }
                    }
                } else {
                    physicalBuffers_[ri] = resNode.importedBuffer;
                }
                continue;
            }

            // Skip suballocated buffers — already handled above
            if (isSuballocated[ri]) {
                stats_.transientBuffersAllocated++;
                continue;
            }

            // Transient resource: compute combined access for usage inference
            auto combinedAccess = ComputeCombinedAccess(ri, builder);

            if (resNode.kind == RGResourceKind::Texture) {
                auto inferredUsage = InferTextureUsage(combinedAccess);
                auto rhiDesc = resNode.textureDesc.ToRhiDesc(inferredUsage);

                auto texResult = device.Dispatch([&](auto& dev) { return dev.CreateTexture(rhiDesc); });
                if (!texResult) {
                    return std::unexpected(core::ErrorCode::OutOfMemory);
                }

                physicalTextures_[ri] = *texResult;
                stats_.transientTexturesAllocated++;

                // Bind to aliasing heap if slot assigned
                if (ri < aliasing.resourceToSlot.size() && aliasing.resourceToSlot[ri] != AliasingLayout::kNotAliased) {
                    uint32_t slotIdx = aliasing.resourceToSlot[ri];
                    if (slotIdx < aliasing.slots.size()) {
                        auto& slot = aliasing.slots[slotIdx];
                        // Find the heap for this group
                        for (auto& heap : heapAllocations_) {
                            if (heap.group == slot.heapGroup) {
                                device.Dispatch([&](auto& dev) {
                                    dev.AliasTextureMemory(*texResult, heap.heap, slot.heapOffset);
                                });
                                break;
                            }
                        }
                    }
                }

                // Create default texture view
                auto viewResult = device.Dispatch([&](auto& dev) {
                    return dev.CreateTextureView(rhi::TextureViewDesc{.texture = *texResult});
                });
                if (viewResult) {
                    physicalTextureViews_[ri] = *viewResult;
                    stats_.transientTextureViewsCreated++;
                }

                transientTextures_.push_back({
                    .resourceIndex = ri,
                    .texture = *texResult,
                    .view = viewResult ? *viewResult : rhi::TextureViewHandle{},
                });

            } else {
                // Buffer (non-suballocated path)
                auto inferredUsage = InferBufferUsage(combinedAccess);
                auto rhiDesc = resNode.bufferDesc.ToRhiDesc(inferredUsage);

                auto bufResult = device.Dispatch([&](auto& dev) { return dev.CreateBuffer(rhiDesc); });
                if (!bufResult) {
                    return std::unexpected(core::ErrorCode::OutOfMemory);
                }

                physicalBuffers_[ri] = *bufResult;
                stats_.transientBuffersAllocated++;

                // Bind to aliasing heap if slot assigned
                if (ri < aliasing.resourceToSlot.size() && aliasing.resourceToSlot[ri] != AliasingLayout::kNotAliased) {
                    uint32_t slotIdx = aliasing.resourceToSlot[ri];
                    if (slotIdx < aliasing.slots.size()) {
                        auto& slot = aliasing.slots[slotIdx];
                        for (auto& heap : heapAllocations_) {
                            if (heap.group == slot.heapGroup) {
                                device.Dispatch([&](auto& dev) {
                                    dev.AliasBufferMemory(*bufResult, heap.heap, slot.heapOffset);
                                });
                                break;
                            }
                        }
                    }
                }

                transientBuffers_.push_back({.resourceIndex = ri, .buffer = *bufResult});
            }
        }

        return {};
    }

    // =========================================================================
    // RecordSinglePass — shared helper for single-threaded and parallel paths
    // =========================================================================

    void RenderGraphExecutor::RecordSinglePass(
        rhi::CommandListHandle& cmdList, const CompiledRenderGraph& graph, uint32_t compiledPassIndex,
        const CompiledPassInfo& compiledPass, RGPassNode& passNode, const frame::FrameContext& frame, bool emitBarriers,
        RenderGraphBuilder& builder, const MergedGroupMembership* mergedMembership
    ) {
        uint32_t passIdx = compiledPass.passIndex;
        bool inMergedGroup = mergedMembership != nullptr && mergedMembership->groupIndex != UINT32_MAX;

        // Debug label begin (PIX/RenderDoc/NSight integration)
        if (config_.enableDebugLabels && passNode.name != nullptr) {
            auto color = GetQueueDebugColor(compiledPass.queue);
            cmdList.Dispatch([&](auto& cmd) { cmd.CmdBeginDebugLabel(passNode.name, color.data()); });
        }

        if (emitBarriers) {
            // Emit aliasing barriers for this pass position (before regular acquire barriers)
            EmitAliasingBarriersForPass(
                cmdList, compiledPassIndex, graph.aliasing, physicalTextures_, physicalBuffers_, builder.GetResources()
            );

            // Emit acquire barriers (skip inter-subpass barriers within merged groups —
            // those are handled by SubpassDependency / tile memory, not explicit barriers)
            if (!compiledPass.acquireBarriers.empty()) {
                EmitBarriers(cmdList, compiledPass.acquireBarriers, builder);
                stats_.barriersEmitted += static_cast<uint32_t>(compiledPass.acquireBarriers.size());
            }
        }

        // Build rendering attachments for graphics passes
        // When inside a merged group, skip per-pass CmdBeginRendering — the caller brackets the group
        std::vector<rhi::RenderingAttachment> colorAttachments;
        rhi::RenderingAttachment depthAttachment{};
        bool hasDepth = false;
        bool isGraphicsPass = (static_cast<uint8_t>(passNode.flags) & static_cast<uint8_t>(RGPassFlags::Graphics)) != 0;
        bool didBeginRendering = false;

        if (isGraphicsPass && !inMergedGroup) {
            BuildRenderingAttachments(passNode, builder, colorAttachments, depthAttachment, hasDepth);
            if (!colorAttachments.empty() || hasDepth) {
                rhi::RenderingDesc renderingDesc{
                    .renderArea = {{0, 0}, {frame.width, frame.height}},
                    .colorAttachments = colorAttachments,
                    .depthAttachment = hasDepth ? &depthAttachment : nullptr,
                };
                cmdList.Dispatch([&](auto& cmd) { cmd.CmdBeginRendering(renderingDesc); });
                didBeginRendering = true;
            }
        } else if (isGraphicsPass && inMergedGroup) {
            // Still build attachment info for RenderPassContext, but don't begin rendering
            BuildRenderingAttachments(passNode, builder, colorAttachments, depthAttachment, hasDepth);
        }

        // Build RenderPassContext
        RenderPassContext ctx{
            .commandList = cmdList,
            .bufferHandle = {},
            .passIndex = passIdx,
            .passName = passNode.name,
            .physicalTextures = physicalTextures_,
            .physicalBuffers = physicalBuffers_,
            .physicalTextureViews = physicalTextureViews_,
            .colorAttachments = colorAttachments,
            .depthAttachment = hasDepth ? &depthAttachment : nullptr,
            .frameAllocator = &frameAllocator_,
            .readbackRing = readbackRing_,
        };

        // Invoke pass execute lambda
        if (passNode.executeFn) {
            passNode.executeFn(ctx);
        }
        stats_.passesRecorded++;

        // End dynamic rendering (only if we began it — not for merged group subpasses)
        if (didBeginRendering) {
            cmdList.Dispatch([](auto& cmd) { cmd.CmdEndRendering(); });
        }

        // Emit release barriers (only in primary cmd buf path)
        if (emitBarriers && !compiledPass.releaseBarriers.empty()) {
            EmitBarriers(cmdList, compiledPass.releaseBarriers, builder);
            stats_.barriersEmitted += static_cast<uint32_t>(compiledPass.releaseBarriers.size());
        }

        // Debug label end
        if (config_.enableDebugLabels && passNode.name != nullptr) {
            cmdList.Dispatch([](auto& cmd) { cmd.CmdEndDebugLabel(); });
        }
    }

    // =========================================================================
    // Phase 2a: RecordPasses (single-threaded)
    // =========================================================================

    auto RenderGraphExecutor::RecordPasses(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
        rhi::DeviceHandle /*device*/, frame::CommandPoolAllocator& poolAllocator
    ) -> core::Result<void> {
        auto& passes = builder.GetPasses();

        // Build merged group lookup (O(N) where N = total subpass count across all groups)
        auto mergedLookup = BuildMergedGroupLookup(graph);

        batchRecordings_.clear();
        batchRecordings_.reserve(graph.batches.size());

        for (auto& batch : graph.batches) {
            auto rhiQueue = ToRhiQueueType(batch.queue);

            auto acqResult = poolAllocator.Acquire(frame.frameIndex, rhiQueue, /*threadIndex=*/0);
            if (!acqResult) {
                return std::unexpected(acqResult.error());
            }

            auto& acq = acqResult->acquisition;
            auto& cmdList = acq.listHandle;
            cmdList.Dispatch([](auto& cmd) { cmd.Begin(); });

            for (uint32_t compiledIdx : batch.passIndices) {
                if (compiledIdx >= graph.passes.size()) {
                    continue;
                }
                auto& compiledPass = graph.passes[compiledIdx];
                if (compiledPass.passIndex >= passes.size()) {
                    continue;
                }
                auto& passNode = passes[compiledPass.passIndex];

                // Merged group handling: bracket with CmdBeginRendering/CmdEndRendering
                const MergedGroupMembership* membership
                    = (compiledIdx < mergedLookup.size() && mergedLookup[compiledIdx].groupIndex != UINT32_MAX)
                          ? &mergedLookup[compiledIdx]
                          : nullptr;

                // Begin merged group rendering scope at first subpass
                if (membership != nullptr && membership->isFirst) {
                    auto& group = graph.mergedGroups[membership->groupIndex];
                    std::vector<rhi::RenderingAttachment> groupColor;
                    rhi::RenderingAttachment groupDepth{};
                    bool groupHasDepth = false;
                    BuildMergedGroupAttachments(group, graph, builder, groupColor, groupDepth, groupHasDepth);

                    if (!groupColor.empty() || groupHasDepth) {
                        uint32_t w = group.renderAreaWidth > 0 ? group.renderAreaWidth : frame.width;
                        uint32_t h = group.renderAreaHeight > 0 ? group.renderAreaHeight : frame.height;
                        rhi::RenderingDesc renderingDesc{
                            .renderArea = {{0, 0}, {w, h}},
                            .colorAttachments = groupColor,
                            .depthAttachment = groupHasDepth ? &groupDepth : nullptr,
                        };
                        cmdList.Dispatch([&](auto& cmd) { cmd.CmdBeginRendering(renderingDesc); });
                    }
                }

                RecordSinglePass(
                    cmdList, graph, compiledIdx, compiledPass, passNode, frame, /*emitBarriers=*/true, builder,
                    membership
                );

                // End merged group rendering scope at last subpass
                if (membership != nullptr && membership->isLast) {
                    cmdList.Dispatch([](auto& cmd) { cmd.CmdEndRendering(); });
                }
            }

            // E-10: Record pending readback transfers at end of last batch
            if (readbackRing_ && readbackRing_->GetPendingCopyCount() > 0) {
                readbackRing_->RecordTransfers(cmdList);
            }

            cmdList.Dispatch([](auto& cmd) { cmd.End(); });
            batchRecordings_.push_back({.queue = batch.queue, .commandBuffers = {acq.bufferHandle}});
        }

        return {};
    }

    // =========================================================================
    // Phase 2b: RecordPassesParallel (E-5/E-6)
    //
    // Design:
    //   - Primary cmd buf: barriers only (thread-safe, no contention)
    //   - Per-pass secondary cmd bufs: pass lambdas (one per thread, thread-affine pools)
    //   - std::jthread workers + std::latch synchronization
    //   - Invariant: each thread only touches its own command pool (threadIndex)
    //   - Invariant: barriers always in primary (correct ordering)
    // =========================================================================

    auto RenderGraphExecutor::RecordPassesParallel(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
        rhi::DeviceHandle /*device*/, frame::CommandPoolAllocator& poolAllocator
    ) -> core::Result<void> {
        auto& passes = builder.GetPasses();
        uint32_t threadCount
            = std::min(config_.maxRecordingThreads, static_cast<uint32_t>(std::thread::hardware_concurrency()));
        threadCount = std::max(threadCount, 1u);

        batchRecordings_.clear();
        batchRecordings_.reserve(graph.batches.size());

        for (auto& batch : graph.batches) {
            auto rhiQueue = ToRhiQueueType(batch.queue);
            uint32_t passCount = static_cast<uint32_t>(batch.passIndices.size());

            // Acquire primary cmd buf (thread 0) for barriers
            auto primaryResult = poolAllocator.Acquire(frame.frameIndex, rhiQueue, /*threadIndex=*/0);
            if (!primaryResult) {
                return std::unexpected(primaryResult.error());
            }
            auto& primaryAcq = primaryResult->acquisition;
            auto& primaryCmd = primaryAcq.listHandle;
            primaryCmd.Dispatch([](auto& cmd) { cmd.Begin(); });

            // For small batches or single pass, fall back to single-threaded inline recording
            if (passCount <= 1 || threadCount <= 1) {
                for (uint32_t compiledIdx : batch.passIndices) {
                    if (compiledIdx >= graph.passes.size()) {
                        continue;
                    }
                    auto& compiledPass = graph.passes[compiledIdx];
                    if (compiledPass.passIndex >= passes.size()) {
                        continue;
                    }
                    auto& passNode = passes[compiledPass.passIndex];
                    RecordSinglePass(
                        primaryCmd, graph, compiledIdx, compiledPass, passNode, frame, /*emitBarriers=*/true, builder
                    );
                }
                primaryCmd.Dispatch([](auto& cmd) { cmd.End(); });
                batchRecordings_.push_back({.queue = batch.queue, .commandBuffers = {primaryAcq.bufferHandle}});
                continue;
            }

            // Parallel path: acquire secondary cmd bufs (one per pass)
            struct PassRecording {
                rhi::CommandListHandle cmdList;
                rhi::CommandBufferHandle bufferHandle;
                uint32_t compiledIdx;
            };
            std::vector<PassRecording> passRecordings(passCount);

            // Acquire secondary cmd bufs, round-robin across threads
            for (uint32_t i = 0; i < passCount; ++i) {
                uint32_t threadIdx = (i % threadCount) + 1;  // +1: thread 0 is primary
                if (threadIdx >= threadCount) {
                    threadIdx = 1;
                }
                auto secResult = poolAllocator.AcquireSecondary(frame.frameIndex, rhiQueue, threadIdx);
                if (!secResult) {
                    return std::unexpected(secResult.error());
                }
                passRecordings[i].cmdList = secResult->acquisition.listHandle;
                passRecordings[i].bufferHandle = secResult->acquisition.bufferHandle;
                passRecordings[i].compiledIdx = batch.passIndices[i];
            }

            // Parallel record: partition passes across worker threads
            std::atomic<uint32_t> errorFlag{0};
            uint32_t actualThreads = std::min(threadCount, passCount);
            std::latch done(actualThreads);

            auto workerFn = [&](uint32_t workerIdx) {
                for (uint32_t i = workerIdx; i < passCount; i += actualThreads) {
                    auto& pr = passRecordings[i];
                    if (pr.compiledIdx >= graph.passes.size()) {
                        continue;
                    }
                    auto& compiledPass = graph.passes[pr.compiledIdx];
                    if (compiledPass.passIndex >= passes.size()) {
                        continue;
                    }
                    auto& passNode = passes[compiledPass.passIndex];

                    pr.cmdList.Dispatch([](auto& cmd) { cmd.Begin(); });
                    RecordSinglePass(
                        pr.cmdList, graph, pr.compiledIdx, compiledPass, passNode, frame, /*emitBarriers=*/false,
                        builder
                    );
                    pr.cmdList.Dispatch([](auto& cmd) { cmd.End(); });
                }
                done.count_down();
            };

            // Launch workers (worker 0 runs inline on calling thread)
            std::vector<std::jthread> workers;
            workers.reserve(actualThreads - 1);
            for (uint32_t w = 1; w < actualThreads; ++w) {
                workers.emplace_back([&workerFn, w] { workerFn(w); });
            }
            workerFn(0);  // Inline on calling thread
            done.wait();

            if (errorFlag.load(std::memory_order_relaxed) != 0) {
                primaryCmd.Dispatch([](auto& cmd) { cmd.End(); });
                return std::unexpected(core::ErrorCode::InvalidState);
            }

            // Primary cmd buf: emit barriers in order, then execute secondary cmd bufs
            std::vector<rhi::CommandBufferHandle> secondaryHandles;
            secondaryHandles.reserve(passCount);

            for (uint32_t i = 0; i < passCount; ++i) {
                auto& pr = passRecordings[i];
                if (pr.compiledIdx >= graph.passes.size()) {
                    continue;
                }
                auto& compiledPass = graph.passes[pr.compiledIdx];

                // Emit acquire barriers in primary (correct ordering guarantee)
                if (!compiledPass.acquireBarriers.empty()) {
                    EmitBarriers(primaryCmd, compiledPass.acquireBarriers, builder);
                    stats_.barriersEmitted += static_cast<uint32_t>(compiledPass.acquireBarriers.size());
                }

                // Execute this pass's secondary cmd buf
                secondaryHandles.clear();
                secondaryHandles.push_back(pr.bufferHandle);
                primaryCmd.Dispatch([&](auto& cmd) {
                    cmd.CmdExecuteSecondary(std::span<const rhi::CommandBufferHandle>{secondaryHandles});
                });
                stats_.passesRecorded++;

                // Emit release barriers in primary
                if (!compiledPass.releaseBarriers.empty()) {
                    EmitBarriers(primaryCmd, compiledPass.releaseBarriers, builder);
                    stats_.barriersEmitted += static_cast<uint32_t>(compiledPass.releaseBarriers.size());
                }
            }

            // E-10: Record pending readback transfers at end of batch
            if (readbackRing_ && readbackRing_->GetPendingCopyCount() > 0) {
                readbackRing_->RecordTransfers(primaryCmd);
            }

            primaryCmd.Dispatch([](auto& cmd) { cmd.End(); });
            batchRecordings_.push_back({.queue = batch.queue, .commandBuffers = {primaryAcq.bufferHandle}});
        }

        return {};
    }

    // =========================================================================
    // Phase 3: SubmitBatches
    // =========================================================================

    auto RenderGraphExecutor::SubmitBatches(
        const CompiledRenderGraph& graph, rhi::DeviceHandle device, frame::SyncScheduler& scheduler
    ) -> core::Result<void> {
        for (size_t bi = 0; bi < graph.batches.size() && bi < batchRecordings_.size(); ++bi) {
            auto& batch = graph.batches[bi];
            auto& recording = batchRecordings_[bi];
            auto rhiQueue = ToRhiQueueType(batch.queue);

            // Build wait semaphores from cross-queue dependencies
            std::vector<rhi::SemaphoreSubmitInfo> waitSems;
            for (auto& wait : batch.waits) {
                auto srcRhiQueue = ToRhiQueueType(wait.srcQueue);
                auto sem = scheduler.GetSemaphore(srcRhiQueue);
                if (sem.IsValid() && wait.timelineValue > 0) {
                    waitSems.push_back({
                        .semaphore = sem,
                        .value = wait.timelineValue,
                        .stageMask = rhi::PipelineStage::AllCommands,
                    });
                }
            }

            // Also add any pending waits accumulated in the scheduler for this queue
            auto pendingWaits = scheduler.GetPendingWaits(rhiQueue);
            for (auto& pw : pendingWaits) {
                waitSems.push_back({
                    .semaphore = pw.semaphore,
                    .value = pw.value,
                    .stageMask = pw.stageMask,
                });
            }

            // Build signal semaphores
            std::vector<rhi::SemaphoreSubmitInfo> signalSems;
            if (batch.signalTimeline) {
                auto signalValue = scheduler.AllocateSignal(rhiQueue);
                auto sem = scheduler.GetSemaphore(rhiQueue);
                if (sem.IsValid()) {
                    signalSems.push_back({
                        .semaphore = sem,
                        .value = signalValue,
                        .stageMask = rhi::PipelineStage::AllCommands,
                    });
                }
            }

            // Submit
            rhi::SubmitDesc submitDesc{
                .commandBuffers = recording.commandBuffers,
                .waitSemaphores = waitSems,
                .signalSemaphores = signalSems,
                .signalFence = {},
            };
            device.Dispatch([&](auto& dev) { dev.Submit(rhiQueue, submitDesc); });
            scheduler.CommitSubmit(rhiQueue);

            stats_.batchesSubmitted++;
        }

        return {};
    }

    // =========================================================================
    // DestroyTransients
    // =========================================================================

    void RenderGraphExecutor::DestroyTransients(rhi::DeviceHandle device) {
        // Destroy texture views and textures
        for (auto& tt : transientTextures_) {
            if (tt.view.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyTextureView(tt.view); });
            }
            if (tt.texture.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyTexture(tt.texture); });
            }
        }
        transientTextures_.clear();

        // Destroy buffers
        for (auto& tb : transientBuffers_) {
            if (tb.buffer.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyBuffer(tb.buffer); });
            }
        }
        transientBuffers_.clear();

        // Destroy heaps
        for (auto& ha : heapAllocations_) {
            if (ha.heap.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyMemoryHeap(ha.heap); });
            }
        }
        heapAllocations_.clear();
    }

    // =========================================================================
    // EmitBarriers — translate BarrierCommand to RHI barriers
    // =========================================================================

    void RenderGraphExecutor::EmitBarriers(
        rhi::CommandListHandle& cmd, std::span<const BarrierCommand> barriers, RenderGraphBuilder& builder
    ) {
        auto& resources = builder.GetResources();

        for (auto& bc : barriers) {
            if (bc.resourceIndex >= resources.size()) {
                continue;
            }

            auto srcMapping = ResolveBarrierCombined(bc.srcAccess);
            auto dstMapping = ResolveBarrierCombined(bc.dstAccess);

            // Use explicit layouts from BarrierCommand if set, otherwise fall back to resolved mapping
            auto srcLayout = bc.srcLayout != rhi::TextureLayout::Undefined ? bc.srcLayout : srcMapping.layout;
            auto dstLayout = bc.dstLayout != rhi::TextureLayout::Undefined ? bc.dstLayout : dstMapping.layout;

            auto srcStage
                = srcMapping.stage != rhi::PipelineStage::None ? srcMapping.stage : rhi::PipelineStage::TopOfPipe;
            auto dstStage
                = dstMapping.stage != rhi::PipelineStage::None ? dstMapping.stage : rhi::PipelineStage::BottomOfPipe;

            auto srcQueue = bc.isCrossQueue ? ToRhiQueueType(bc.srcQueue) : rhi::QueueType::Graphics;
            auto dstQueue = bc.isCrossQueue ? ToRhiQueueType(bc.dstQueue) : rhi::QueueType::Graphics;

            if (resources[bc.resourceIndex].kind == RGResourceKind::Texture) {
                auto texHandle = physicalTextures_[bc.resourceIndex];
                if (!texHandle.IsValid()) {
                    continue;
                }

                rhi::TextureSubresourceRange subresource{};
                if (bc.mipLevel != kAllMips) {
                    subresource.baseMipLevel = bc.mipLevel;
                    subresource.mipLevelCount = 1;
                }
                if (bc.arrayLayer != kAllLayers) {
                    subresource.baseArrayLayer = bc.arrayLayer;
                    subresource.arrayLayerCount = 1;
                }

                rhi::TextureBarrierDesc desc{
                    .srcStage = srcStage,
                    .dstStage = dstStage,
                    .srcAccess = srcMapping.access,
                    .dstAccess = dstMapping.access,
                    .oldLayout = srcLayout,
                    .newLayout = dstLayout,
                    .subresource = subresource,
                    .srcQueue = srcQueue,
                    .dstQueue = dstQueue,
                };
                cmd.Dispatch([&](auto& c) { c.CmdTextureBarrier(texHandle, desc); });

            } else {
                auto bufHandle = physicalBuffers_[bc.resourceIndex];
                if (!bufHandle.IsValid()) {
                    continue;
                }

                rhi::BufferBarrierDesc desc{
                    .srcStage = srcStage,
                    .dstStage = dstStage,
                    .srcAccess = srcMapping.access,
                    .dstAccess = dstMapping.access,
                    .srcQueue = srcQueue,
                    .dstQueue = dstQueue,
                };
                cmd.Dispatch([&](auto& c) { c.CmdBufferBarrier(bufHandle, desc); });
            }
        }
    }

    // =========================================================================
    // BuildRenderingAttachments
    // =========================================================================

    void RenderGraphExecutor::BuildRenderingAttachments(
        const RGPassNode& passNode, RenderGraphBuilder& /*builder*/, std::vector<rhi::RenderingAttachment>& outColor,
        rhi::RenderingAttachment& outDepth, bool& hasDepth
    ) {
        hasDepth = false;
        outColor.clear();

        // Use RGAttachmentInfo from pass node if available (populated by WriteColorAttachment/WriteDepthStencil)
        if (!passNode.colorAttachments.empty() || passNode.hasDepthStencil) {
            // Determine max slot index to size the color attachment array correctly
            uint32_t maxSlot = 0;
            for (auto& att : passNode.colorAttachments) {
                maxSlot = std::max(maxSlot, att.slotIndex + 1);
            }
            outColor.resize(maxSlot);

            for (auto& attInfo : passNode.colorAttachments) {
                auto idx = attInfo.handle.GetIndex();
                rhi::RenderingAttachment att{};
                if (idx < physicalTextureViews_.size()) {
                    att.view = physicalTextureViews_[idx];
                }
                att.loadOp = attInfo.loadOp;
                att.storeOp = attInfo.storeOp;
                att.clearValue = attInfo.clearValue;
                if (attInfo.slotIndex < maxSlot) {
                    outColor[attInfo.slotIndex] = att;
                }
            }

            if (passNode.hasDepthStencil) {
                auto idx = passNode.depthStencilAttachment.handle.GetIndex();
                if (idx < physicalTextureViews_.size()) {
                    outDepth.view = physicalTextureViews_[idx];
                }
                outDepth.loadOp = passNode.depthStencilAttachment.loadOp;
                outDepth.storeOp = passNode.depthStencilAttachment.storeOp;
                outDepth.clearValue = passNode.depthStencilAttachment.clearValue;
                hasDepth = true;
            }
            return;
        }

        // Fallback: infer from write accesses (backward compat for passes that don't use the new API)
        for (auto& w : passNode.writes) {
            auto idx = w.handle.GetIndex();
            if ((w.access & ResourceAccess::ColorAttachWrite) != ResourceAccess::None) {
                rhi::RenderingAttachment att{};
                if (idx < physicalTextureViews_.size()) {
                    att.view = physicalTextureViews_[idx];
                }
                att.loadOp = rhi::AttachmentLoadOp::Clear;
                att.storeOp = rhi::AttachmentStoreOp::Store;
                outColor.push_back(att);
            }
            if ((w.access & ResourceAccess::DepthStencilWrite) != ResourceAccess::None) {
                if (idx < physicalTextureViews_.size()) {
                    outDepth.view = physicalTextureViews_[idx];
                }
                outDepth.loadOp = rhi::AttachmentLoadOp::Clear;
                outDepth.storeOp = rhi::AttachmentStoreOp::Store;
                outDepth.clearValue.depthStencil = {1.0f, 0};
                hasDepth = true;
            }
        }
    }

    // =========================================================================
    // BuildMergedGroupLookup — reverse map compiledPassIndex -> merged group
    // =========================================================================

    auto RenderGraphExecutor::BuildMergedGroupLookup(const CompiledRenderGraph& graph) const
        -> std::vector<MergedGroupMembership> {
        auto lookup = std::vector<MergedGroupMembership>(graph.passes.size());

        for (uint32_t gi = 0; gi < graph.mergedGroups.size(); ++gi) {
            auto& group = graph.mergedGroups[gi];
            for (uint32_t si = 0; si < group.subpassIndices.size(); ++si) {
                auto compiledIdx = group.subpassIndices[si];
                if (compiledIdx < lookup.size()) {
                    lookup[compiledIdx] = MergedGroupMembership{
                        .groupIndex = gi,
                        .subpassPosition = si,
                        .isFirst = (si == 0),
                        .isLast = (si == group.subpassIndices.size() - 1),
                    };
                }
            }
        }
        return lookup;
    }

    // =========================================================================
    // BuildMergedGroupAttachments — union of all subpass attachments
    // =========================================================================

    void RenderGraphExecutor::BuildMergedGroupAttachments(
        const MergedRenderPassGroup& group, const CompiledRenderGraph& graph, RenderGraphBuilder& builder,
        std::vector<rhi::RenderingAttachment>& outColor, rhi::RenderingAttachment& outDepth, bool& hasDepth
    ) {
        hasDepth = false;
        outColor.clear();

        auto& passes = builder.GetPasses();

        // Collect attachments from the first subpass (which defines Clear/Load semantics)
        // Subsequent subpasses in the group inherit via tile memory
        if (!group.subpassIndices.empty()) {
            auto firstCompiledIdx = group.subpassIndices[0];
            if (firstCompiledIdx < graph.passes.size()) {
                auto firstPassIdx = graph.passes[firstCompiledIdx].passIndex;
                if (firstPassIdx < passes.size()) {
                    BuildRenderingAttachments(passes[firstPassIdx], builder, outColor, outDepth, hasDepth);
                }
            }
        }

        // For shared attachments used by later subpasses but not the first, add them with Load/Store
        for (auto resIdx : group.sharedAttachments) {
            if (resIdx < physicalTextureViews_.size() && physicalTextureViews_[resIdx].IsValid()) {
                bool alreadyIncluded = false;
                for (auto& c : outColor) {
                    if (c.view == physicalTextureViews_[resIdx]) {
                        alreadyIncluded = true;
                        break;
                    }
                }
                if (outDepth.view == physicalTextureViews_[resIdx]) {
                    alreadyIncluded = true;
                }
                if (!alreadyIncluded) {
                    outColor.push_back(
                        rhi::RenderingAttachment{
                            .view = physicalTextureViews_[resIdx],
                            .loadOp = rhi::AttachmentLoadOp::Load,
                            .storeOp = rhi::AttachmentStoreOp::Store,
                            .clearValue = {},
                            .resolveView = {},
                        }
                    );
                }
            }
        }

        // Use merged group dimensions if specified, otherwise defer to frame context
        // (render area is set at CmdBeginRendering call site)
    }

}  // namespace miki::rg
