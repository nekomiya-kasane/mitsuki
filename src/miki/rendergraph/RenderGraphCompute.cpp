/** @file RenderGraphCompute.cpp
 *  @brief GPGPU & Heterogeneous Compute support implementation.
 *
 *  See RenderGraphCompute.h for design overview.
 *  Namespace: miki::rg
 */

#include "miki/rendergraph/RenderGraphCompute.h"

#include <algorithm>
#include <cassert>
#include <format>

#include "miki/rendergraph/RenderGraphBuilder.h"

namespace miki::rg {

    // =========================================================================
    // ComputeSubgraphBuilder
    // =========================================================================

    ComputeSubgraphBuilder::ComputeSubgraphBuilder(const ComputeSubgraphConfig& config)
        : config_(config), builder_(std::make_unique<RenderGraphBuilder>()) {}

    ComputeSubgraphBuilder::~ComputeSubgraphBuilder() = default;

    ComputeSubgraphBuilder::ComputeSubgraphBuilder(ComputeSubgraphBuilder&&) noexcept = default;
    auto ComputeSubgraphBuilder::operator=(ComputeSubgraphBuilder&&) noexcept -> ComputeSubgraphBuilder& = default;

    auto ComputeSubgraphBuilder::AddComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute)
        -> RGPassHandle {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        return builder_->AddComputePass(name, std::move(setup), std::move(execute));
    }

    auto ComputeSubgraphBuilder::AddAsyncComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute)
        -> RGPassHandle {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        return builder_->AddAsyncComputePass(name, std::move(setup), std::move(execute));
    }

    auto ComputeSubgraphBuilder::AddTransferPass(const char* name, PassSetupFn setup, PassExecuteFn execute)
        -> RGPassHandle {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        return builder_->AddTransferPass(name, std::move(setup), std::move(execute));
    }

    auto ComputeSubgraphBuilder::CreateBuffer(const RGBufferDesc& desc) -> RGResourceHandle {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        return builder_->CreateBuffer(desc);
    }

    auto ComputeSubgraphBuilder::CreateTexture(const RGTextureDesc& desc) -> RGResourceHandle {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        return builder_->CreateTexture(desc);
    }

    auto ComputeSubgraphBuilder::ImportBuffer(rhi::BufferHandle buffer, const char* name) -> RGResourceHandle {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        return builder_->ImportBuffer(buffer, name);
    }

    auto ComputeSubgraphBuilder::ImportTexture(rhi::TextureHandle texture, const char* name) -> RGResourceHandle {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        return builder_->ImportTexture(texture, name);
    }

    auto ComputeSubgraphBuilder::AddWorkGraphPass(
        const char* name, const WorkGraphPassDesc& desc, PassSetupFn setup, PassExecuteFn execute,
        WorkGraphFallbackFn fallback
    ) -> RGPassHandle {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        // Work graph passes are compute passes from the render graph's perspective.
        // The actual DispatchGraph vs DispatchIndirect decision happens at execution time.
        auto handle = builder_->AddComputePass(name, std::move(setup), std::move(execute));
        workGraphDescs_.push_back(desc);
        workGraphFallbacks_.push_back(std::move(fallback));
        return handle;
    }

    void ComputeSubgraphBuilder::WaitForAsyncTask(RGPassHandle /*pass*/, uint64_t taskHandle) {
        asyncDependencies_.push_back(
            AsyncTaskDependency{
                .taskHandle = taskHandle,
                .timelineValue = 0,  // Resolved at compile time
                .taskQueue = RGQueueType::AsyncCompute,
            }
        );
    }

    auto ComputeSubgraphBuilder::GetPassCount() const noexcept -> uint32_t {
        return builder_ ? builder_->GetPassCount() : 0;
    }

    auto ComputeSubgraphBuilder::GetResourceCount() const noexcept -> uint32_t {
        return builder_ ? builder_->GetResourceCount() : 0;
    }

    auto ComputeSubgraphBuilder::TakeBuilder() -> RenderGraphBuilder& {
        assert(builder_ && "ComputeSubgraphBuilder: builder already consumed");
        return *builder_;
    }

    auto ComputeSubgraphBuilder::GetBuilder() const noexcept -> const RenderGraphBuilder& {
        assert(builder_ && "ComputeSubgraphBuilder: builder not available");
        return *builder_;
    }

    // =========================================================================
    // MultiFrameTaskResolver
    // =========================================================================

    auto MultiFrameTaskResolver::ResolveAll(std::span<const AsyncTaskDependency> dependencies) const
        -> std::vector<ResolvedAsyncWait> {
        stats_ = {};
        stats_.totalDependencies = static_cast<uint32_t>(dependencies.size());

        std::vector<ResolvedAsyncWait> results;
        results.reserve(dependencies.size());

        for (const auto& dep : dependencies) {
            ResolvedAsyncWait wait;
            wait.taskHandle = dep.taskHandle;
            wait.waitOnQueue = dep.taskQueue;

            if (dep.taskHandle == 0) {
                // Invalid task handle — skip
                wait.status = AsyncTaskStatus::NotResolved;
                results.push_back(wait);
                continue;
            }

            // Query task status via callback
            if (taskQuery_) {
                wait.status = taskQuery_(dep.taskHandle);
            } else {
                // No query function — assume pending (conservative, inject wait)
                wait.status = AsyncTaskStatus::Pending;
            }

            switch (wait.status) {
                case AsyncTaskStatus::Ready:
                    // Task already complete — elide the wait, no sync point needed
                    stats_.readyElided++;
                    break;

                case AsyncTaskStatus::Pending:
                    // Task in flight — resolve timeline value and inject wait
                    if (timelineResolver_) {
                        wait.waitTimelineValue = timelineResolver_(dep.taskHandle);
                    } else {
                        wait.waitTimelineValue = dep.timelineValue;
                    }
                    stats_.pendingInjected++;
                    break;

                case AsyncTaskStatus::Failed:
                    // Task failed — mark for graceful degradation
                    stats_.failedPasses++;
                    break;

                case AsyncTaskStatus::NotResolved: break;
            }

            results.push_back(wait);
        }

        return results;
    }

    auto MultiFrameTaskResolver::FormatStatus() const -> std::string {
        return std::format(
            "MultiFrameTaskResolver: {} deps ({} elided, {} injected, {} failed)", stats_.totalDependencies,
            stats_.readyElided, stats_.pendingInjected, stats_.failedPasses
        );
    }

    // =========================================================================
    // DeviceAffinityResolver
    // =========================================================================

    void DeviceAffinityResolver::SetDevices(std::span<const DeviceInfo> devices) {
        devices_.assign(devices.begin(), devices.end());
    }

    auto DeviceAffinityResolver::ResolveAll(
        std::span<const RGDeviceAffinityConfig> affinityHints, std::span<const DependencyEdge> edges
    ) const -> std::vector<uint8_t> {
        (void)edges;  // Used in future multi-device load-balancing
        stats_ = {};
        stats_.totalPasses = static_cast<uint32_t>(affinityHints.size());

        std::vector<uint8_t> assignments(affinityHints.size(), primaryDevice_);

        if (devices_.size() <= 1) {
            // Single-device mode: all passes on primary device
            stats_.passesOnPrimary = stats_.totalPasses;
            return assignments;
        }

        // Multi-device resolution
        for (uint32_t i = 0; i < affinityHints.size(); ++i) {
            const auto& hint = affinityHints[i];

            switch (hint.affinity) {
                case RGDeviceAffinity::Any:
                    // Use primary device (could be optimized with load balancing in future)
                    assignments[i] = primaryDevice_;
                    break;

                case RGDeviceAffinity::DiscreteGPU:
                    // Find first discrete GPU
                    for (const auto& dev : devices_) {
                        if (!dev.isIntegrated) {
                            assignments[i] = dev.deviceIndex;
                            break;
                        }
                    }
                    break;

                case RGDeviceAffinity::IntegratedGPU:
                    // Find first integrated GPU
                    for (const auto& dev : devices_) {
                        if (dev.isIntegrated) {
                            assignments[i] = dev.deviceIndex;
                            break;
                        }
                    }
                    break;

                case RGDeviceAffinity::CopyEngine:
                    // Find device with async transfer support, prefer primary
                    for (const auto& dev : devices_) {
                        if (dev.hasAsyncTransfer) {
                            assignments[i] = dev.deviceIndex;
                            break;
                        }
                    }
                    break;
            }

            // Explicit device index override
            if (hint.deviceIndex != 0 && hint.deviceIndex < devices_.size()) {
                assignments[i] = hint.deviceIndex;
            }
        }

        // Count statistics
        for (auto assignment : assignments) {
            if (assignment == primaryDevice_) {
                stats_.passesOnPrimary++;
            } else {
                stats_.passesOnSecondary++;
            }
        }

        return assignments;
    }

    auto DeviceAffinityResolver::DetectCrossDeviceTransfers(
        std::span<const uint8_t> deviceAssignments, std::span<const DependencyEdge> edges
    ) const -> std::vector<CrossDeviceTransferEdge> {
        std::vector<CrossDeviceTransferEdge> transfers;

        if (devices_.size() <= 1) {
            return transfers;  // No cross-device transfers in single-device mode
        }

        for (const auto& edge : edges) {
            if (edge.srcPass >= deviceAssignments.size() || edge.dstPass >= deviceAssignments.size()) {
                continue;
            }

            uint8_t srcDevice = deviceAssignments[edge.srcPass];
            uint8_t dstDevice = deviceAssignments[edge.dstPass];

            if (srcDevice != dstDevice) {
                transfers.push_back(
                    CrossDeviceTransferEdge{
                        .srcPassIndex = edge.srcPass,
                        .dstPassIndex = edge.dstPass,
                        .resourceIndex = edge.resourceIndex,
                        .srcDevice = srcDevice,
                        .dstDevice = dstDevice,
                        .estimatedTransferBytes = 0,  // Estimated by caller based on resource size
                    }
                );
                stats_.crossDeviceTransfers++;
            }
        }

        return transfers;
    }

    auto DeviceAffinityResolver::FormatStatus() const -> std::string {
        if (devices_.size() <= 1) {
            return std::format("DeviceAffinityResolver: single-device mode, {} passes on primary", stats_.totalPasses);
        }
        return std::format(
            "DeviceAffinityResolver: {} devices, {} passes (primary={}, secondary={}, {} cross-device transfers, ~{}B)",
            devices_.size(), stats_.totalPasses, stats_.passesOnPrimary, stats_.passesOnSecondary,
            stats_.crossDeviceTransfers, stats_.estimatedTransferBytes
        );
    }

}  // namespace miki::rg
