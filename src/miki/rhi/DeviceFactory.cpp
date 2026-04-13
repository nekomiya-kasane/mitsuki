/** @file DeviceFactory.cpp
 *  @brief OwnedDevice + CreateDevice implementation.
 *
 *  Includes AllBackends.h to get complete types for all enabled backends.
 *  The Impl stores the concrete device in a tagged union (one unique_ptr per backend).
 */

#include "miki/rhi/DeviceFactory.h"

#include "miki/rhi/backend/AllBackends.h"

namespace miki::rhi {

    // =========================================================================
    // OwnedDevice::Impl — stores exactly one backend device
    // =========================================================================

    struct OwnedDevice::Impl {
        BackendType backend = BackendType::Mock;
        DeviceHandle handle;

#if MIKI_BUILD_VULKAN
        std::unique_ptr<VulkanDevice> vulkan;
#endif
#if MIKI_BUILD_D3D12
        std::unique_ptr<D3D12Device> d3d12;
#endif
#if MIKI_BUILD_OPENGL
        std::unique_ptr<OpenGLDevice> opengl;
#endif
#if MIKI_BUILD_WEBGPU
        std::unique_ptr<WebGPUDevice> webgpu;
#endif
        std::unique_ptr<MockDevice> mock;

        void WaitIdle() {
            if (handle.IsValid()) {
                handle.Dispatch([](auto& dev) { dev.WaitIdle(); });
            }
        }
    };

    // =========================================================================
    // OwnedDevice lifecycle
    // =========================================================================

    OwnedDevice::OwnedDevice(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    OwnedDevice::~OwnedDevice() {
        if (impl_) {
            impl_->WaitIdle();
        }
        // unique_ptr members destroy the concrete device automatically
    }

    OwnedDevice::OwnedDevice(OwnedDevice&&) noexcept = default;
    auto OwnedDevice::operator=(OwnedDevice&&) noexcept -> OwnedDevice& = default;

    auto OwnedDevice::GetHandle() const noexcept -> DeviceHandle {
        return impl_ ? impl_->handle : DeviceHandle{};
    }

    auto OwnedDevice::IsValid() const noexcept -> bool {
        return impl_ && impl_->handle.IsValid();
    }

    auto OwnedDevice::GetBackendType() const noexcept -> BackendType {
        return impl_ ? impl_->backend : BackendType::Mock;
    }

    // =========================================================================
    // CreateDevice — the unified factory function
    // =========================================================================

    auto CreateDevice(const DeviceDesc& iDesc) -> core::Result<OwnedDevice> {
        auto impl = std::make_unique<OwnedDevice::Impl>();
        impl->backend = iDesc.backend;

        switch (iDesc.backend) {
#if MIKI_BUILD_VULKAN
            case BackendType::Vulkan14:
            case BackendType::VulkanCompat: {
                VulkanDeviceDesc vkDesc{
                    .tier = iDesc.backend,
                    .enableValidation = iDesc.enableValidation,
                    .enableDebugMessenger = iDesc.enableValidation,
                    .appName = iDesc.appName,
                };
                impl->vulkan = std::make_unique<VulkanDevice>();
                if (auto r = impl->vulkan->Init(vkDesc); !r) {
                    return std::unexpected(core::ErrorCode::InvalidState);
                }
                impl->handle = DeviceHandle(impl->vulkan.get(), iDesc.backend);
                break;
            }
#endif
#if MIKI_BUILD_D3D12
            case BackendType::D3D12: {
                D3D12DeviceDesc d3dDesc{
                    .enableValidation = iDesc.enableValidation,
                    .enableGpuCapture = iDesc.enableGpuCapture,
                    .adapterIndex = iDesc.adapterIndex,
                };
                impl->d3d12 = std::make_unique<D3D12Device>();
                if (auto r = impl->d3d12->Init(d3dDesc); !r) {
                    return std::unexpected(core::ErrorCode::InvalidState);
                }
                impl->handle = DeviceHandle(impl->d3d12.get(), BackendType::D3D12);
                break;
            }
#endif
#if MIKI_BUILD_OPENGL
            case BackendType::OpenGL43: {
                OpenGLDeviceDesc glDesc{
                    .enableValidation = iDesc.enableValidation,
                    .windowBackend = static_cast<platform::IWindowBackend*>(iDesc.windowBackend),
                    .nativeToken = iDesc.nativeToken,
                };
                impl->opengl = std::make_unique<OpenGLDevice>();
                if (auto r = impl->opengl->Init(glDesc); !r) {
                    return std::unexpected(core::ErrorCode::InvalidState);
                }
                impl->handle = DeviceHandle(impl->opengl.get(), BackendType::OpenGL43);
                break;
            }
#endif
#if MIKI_BUILD_WEBGPU
            case BackendType::WebGPU: {
                WebGPUDeviceDesc wgpuDesc{
                    .enableValidation = iDesc.enableValidation,
                };
                impl->webgpu = std::make_unique<WebGPUDevice>();
                if (auto r = impl->webgpu->Init(wgpuDesc); !r) {
                    return std::unexpected(core::ErrorCode::InvalidState);
                }
                impl->handle = DeviceHandle(impl->webgpu.get(), BackendType::WebGPU);
                break;
            }
#endif
            case BackendType::Mock: {
                impl->mock = std::make_unique<MockDevice>();
                if (auto r = impl->mock->Init(); !r) {
                    return std::unexpected(core::ErrorCode::InvalidState);
                }
                impl->handle = DeviceHandle(impl->mock.get(), BackendType::Mock);
                break;
            }
            default: return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        return OwnedDevice(std::move(impl));
    }

    // =========================================================================
    // DeviceHandle::GetDeviceName — defined here because all backend types are complete
    // =========================================================================

    auto DeviceHandle::GetDeviceName() const noexcept -> std::string_view {
        return Dispatch([](auto& dev) -> std::string_view { return dev.GetCapabilities().deviceName; });
    }

    // DeviceHandle::GetSyncScheduler — defined here because all backend types are complete
    auto DeviceHandle::GetSyncScheduler() -> frame::SyncScheduler& {
        return Dispatch([](auto& dev) -> frame::SyncScheduler& { return dev.GetSyncScheduler(); });
    }

}  // namespace miki::rhi
