/** @file Sync.h
 *  @brief Synchronization primitives: Fence, Semaphore, Barrier descriptors.
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>

#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/Resources.h"

namespace miki::rhi {

    // =========================================================================
    // Semaphore
    // =========================================================================

    struct SemaphoreDesc {
        SemaphoreType type         = SemaphoreType::Binary;
        uint64_t      initialValue = 0;     ///< Timeline only
    };

    struct SemaphoreSubmitInfo {
        SemaphoreHandle semaphore;
        uint64_t        value     = 0;      ///< Timeline value (ignored for Binary)
        PipelineStage   stageMask = PipelineStage::AllCommands;
    };

    // =========================================================================
    // Barrier descriptors (in-command-buffer)
    // =========================================================================

    static constexpr uint64_t kWholeSize = ~uint64_t{0};

    struct BufferBarrierDesc {
        PipelineStage srcStage  = PipelineStage::TopOfPipe;
        PipelineStage dstStage  = PipelineStage::BottomOfPipe;
        AccessFlags   srcAccess = AccessFlags::None;
        AccessFlags   dstAccess = AccessFlags::None;
        uint64_t      offset    = 0;
        uint64_t      size      = kWholeSize;
        QueueType     srcQueue  = QueueType::Graphics;
        QueueType     dstQueue  = QueueType::Graphics;
    };

    struct TextureBarrierDesc {
        PipelineStage           srcStage  = PipelineStage::TopOfPipe;
        PipelineStage           dstStage  = PipelineStage::BottomOfPipe;
        AccessFlags             srcAccess = AccessFlags::None;
        AccessFlags             dstAccess = AccessFlags::None;
        TextureLayout           oldLayout = TextureLayout::Undefined;
        TextureLayout           newLayout = TextureLayout::Undefined;
        TextureSubresourceRange subresource;
        QueueType               srcQueue  = QueueType::Graphics;
        QueueType               dstQueue  = QueueType::Graphics;
    };

    struct PipelineBarrierDesc {
        PipelineStage srcStage  = PipelineStage::TopOfPipe;
        PipelineStage dstStage  = PipelineStage::BottomOfPipe;
        AccessFlags   srcAccess = AccessFlags::None;
        AccessFlags   dstAccess = AccessFlags::None;
    };

    // =========================================================================
    // Submit descriptor
    // =========================================================================

    struct SubmitDesc {
        std::span<const CommandBufferHandle> commandBuffers;
        std::span<const SemaphoreSubmitInfo> waitSemaphores;
        std::span<const SemaphoreSubmitInfo> signalSemaphores;
        FenceHandle                          signalFence;
    };

}  // namespace miki::rhi
