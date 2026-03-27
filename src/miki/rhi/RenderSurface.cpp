/** @brief RenderSurface implementation — Pimpl forwarding + factory dispatch.
 *
 * Impl base class is defined in RenderSurfaceImpl.h (shared internal header).
 * Factory dispatch by BackendType creates the correct backend subclass.
 */

#include "RenderSurfaceImpl.h"

#include "miki/core/ErrorCode.h"
#include "miki/rhi/IDevice.h"

// Backend forward declarations (defined in backend TUs)
#ifdef MIKI_BUILD_VULKAN
[[nodiscard]] auto CreateVulkanRenderSurface(miki::rhi::IDevice& iDevice, const miki::rhi::RenderSurfaceDesc& iDesc)
    -> miki::core::Result<std::unique_ptr<miki::rhi::RenderSurface::Impl>>;
#endif
#ifdef MIKI_BUILD_D3D12
[[nodiscard]] auto CreateD3D12RenderSurface(miki::rhi::IDevice& iDevice, const miki::rhi::RenderSurfaceDesc& iDesc)
    -> miki::core::Result<std::unique_ptr<miki::rhi::RenderSurface::Impl>>;
#endif
#ifdef MIKI_BUILD_WEBGPU
[[nodiscard]] auto CreateWebGpuRenderSurface(miki::rhi::IDevice& iDevice, const miki::rhi::RenderSurfaceDesc& iDesc)
    -> miki::core::Result<std::unique_ptr<miki::rhi::RenderSurface::Impl>>;
#endif
#ifdef MIKI_BUILD_MOCK
[[nodiscard]] auto CreateMockRenderSurface(miki::rhi::IDevice& iDevice, const miki::rhi::RenderSurfaceDesc& iDesc)
    -> miki::core::Result<std::unique_ptr<miki::rhi::RenderSurface::Impl>>;
#endif

namespace miki::rhi {

    // ===========================================================================
    // RenderSurface — Pimpl forwarding
    // ===========================================================================

    RenderSurface::RenderSurface(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    RenderSurface::~RenderSurface() = default;

    RenderSurface::RenderSurface(RenderSurface&& iOther) noexcept = default;
    auto RenderSurface::operator=(RenderSurface&& iOther) noexcept -> RenderSurface& = default;

    auto RenderSurface::Create(IDevice& iDevice, const RenderSurfaceDesc& iDesc)
        -> miki::core::Result<std::unique_ptr<RenderSurface>> {
        [[maybe_unused]] auto backend = iDevice.GetBackendType();
        miki::core::Result<std::unique_ptr<Impl>> implResult = std::unexpected(miki::core::ErrorCode::NotSupported);

#ifdef MIKI_BUILD_VULKAN
        if (backend == BackendType::Vulkan) {
            implResult = CreateVulkanRenderSurface(iDevice, iDesc);
        }
#endif
#ifdef MIKI_BUILD_D3D12
        if (backend == BackendType::D3D12) {
            implResult = CreateD3D12RenderSurface(iDevice, iDesc);
        }
#endif
#ifdef MIKI_BUILD_WEBGPU
        if (backend == BackendType::WebGPU) {
            implResult = CreateWebGpuRenderSurface(iDevice, iDesc);
        }
#endif
#ifdef MIKI_BUILD_MOCK
        if (backend == BackendType::Mock) {
            implResult = CreateMockRenderSurface(iDevice, iDesc);
        }
#endif

        if (!implResult) {
            return std::unexpected(implResult.error());
        }

        return std::unique_ptr<RenderSurface>(new RenderSurface(std::move(*implResult)));
    }

    auto RenderSurface::Configure(const RenderSurfaceConfig& iConfig) -> miki::core::Result<void> {
        if (!impl_) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }
        return impl_->Configure(iConfig);
    }

    auto RenderSurface::Resize(uint32_t iWidth, uint32_t iHeight) -> miki::core::Result<void> {
        if (!impl_) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }
        auto config = impl_->GetConfig();
        config.width = iWidth;
        config.height = iHeight;
        return impl_->Configure(config);
    }

    auto RenderSurface::GetConfig() const noexcept -> RenderSurfaceConfig {
        if (!impl_) {
            return {};
        }
        return impl_->GetConfig();
    }

    auto RenderSurface::GetCapabilities() const -> RenderSurfaceCapabilities {
        if (!impl_) {
            return {};
        }
        return impl_->GetCapabilities();
    }

    auto RenderSurface::AcquireNextImage() -> miki::core::Result<TextureHandle> {
        if (!impl_) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }
        return impl_->AcquireNextImage();
    }

    auto RenderSurface::Present() -> miki::core::Result<void> {
        if (!impl_) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }
        return impl_->Present();
    }

    auto RenderSurface::GetFormat() const noexcept -> Format {
        if (!impl_) {
            return Format::BGRA8_UNORM;
        }
        return impl_->GetFormat();
    }

    auto RenderSurface::GetExtent() const noexcept -> Extent2D {
        if (!impl_) {
            return {};
        }
        return impl_->GetExtent();
    }

    auto RenderSurface::GetCurrentTexture() const noexcept -> TextureHandle {
        if (!impl_) {
            return {};
        }
        return impl_->GetCurrentTexture();
    }

    auto RenderSurface::GetSubmitSyncInfo() const noexcept -> SubmitSyncInfo {
        if (!impl_) {
            return {};
        }
        return impl_->GetSubmitSyncInfo();
    }

    auto RenderSurface::GetSubmitSyncInfo2() const noexcept -> SubmitSyncInfo2 {
        if (!impl_) {
            return {};
        }
        return impl_->GetSubmitSyncInfo2();
    }

    auto RenderSurface::UsesTimelineFramePacing() const noexcept -> bool {
        if (!impl_) {
            return false;
        }
        return impl_->UsesTimelineFramePacing();
    }

    auto RenderSurface::BuildSubmitInfo() const noexcept -> SubmitInfo2 {
        if (!impl_) {
            return {};
        }

        if (impl_->UsesTimelineFramePacing()) {
            auto sync2 = impl_->GetSubmitSyncInfo2();
            return SubmitInfo2{
                .queue = QueueType::Graphics,
                .timelineSignals = sync2.timelineSignals,
                .waitSemaphores = sync2.waitBinarySemaphores,
                .signalSemaphores = sync2.signalBinarySemaphores,
            };
        }

        auto sync = impl_->GetSubmitSyncInfo();
        return SubmitInfo2{
            .queue = QueueType::Graphics,
            .waitSemaphores = sync.waitSemaphores,
            .signalSemaphores = sync.signalSemaphores,
            .signalFence = sync.signalFence,
        };
    }

}  // namespace miki::rhi
