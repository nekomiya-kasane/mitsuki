/** @file PassRecorder.cpp
 *  @brief Phase 2 of render graph execution: command buffer recording.
 *  See: PassRecorder.h
 */

#include "miki/rendergraph/PassRecorder.h"

#include "miki/rendergraph/BarrierEmitter.h"
#include "miki/resource/ReadbackRing.h"
#include "miki/rhi/backend/AllBackends.h"
#include "miki/rhi/backend/AllCommandBuffers.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <latch>
#include <thread>
#include <utility>

namespace miki::rg {

    // =========================================================================
    // Construction / Move
    // =========================================================================

    PassRecorder::PassRecorder(const PassRecorderConfig& config) : config_(config) {}
    PassRecorder::~PassRecorder() = default;
    PassRecorder::PassRecorder(PassRecorder&&) noexcept = default;
    auto PassRecorder::operator=(PassRecorder&&) noexcept -> PassRecorder& = default;

    // =========================================================================
    // Record — main entry point
    // =========================================================================

    auto PassRecorder::Record(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
        rhi::DeviceHandle /*device*/, frame::CommandPoolAllocator& poolAllocator,
        const PhysicalResourceTable& physicalTable, core::LinearAllocator& frameAllocator,
        resource::ReadbackRing* readbackRing
    ) -> core::Result<void> {
        stats_ = {};
        batchRecordings_.clear();

        session_ = RecordingSession{
            .graph = &graph,
            .builder = &builder,
            .frame = &frame,
            .poolAllocator = &poolAllocator,
            .physicalTable = &physicalTable,
            .frameAllocator = &frameAllocator,
            .readbackRing = readbackRing,
        };

        if (config_.enableParallelRecording && config_.maxRecordingThreads > 1) {
            return RecordParallel();
        }
        return RecordSingleThreaded();
    }

    // =========================================================================
    // RecordSingleThreaded
    // =========================================================================

    auto PassRecorder::RecordSingleThreaded() -> core::Result<void> {
        auto& graph = *session_.graph;
        auto& passes = session_.builder->GetPasses();
        auto& frame = *session_.frame;
        auto mergedLookup = BuildMergedGroupLookup(graph);

        batchRecordings_.reserve(graph.batches.size());

        for (auto& batch : graph.batches) {
            auto rhiQueue = ToRhiQueueType(batch.queue);
            auto acqResult = session_.poolAllocator->Acquire(frame.frameIndex, rhiQueue, /*threadIndex=*/0);
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

                // Merged group membership
                const MergedGroupMembership* membership
                    = (compiledIdx < mergedLookup.size() && mergedLookup[compiledIdx].groupIndex != UINT32_MAX)
                          ? &mergedLookup[compiledIdx]
                          : nullptr;

                // Begin merged group rendering scope
                if (membership != nullptr && membership->isFirst) {
                    auto& group = graph.mergedGroups[membership->groupIndex];
                    std::vector<rhi::RenderingAttachment> groupColor;
                    rhi::RenderingAttachment groupDepth{};
                    bool groupHasDepth = false;
                    BuildMergedGroupAttachments(group, groupColor, groupDepth, groupHasDepth);

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

                RecordSinglePass(cmdList, compiledIdx, compiledPass, passNode, /*emitBarriers=*/true, membership);

                // End merged group rendering scope
                if (membership != nullptr && membership->isLast) {
                    cmdList.Dispatch([](auto& cmd) { cmd.CmdEndRendering(); });
                }
            }

            // E-10: Record pending readback transfers
            if (session_.readbackRing && session_.readbackRing->GetPendingCopyCount() > 0) {
                session_.readbackRing->RecordTransfers(cmdList);
            }

            cmdList.Dispatch([](auto& cmd) { cmd.End(); });
            batchRecordings_.push_back({.queue = batch.queue, .commandBuffers = {acq.bufferHandle}});
        }

        return {};
    }

    // =========================================================================
    // RecordParallel (E-5/E-6)
    // =========================================================================

    auto PassRecorder::RecordParallel() -> core::Result<void> {
        auto& graph = *session_.graph;
        auto& passes = session_.builder->GetPasses();
        auto& frame = *session_.frame;
        uint32_t threadCount
            = std::min(config_.maxRecordingThreads, static_cast<uint32_t>(std::thread::hardware_concurrency()));
        threadCount = std::max(threadCount, 1u);

        batchRecordings_.reserve(graph.batches.size());

        for (auto& batch : graph.batches) {
            auto rhiQueue = ToRhiQueueType(batch.queue);
            uint32_t passCount = static_cast<uint32_t>(batch.passIndices.size());

            // Acquire primary cmd buf for barriers
            auto primaryResult = session_.poolAllocator->Acquire(frame.frameIndex, rhiQueue, /*threadIndex=*/0);
            if (!primaryResult) {
                return std::unexpected(primaryResult.error());
            }
            auto& primaryAcq = primaryResult->acquisition;
            auto& primaryCmd = primaryAcq.listHandle;
            primaryCmd.Dispatch([](auto& cmd) { cmd.Begin(); });

            // Small batches: fall back to inline recording
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
                    RecordSinglePass(primaryCmd, compiledIdx, compiledPass, passNode, /*emitBarriers=*/true);
                }
                primaryCmd.Dispatch([](auto& cmd) { cmd.End(); });
                batchRecordings_.push_back({.queue = batch.queue, .commandBuffers = {primaryAcq.bufferHandle}});
                continue;
            }

            // Parallel path: acquire secondary cmd bufs
            struct PassRec {
                rhi::CommandListHandle cmdList;
                rhi::CommandBufferHandle bufferHandle;
                uint32_t compiledIdx = 0;
            };
            std::vector<PassRec> passRecordings(passCount);

            for (uint32_t i = 0; i < passCount; ++i) {
                uint32_t threadIdx = (i % threadCount) + 1;
                if (threadIdx >= threadCount) {
                    threadIdx = 1;
                }
                auto secResult = session_.poolAllocator->AcquireSecondary(frame.frameIndex, rhiQueue, threadIdx);
                if (!secResult) {
                    return std::unexpected(secResult.error());
                }
                passRecordings[i].cmdList = secResult->acquisition.listHandle;
                passRecordings[i].bufferHandle = secResult->acquisition.bufferHandle;
                passRecordings[i].compiledIdx = batch.passIndices[i];
            }

            // Parallel record
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
                    RecordSinglePass(pr.cmdList, pr.compiledIdx, compiledPass, passNode, /*emitBarriers=*/false);
                    pr.cmdList.Dispatch([](auto& cmd) { cmd.End(); });
                }
                done.count_down();
            };

            // Launch workers (worker 0 runs inline)
            std::vector<std::jthread> workers;
            workers.reserve(actualThreads - 1);
            for (uint32_t w = 1; w < actualThreads; ++w) {
                workers.emplace_back([&workerFn, w] { workerFn(w); });
            }
            workerFn(0);
            done.wait();

            if (errorFlag.load(std::memory_order_relaxed) != 0) {
                primaryCmd.Dispatch([](auto& cmd) { cmd.End(); });
                return std::unexpected(core::ErrorCode::InvalidState);
            }

            // Primary: emit barriers in order, then execute secondaries
            auto& resources = session_.builder->GetResources();
            auto& physTex = session_.physicalTable->textures;
            auto& physBuf = session_.physicalTable->buffers;
            std::vector<rhi::CommandBufferHandle> secondaryHandles;
            secondaryHandles.reserve(passCount);

            for (uint32_t i = 0; i < passCount; ++i) {
                auto& pr = passRecordings[i];
                if (pr.compiledIdx >= graph.passes.size()) {
                    continue;
                }
                auto& compiledPass = graph.passes[pr.compiledIdx];

                if (!compiledPass.acquireBarriers.empty()) {
                    EmitBarriers(primaryCmd, compiledPass.acquireBarriers, resources, physTex, physBuf);
                    stats_.barriersEmitted += static_cast<uint32_t>(compiledPass.acquireBarriers.size());
                }

                secondaryHandles.clear();
                secondaryHandles.push_back(pr.bufferHandle);
                primaryCmd.Dispatch([&](auto& cmd) {
                    cmd.CmdExecuteSecondary(std::span<const rhi::CommandBufferHandle>{secondaryHandles});
                });
                stats_.passesRecorded++;

                if (!compiledPass.releaseBarriers.empty()) {
                    EmitBarriers(primaryCmd, compiledPass.releaseBarriers, resources, physTex, physBuf);
                    stats_.barriersEmitted += static_cast<uint32_t>(compiledPass.releaseBarriers.size());
                }
            }

            // E-10: Record pending readback transfers
            if (session_.readbackRing && session_.readbackRing->GetPendingCopyCount() > 0) {
                session_.readbackRing->RecordTransfers(primaryCmd);
            }

            primaryCmd.Dispatch([](auto& cmd) { cmd.End(); });
            batchRecordings_.push_back({.queue = batch.queue, .commandBuffers = {primaryAcq.bufferHandle}});
        }

        return {};
    }

    // =========================================================================
    // RecordSinglePass
    // =========================================================================

    void PassRecorder::RecordSinglePass(
        rhi::CommandListHandle& cmdList, uint32_t compiledPassIndex, const CompiledPassInfo& compiledPass,
        RGPassNode& passNode, bool emitBarriers, const MergedGroupMembership* mergedMembership
    ) {
        auto& graph = *session_.graph;
        auto& physicalTable = *session_.physicalTable;
        auto& frame = *session_.frame;
        uint32_t passIdx = compiledPass.passIndex;
        bool inMergedGroup = mergedMembership != nullptr && mergedMembership->groupIndex != UINT32_MAX;

        // Debug label begin
        if (config_.enableDebugLabels && passNode.name != nullptr) {
            auto color = GetQueueDebugColor(compiledPass.queue);
            cmdList.Dispatch([&](auto& cmd) { cmd.CmdBeginDebugLabel(passNode.name, color.data()); });
        }

        if (emitBarriers) {
            EmitAliasingBarriers(
                cmdList, compiledPassIndex, graph.aliasing, physicalTable.textures, physicalTable.buffers,
                session_.builder->GetResources()
            );

            if (!compiledPass.acquireBarriers.empty()) {
                EmitBarriers(
                    cmdList, compiledPass.acquireBarriers, session_.builder->GetResources(), physicalTable.textures,
                    physicalTable.buffers
                );
                stats_.barriersEmitted += static_cast<uint32_t>(compiledPass.acquireBarriers.size());
            }
        }

        // Build rendering attachments for graphics passes
        std::vector<rhi::RenderingAttachment> colorAttachments;
        rhi::RenderingAttachment depthAttachment{};
        bool hasDepth = false;
        bool isGraphicsPass = (passNode.flags & RGPassFlags::Graphics) != RGPassFlags::None;
        bool didBeginRendering = false;

        if (isGraphicsPass) {
            BuildRenderingAttachments(
                passNode, physicalTable.textureViews, colorAttachments, depthAttachment, hasDepth
            );
            if (!inMergedGroup && (!colorAttachments.empty() || hasDepth)) {
                rhi::RenderingDesc renderingDesc{
                    .renderArea = {{0, 0}, {frame.width, frame.height}},
                    .colorAttachments = colorAttachments,
                    .depthAttachment = hasDepth ? &depthAttachment : nullptr,
                };
                cmdList.Dispatch([&](auto& cmd) { cmd.CmdBeginRendering(renderingDesc); });
                didBeginRendering = true;
            }
        }

        // Build RenderPassContext
        RenderPassContext ctx{
            .commandList = cmdList,
            .bufferHandle = {},
            .passIndex = passIdx,
            .passName = passNode.name,
            .physicalTextures = physicalTable.textures,
            .physicalBuffers = physicalTable.buffers,
            .physicalTextureViews = physicalTable.textureViews,
            .colorAttachments = colorAttachments,
            .depthAttachment = hasDepth ? &depthAttachment : nullptr,
            .frameAllocator = session_.frameAllocator,
            .readbackRing = session_.readbackRing,
        };

        if (passNode.executeFn) {
            passNode.executeFn(ctx);
        }
        stats_.passesRecorded++;

        if (didBeginRendering) {
            cmdList.Dispatch([](auto& cmd) { cmd.CmdEndRendering(); });
        }

        // Release barriers
        if (emitBarriers && !compiledPass.releaseBarriers.empty()) {
            EmitBarriers(
                cmdList, compiledPass.releaseBarriers, session_.builder->GetResources(), physicalTable.textures,
                physicalTable.buffers
            );
            stats_.barriersEmitted += static_cast<uint32_t>(compiledPass.releaseBarriers.size());
        }

        // Debug label end
        if (config_.enableDebugLabels && passNode.name != nullptr) {
            cmdList.Dispatch([](auto& cmd) { cmd.CmdEndDebugLabel(); });
        }
    }

    // =========================================================================
    // BuildRenderingAttachments
    // =========================================================================

    void PassRecorder::BuildRenderingAttachments(
        const RGPassNode& passNode, std::span<const rhi::TextureViewHandle> physicalTextureViews,
        std::vector<rhi::RenderingAttachment>& outColor, rhi::RenderingAttachment& outDepth, bool& hasDepth
    ) {
        hasDepth = false;
        outColor.clear();

        // Primary path: use RGAttachmentInfo from pass node
        if (!passNode.colorAttachments.empty() || passNode.hasDepthStencil) {
            uint32_t maxSlot = 0;
            for (auto& att : passNode.colorAttachments) {
                maxSlot = std::max(maxSlot, att.slotIndex + 1);
            }
            outColor.resize(maxSlot);

            for (auto& attInfo : passNode.colorAttachments) {
                auto idx = attInfo.handle.GetIndex();
                rhi::RenderingAttachment att{};
                if (idx < physicalTextureViews.size()) {
                    att.view = physicalTextureViews[idx];
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
                if (idx < physicalTextureViews.size()) {
                    outDepth.view = physicalTextureViews[idx];
                }
                outDepth.loadOp = passNode.depthStencilAttachment.loadOp;
                outDepth.storeOp = passNode.depthStencilAttachment.storeOp;
                outDepth.clearValue = passNode.depthStencilAttachment.clearValue;
                hasDepth = true;
            }
            return;
        }

        // Fallback: infer from write accesses
        for (auto& w : passNode.writes) {
            auto idx = w.handle.GetIndex();
            if ((w.access & ResourceAccess::ColorAttachWrite) != ResourceAccess::None) {
                rhi::RenderingAttachment att{};
                if (idx < physicalTextureViews.size()) {
                    att.view = physicalTextureViews[idx];
                }
                att.loadOp = rhi::AttachmentLoadOp::Clear;
                att.storeOp = rhi::AttachmentStoreOp::Store;
                outColor.push_back(att);
            }
            if ((w.access & ResourceAccess::DepthStencilWrite) != ResourceAccess::None) {
                if (idx < physicalTextureViews.size()) {
                    outDepth.view = physicalTextureViews[idx];
                }
                outDepth.loadOp = rhi::AttachmentLoadOp::Clear;
                outDepth.storeOp = rhi::AttachmentStoreOp::Store;
                outDepth.clearValue.depthStencil = {1.0f, 0};
                hasDepth = true;
            }
        }
    }

    // =========================================================================
    // BuildMergedGroupLookup
    // =========================================================================

    auto PassRecorder::BuildMergedGroupLookup(const CompiledRenderGraph& graph) -> std::vector<MergedGroupMembership> {
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
    // BuildMergedGroupAttachments
    // =========================================================================

    void PassRecorder::BuildMergedGroupAttachments(
        const MergedRenderPassGroup& group, std::vector<rhi::RenderingAttachment>& outColor,
        rhi::RenderingAttachment& outDepth, bool& hasDepth
    ) {
        hasDepth = false;
        outColor.clear();

        auto& graph = *session_.graph;
        auto& passes = session_.builder->GetPasses();
        auto physicalTextureViews = session_.physicalTable->textureViews;

        // First subpass defines Clear/Load semantics
        if (!group.subpassIndices.empty()) {
            auto firstCompiledIdx = group.subpassIndices[0];
            if (firstCompiledIdx < graph.passes.size()) {
                auto firstPassIdx = graph.passes[firstCompiledIdx].passIndex;
                if (firstPassIdx < passes.size()) {
                    BuildRenderingAttachments(passes[firstPassIdx], physicalTextureViews, outColor, outDepth, hasDepth);
                }
            }
        }

        // Add shared attachments from later subpasses
        for (auto resIdx : group.sharedAttachments) {
            if (resIdx < physicalTextureViews.size() && physicalTextureViews[resIdx].IsValid()) {
                bool alreadyIncluded = false;
                for (auto& c : outColor) {
                    if (c.view == physicalTextureViews[resIdx]) {
                        alreadyIncluded = true;
                        break;
                    }
                }
                if (outDepth.view == physicalTextureViews[resIdx]) {
                    alreadyIncluded = true;
                }
                if (!alreadyIncluded) {
                    outColor.push_back(
                        rhi::RenderingAttachment{
                            .view = physicalTextureViews[resIdx],
                            .loadOp = rhi::AttachmentLoadOp::Load,
                            .storeOp = rhi::AttachmentStoreOp::Store,
                            .clearValue = {},
                            .resolveView = {},
                        }
                    );
                }
            }
        }
    }

}  // namespace miki::rg
