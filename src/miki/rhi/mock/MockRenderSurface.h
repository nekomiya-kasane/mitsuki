/** @brief MockRenderSurface — CPU-only test mock for RenderSurface.
 *
 * Simulates acquire/present cycle, configurable error injection,
 * and mock capabilities. No GPU required.
 *
 * Namespace: miki::rhi::mock
 */
#pragma once

#include "RenderSurfaceImpl.h"

#include <optional>

namespace miki::rhi::mock {

    class MockDevice;

    class MockRenderSurface final : public RenderSurface::Impl {
       public:
        ~MockRenderSurface() override = default;

        [[nodiscard]] static auto Create(IDevice& iDevice, const RenderSurfaceDesc& iDesc)
            -> miki::core::Result<std::unique_ptr<RenderSurface::Impl>>;

        [[nodiscard]] auto Configure(const RenderSurfaceConfig& iConfig) -> miki::core::Result<void> override;

        [[nodiscard]] auto GetConfig() const noexcept -> RenderSurfaceConfig override;
        [[nodiscard]] auto GetCapabilities() const -> RenderSurfaceCapabilities override;

        [[nodiscard]] auto AcquireNextImage() -> miki::core::Result<TextureHandle> override;

        [[nodiscard]] auto Present() -> miki::core::Result<void> override;

        [[nodiscard]] auto GetFormat() const noexcept -> Format override;
        [[nodiscard]] auto GetExtent() const noexcept -> Extent2D override;
        [[nodiscard]] auto GetCurrentTexture() const noexcept -> TextureHandle override;
        [[nodiscard]] auto GetSubmitSyncInfo() const noexcept -> SubmitSyncInfo override;

        /** @brief Inject an error for the next AcquireNextImage call. */
        auto InjectAcquireError(miki::core::ErrorCode iError) -> void;

        /** @brief Get acquire/present call counts for verification. */
        [[nodiscard]] auto GetAcquireCount() const noexcept -> uint32_t { return acquireCount_; }
        [[nodiscard]] auto GetPresentCount() const noexcept -> uint32_t { return presentCount_; }

       private:
        MockRenderSurface() = default;

        RenderSurfaceConfig config_ = {};
        TextureHandle currentTexture_ = {};
        uint32_t acquireCount_ = 0;
        uint32_t presentCount_ = 0;
        uint32_t nextImageIndex_ = 0;
        std::optional<miki::core::ErrorCode> pendingAcquireError_;
    };

}  // namespace miki::rhi::mock
