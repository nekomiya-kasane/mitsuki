/** @file FrameContext.h
 *  @brief Per-frame context returned by FrameManager::BeginFrame.
 *
 *  Contains frame index (for resource rotation), monotonic frame number,
 *  swapchain image handle, dimensions, and sync state for EndFrame.
 *
 *  See: specs/03-sync.md SS4.2
 *  Namespace: miki::frame
 */
#pragma once

#include <cstdint>

#include "miki/rhi/Handle.h"

namespace miki::frame {

    /// @brief Per-frame context returned by FrameManager::BeginFrame.
    struct FrameContext {
        uint32_t frameIndex = 0;            ///< [0, framesInFlight) for resource ring rotation
        uint64_t frameNumber = 0;           ///< Monotonic, never wraps within process lifetime
        rhi::TextureHandle swapchainImage;  ///< Current swapchain image (invalid if offscreen)
        uint32_t width = 0;                 ///< Current framebuffer width
        uint32_t height = 0;                ///< Current framebuffer height

        // Sync state — opaque to caller, consumed by EndFrame internally.
        uint64_t graphicsTimelineTarget = 0;  ///< Timeline value to signal on graphics queue
        uint64_t transferWaitValue = 0;       ///< Transfer timeline to wait (0 = none)
        uint64_t computeWaitValue = 0;        ///< Compute timeline to wait (0 = none)
    };

}  // namespace miki::frame
