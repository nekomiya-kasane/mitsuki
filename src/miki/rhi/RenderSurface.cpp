/** @file RenderSurface.cpp
 *  @brief RenderSurface PIMPL implementation.
 *
 *  All DeviceBase calls go through DeviceHandle::Dispatch, which requires
 *  complete backend class definitions — hence the AllBackends.h include.
 *  See specs/02-rhi-design.md SS2.2 for the Dispatch architecture.
 */

#include "miki/rhi/RenderSurface.h"
#include "miki/rhi/backend/AllBackends.h"

#include <algorithm>
#include <array>

namespace miki::rhi {

    // =========================================================================
    // RenderSurface::Impl
    // =========================================================================

    struct RenderSurface::Impl {
        DeviceHandle device;
        NativeWindowHandle nativeWindow;
        SwapchainHandle swapchain;
        RenderSurfaceConfig config;
        Extent2D extent{};
        Format actualFormat = Format::BGRA8_SRGB;
        uint32_t currentImageIndex = 0;
        SubmitSyncInfo syncInfo;

        explicit Impl(DeviceHandle iDevice, NativeWindowHandle iNativeWindow)
            : device(iDevice), nativeWindow(iNativeWindow) {}
    };

    // =========================================================================
    // Config resolution
    // =========================================================================

    /// Resolve RenderSurfaceConfig (intent) -> SwapchainDesc (precise params).
    static auto ResolveSwapchainDesc(
        const RenderSurfaceConfig& iConfig, const NativeWindowHandle& iWindow, uint32_t iWidth, uint32_t iHeight
    ) -> SwapchainDesc {
        SwapchainDesc desc;
        desc.surface = iWindow;
        desc.width = iWidth;
        desc.height = iHeight;
        desc.preferredFormat = iConfig.preferredFormat;
        desc.presentMode = iConfig.presentMode;
        desc.colorSpace = iConfig.colorSpace;

        switch (iConfig.imageCount) {
            case ImageCountHint::Auto: desc.imageCount = (iConfig.presentMode == PresentMode::Mailbox) ? 3 : 2; break;
            case ImageCountHint::Minimal: desc.imageCount = 2; break;
            case ImageCountHint::Triple: desc.imageCount = 3; break;
        }

        // Backend may override format based on color space:
        //   scRGBLinear -> RGBA16_FLOAT (FP16 linear)
        //   HDR10_ST2084 -> RGB10A2_UNORM (PQ 10-bit)
        if (iConfig.colorSpace == SurfaceColorSpace::scRGBLinear) {
            desc.preferredFormat = Format::RGBA16_FLOAT;
        } else if (iConfig.colorSpace == SurfaceColorSpace::HDR10_ST2084) {
            desc.preferredFormat = Format::RGB10A2_UNORM;
        }

        return desc;
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    RenderSurface::~RenderSurface() {
        if (impl_ && impl_->swapchain.IsValid()) {
            impl_->device.Dispatch([&](auto& dev) { dev.DestroySwapchain(impl_->swapchain); });
        }
    }

    RenderSurface::RenderSurface(RenderSurface&&) noexcept = default;
    auto RenderSurface::operator=(RenderSurface&&) noexcept -> RenderSurface& = default;
    RenderSurface::RenderSurface(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto RenderSurface::Create(DeviceHandle iDevice, NativeWindowHandle iNativeWindow)
        -> core::Result<std::unique_ptr<RenderSurface>> {
        if (!iDevice.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        auto impl = std::make_unique<Impl>(iDevice, iNativeWindow);
        return std::unique_ptr<RenderSurface>(new RenderSurface(std::move(impl)));
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    auto RenderSurface::Configure(const RenderSurfaceConfig& iConfig, uint32_t iWidth, uint32_t iHeight)
        -> core::Result<void> {
        if (!impl_) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }

        // Validate against real backend surface capabilities
        auto caps = GetCapabilities();
        auto desc = ResolveSwapchainDesc(iConfig, impl_->nativeWindow, iWidth, iHeight);

        if (!caps.supportedFormats.empty()) {
            if (std::ranges::find(caps.supportedFormats, desc.preferredFormat) == caps.supportedFormats.end()) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
        if (!caps.supportedPresentModes.empty()) {
            if (std::ranges::find(caps.supportedPresentModes, desc.presentMode) == caps.supportedPresentModes.end()) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
        if (!caps.supportedColorSpaces.empty()) {
            if (std::ranges::find(caps.supportedColorSpaces, desc.colorSpace) == caps.supportedColorSpaces.end()) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }

        // Destroy old swapchain if exists
        if (impl_->swapchain.IsValid()) {
            impl_->device.Dispatch([&](auto& dev) { dev.DestroySwapchain(impl_->swapchain); });
            impl_->swapchain = {};
        }

        // Create new swapchain if exists
        auto result = impl_->device.Dispatch([&](auto& dev) { return dev.CreateSwapchain(desc); });
        if (!result) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }

        impl_->swapchain = *result;
        impl_->config = iConfig;
        impl_->extent = {.width = iWidth, .height = iHeight};
        impl_->actualFormat = desc.preferredFormat;
        return {};
    }

    auto RenderSurface::Resize(uint32_t iWidth, uint32_t iHeight) -> core::Result<void> {
        if (!impl_ || !impl_->swapchain.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }
        auto result
            = impl_->device.Dispatch([&](auto& dev) { return dev.ResizeSwapchain(impl_->swapchain, iWidth, iHeight); });
        if (!result) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }
        impl_->extent = {.width = iWidth, .height = iHeight};
        return {};
    }

    auto RenderSurface::Reconfigure(const RenderSurfaceConfig& iConfig) -> core::Result<void> {
        return Configure(iConfig, impl_->extent.width, impl_->extent.height);
    }

    // =========================================================================
    // Sync injection
    // =========================================================================

    auto RenderSurface::SetSubmitSyncInfo(const SubmitSyncInfo& iInfo) -> void {
        assert(impl_ && "RenderSurface used after move");
        impl_->syncInfo = iInfo;
    }

    // =========================================================================
    // Per-frame operations
    // =========================================================================

    auto RenderSurface::AcquireNextImage() -> core::Result<uint32_t> {
        if (!impl_ || !impl_->swapchain.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }
        auto result = impl_->device.Dispatch([&](auto& dev) {
            return dev.AcquireNextImage(
                impl_->swapchain, impl_->syncInfo.imageAvailable, impl_->syncInfo.inFlightFence
            );
        });
        if (!result) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }
        impl_->currentImageIndex = *result;
        return *result;
    }

    auto RenderSurface::Present() -> core::Result<void> {
        if (!impl_ || !impl_->swapchain.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }
        std::array waitSems = {impl_->syncInfo.renderFinished};
        impl_->device.Dispatch([&](auto& dev) {
            dev.Present(impl_->swapchain, std::span<const SemaphoreHandle>{waitSems});
        });
        return {};
    }

    // =========================================================================
    // Queries
    // =========================================================================

    auto RenderSurface::GetConfig() const noexcept -> const RenderSurfaceConfig& {
        return impl_->config;
    }
    auto RenderSurface::GetFormat() const noexcept -> Format {
        return impl_->actualFormat;
    }
    auto RenderSurface::GetExtent() const noexcept -> Extent2D {
        return impl_->extent;
    }
    auto RenderSurface::GetCurrentImageIndex() const noexcept -> uint32_t {
        return impl_->currentImageIndex;
    }

    auto RenderSurface::GetCurrentTexture() const noexcept -> TextureHandle {
        if (!impl_ || !impl_->swapchain.IsValid()) {
            return {};
        }
        return impl_->device.Dispatch([&](auto& dev) {
            return dev.GetSwapchainTexture(impl_->swapchain, impl_->currentImageIndex);
        });
    }

    auto RenderSurface::GetSwapchainHandle() const noexcept -> SwapchainHandle {
        return impl_ ? impl_->swapchain : SwapchainHandle{};
    }

    auto RenderSurface::GetSubmitSyncInfo() const noexcept -> const SubmitSyncInfo& {
        return impl_->syncInfo;
    }

    auto RenderSurface::GetCapabilities() const -> RenderSurfaceCapabilities {
        return impl_->device.Dispatch([&](const auto& dev) { return dev.GetSurfaceCapabilities(impl_->nativeWindow); });
    }

}  // namespace miki::rhi
