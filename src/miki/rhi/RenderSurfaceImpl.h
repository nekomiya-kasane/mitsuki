/** @brief RenderSurface::Impl — internal abstract base for backend polymorphism.
 *
 * This header is INTERNAL (not under include/). Backend subclasses include it
 * to inherit from Impl. The public header (RenderSurface.h) only forward-declares
 * Impl as an incomplete type (Pimpl pattern).
 *
 * Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/RenderSurface.h"

namespace miki::rhi {

    struct RenderSurface::Impl {
        virtual ~Impl() = default;

        Impl() = default;
        Impl(const Impl&) = delete;
        auto operator=(const Impl&) -> Impl& = delete;
        Impl(Impl&&) = default;
        auto operator=(Impl&&) -> Impl& = default;

        [[nodiscard]] virtual auto Configure(const RenderSurfaceConfig& iConfig) -> miki::core::Result<void> = 0;

        [[nodiscard]] virtual auto GetConfig() const noexcept -> RenderSurfaceConfig = 0;

        [[nodiscard]] virtual auto GetCapabilities() const -> RenderSurfaceCapabilities = 0;

        [[nodiscard]] virtual auto AcquireNextImage() -> miki::core::Result<TextureHandle> = 0;

        [[nodiscard]] virtual auto Present() -> miki::core::Result<void> = 0;

        [[nodiscard]] virtual auto GetFormat() const noexcept -> Format = 0;
        [[nodiscard]] virtual auto GetExtent() const noexcept -> Extent2D = 0;
        [[nodiscard]] virtual auto GetCurrentTexture() const noexcept -> TextureHandle = 0;

        /** @brief Compat-tier sync info (binary semaphores + fence). */
        [[nodiscard]] virtual auto GetSubmitSyncInfo() const noexcept -> SubmitSyncInfo = 0;

        /** @brief Tier1 sync info (timeline + binary semaphores, no fence).
         *  Default returns empty — Compat subclasses need not override.
         */
        [[nodiscard]] virtual auto GetSubmitSyncInfo2() const noexcept -> SubmitSyncInfo2 { return {}; }

        /** @brief Whether this surface uses timeline semaphore frame pacing. */
        [[nodiscard]] virtual auto UsesTimelineFramePacing() const noexcept -> bool { return false; }
    };

}  // namespace miki::rhi
