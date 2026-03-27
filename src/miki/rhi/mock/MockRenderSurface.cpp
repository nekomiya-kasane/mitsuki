/** @brief MockRenderSurface implementation — CPU-only test mock. */

#include "MockRenderSurface.h"

#include "miki/core/ErrorCode.h"

namespace miki::rhi::mock {

    auto MockRenderSurface::Create(IDevice& /*iDevice*/, const RenderSurfaceDesc& iDesc)
        -> miki::core::Result<std::unique_ptr<RenderSurface::Impl>> {
        auto impl = std::unique_ptr<MockRenderSurface>(new MockRenderSurface());
        impl->config_ = iDesc.initialConfig;

        if (impl->config_.width == 0 || impl->config_.height == 0) {
            impl->config_.width = 800;
            impl->config_.height = 600;
        }

        // Create a fake texture handle for mock acquire
        impl->currentTexture_ = TextureHandle::Create(1, 0, 0);

        return std::unique_ptr<RenderSurface::Impl>(std::move(impl));
    }

    auto MockRenderSurface::Configure(const RenderSurfaceConfig& iConfig) -> miki::core::Result<void> {
        if (iConfig.width == 0 || iConfig.height == 0) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }
        config_ = iConfig;
        return {};
    }

    auto MockRenderSurface::GetConfig() const noexcept -> RenderSurfaceConfig {
        return config_;
    }

    auto MockRenderSurface::GetCapabilities() const -> RenderSurfaceCapabilities {
        return RenderSurfaceCapabilities{
            .supportedFormats = {Format::BGRA8_UNORM, Format::RGBA8_UNORM, Format::RGBA8_SRGB},
            .supportedPresentModes = {PresentMode::Fifo, PresentMode::Mailbox, PresentMode::Immediate},
            .minExtent = {1, 1},
            .maxExtent = {16384, 16384},
            .minImageCount = 2,
            .maxImageCount = 8,
        };
    }

    auto MockRenderSurface::AcquireNextImage() -> miki::core::Result<TextureHandle> {
        if (pendingAcquireError_) {
            auto err = *pendingAcquireError_;
            pendingAcquireError_.reset();
            return std::unexpected(err);
        }

        ++acquireCount_;
        nextImageIndex_ = (nextImageIndex_ + 1) % config_.imageCount;
        currentTexture_ = TextureHandle::Create(1, nextImageIndex_, 0);
        return currentTexture_;
    }

    auto MockRenderSurface::Present() -> miki::core::Result<void> {
        ++presentCount_;
        return {};
    }

    auto MockRenderSurface::GetFormat() const noexcept -> Format {
        return config_.format;
    }

    auto MockRenderSurface::GetExtent() const noexcept -> Extent2D {
        return {config_.width, config_.height};
    }

    auto MockRenderSurface::GetCurrentTexture() const noexcept -> TextureHandle {
        return currentTexture_;
    }

    auto MockRenderSurface::GetSubmitSyncInfo() const noexcept -> SubmitSyncInfo {
        return {};
    }

    auto MockRenderSurface::InjectAcquireError(miki::core::ErrorCode iError) -> void {
        pendingAcquireError_ = iError;
    }

}  // namespace miki::rhi::mock

// Dispatch function for RenderSurface::Create factory
auto CreateMockRenderSurface(miki::rhi::IDevice& iDevice, const miki::rhi::RenderSurfaceDesc& iDesc)
    -> miki::core::Result<std::unique_ptr<miki::rhi::RenderSurface::Impl>> {
    return miki::rhi::mock::MockRenderSurface::Create(iDevice, iDesc);
}
