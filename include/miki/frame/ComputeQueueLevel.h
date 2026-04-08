/** @file ComputeQueueLevel.h
 *  @brief 4-level compute queue capability detection and QFOT helpers.
 *
 *  Detects hardware compute queue isolation level at device init.
 *  Used by AsyncTaskManager and FrameManager to choose between dual-queue
 *  priority isolation, single-queue batch splitting, or graphics-only fallback.
 *
 *  See: specs/03-sync.md §5.8.2
 *  Namespace: miki::frame
 */
#pragma once

#include <cstdint>

#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/Sync.h"

namespace miki::frame {

    /// @brief Hardware capability level for compute queue management.
    enum class ComputeQueueLevel : uint8_t {
        A_DualQueuePriority,    ///< 2+ compute queues + VK_EXT_global_priority (physical isolation)
        B_SingleQueuePriority,  ///< 1 compute queue + global priority HIGH
        C_SingleQueueBatch,     ///< 1 compute queue, batch splitting only
        D_GraphicsOnly,         ///< No compute queue (T2/T3/T4)
    };

    /// @brief Detect the highest available compute queue isolation level.
    inline auto DetectComputeQueueLevel(const rhi::GpuCapabilityProfile& iCaps) -> ComputeQueueLevel {
        if (!iCaps.hasAsyncCompute) {
            return ComputeQueueLevel::D_GraphicsOnly;
        }
        if (iCaps.computeQueueFamilyCount >= 2 && iCaps.hasGlobalPriority) {
            return ComputeQueueLevel::A_DualQueuePriority;
        }
        if (iCaps.hasGlobalPriority) {
            return ComputeQueueLevel::B_SingleQueuePriority;
        }
        return ComputeQueueLevel::C_SingleQueueBatch;
    }

    /// @brief Human-readable name for ComputeQueueLevel.
    inline auto ComputeQueueLevelName(ComputeQueueLevel iLevel) -> const char* {
        switch (iLevel) {
            case ComputeQueueLevel::A_DualQueuePriority: return "A: Dual Queue + Priority";
            case ComputeQueueLevel::B_SingleQueuePriority: return "B: Single Queue + Priority";
            case ComputeQueueLevel::C_SingleQueueBatch: return "C: Single Queue + Batch Split";
            case ComputeQueueLevel::D_GraphicsOnly: return "D: Graphics Only";
        }
        return "Unknown";
    }

    // =========================================================================
    // QFOT (Queue Family Ownership Transfer) helpers — specs/03-sync.md §5.7.3
    // =========================================================================

    /// @brief Build a QFOT release barrier (srcQueue releases ownership).
    /// Vulkan-only; D3D12 returns identity barrier (no QFOT needed).
    inline auto MakeQFOTRelease(rhi::QueueType iSrcQueue, rhi::QueueType iDstQueue, rhi::AccessFlags iSrcAccess)
        -> rhi::BufferBarrierDesc {
        return {
            .srcStage = rhi::PipelineStage::AllCommands,
            .dstStage = rhi::PipelineStage::AllCommands,
            .srcAccess = iSrcAccess,
            .dstAccess = rhi::AccessFlags::None,
            .srcQueue = iSrcQueue,
            .dstQueue = iDstQueue,
        };
    }

    /// @brief Build a QFOT acquire barrier (dstQueue acquires ownership).
    inline auto MakeQFOTAcquire(rhi::QueueType iSrcQueue, rhi::QueueType iDstQueue, rhi::AccessFlags iDstAccess)
        -> rhi::BufferBarrierDesc {
        return {
            .srcStage = rhi::PipelineStage::AllCommands,
            .dstStage = rhi::PipelineStage::AllCommands,
            .srcAccess = rhi::AccessFlags::None,
            .dstAccess = iDstAccess,
            .srcQueue = iSrcQueue,
            .dstQueue = iDstQueue,
        };
    }

}  // namespace miki::frame
