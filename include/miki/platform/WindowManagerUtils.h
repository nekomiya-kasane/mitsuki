/** @file WindowManagerUtils.h
 *  @brief Application-level helpers that orchestrate WindowManager + SurfaceManager.
 *
 *  These are NOT methods on either manager -- they bridge the two concerns
 *  while preserving the three-concern separation (spec SS6.3).
 *
 *  Namespace: miki::platform
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/platform/WindowManager.h"
#include "miki/rhi/SurfaceManager.h"

namespace miki::platform {

    /** @brief Cascade-destroy a window and all children with GPU safety.
     *
     *  Orchestrates the two managers in the correct sequence:
     *    1. GetDescendantsPostOrder(parent) + self  (leaves first)
     *    2. DetachSurfaces(postOrderList)            (per-surface timeline wait, NO WaitIdle)
     *    3. DestroyWindow(parent)                    (OS windows destroyed post-order)
     *
     *  Other windows continue rendering uninterrupted during this operation.
     *
     *  @param iWm     WindowManager owning the window tree.
     *  @param iSm     SurfaceManager owning GPU surfaces.
     *  @param iHandle Root of the subtree to destroy.
     *  @return void on success, or first error encountered.
     */
    [[nodiscard]] inline auto DestroyWindowCascade(WindowManager& iWm, rhi::SurfaceManager& iSm,
        WindowHandle iHandle) -> miki::core::Result<void> {
        // 1. Get all descendants + self in post-order (leaves first)
        auto victims = iWm.GetDescendantsPostOrder(iHandle);
        victims.push_back(iHandle);

        // 2. Detach all GPU surfaces (per-surface timeline wait, NOT WaitIdle)
        auto detachResult = iSm.DetachSurfaces(victims);
        if (!detachResult) return std::unexpected(detachResult.error());

        // 3. Destroy OS windows (post-order cascade)
        auto destroyResult = iWm.DestroyWindow(iHandle);
        if (!destroyResult) return std::unexpected(destroyResult.error());

        return {};
    }

}  // namespace miki::platform
