/** @file DeviceFactory.h
 *  @brief Unified device creation — OwnedDevice + CreateDevice factory.
 *
 *  OwnedDevice owns the concrete backend device via PIMPL. It provides
 *  a non-owning DeviceHandle view for use in RenderSurface, FrameManager, etc.
 *  CreateDevice() replaces per-demo switch-case device construction.
 *
 *  See: specs/02-progress.md "H1+H2+M2: OwnedDevice + DeviceFactory"
 *  Namespace: miki::rhi
 */
#pragma once

#include <memory>

#include "miki/core/Result.h"
#include "miki/rhi/Device.h"

namespace miki::rhi {

    /** @brief RAII owner of a concrete backend device.
     *
     *  - Destructor calls WaitIdle() then destroys the backend device.
     *  - Move-only. Non-copyable.
     *  - GetHandle() returns a non-owning DeviceHandle for passing to
     *    SurfaceManager, FrameManager, RenderSurface, IPipelineFactory, etc.
     */
    class OwnedDevice {
       public:
        ~OwnedDevice();

        OwnedDevice(const OwnedDevice&) = delete;
        auto operator=(const OwnedDevice&) -> OwnedDevice& = delete;
        OwnedDevice(OwnedDevice&&) noexcept;
        auto operator=(OwnedDevice&&) noexcept -> OwnedDevice&;

        /** @brief Get a non-owning handle suitable for Dispatch. */
        [[nodiscard]] auto GetHandle() const noexcept -> DeviceHandle;

        /** @brief Check if this device was successfully initialized. */
        [[nodiscard]] auto IsValid() const noexcept -> bool;

        /** @brief Get the backend type of the owned device. */
        [[nodiscard]] auto GetBackendType() const noexcept -> BackendType;

       private:
        friend auto CreateDevice(const DeviceDesc& iDesc) -> core::Result<OwnedDevice>;
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit OwnedDevice(std::unique_ptr<Impl> iImpl);
    };

    /** @brief Create a device from a unified DeviceDesc.
     *
     *  Selects the backend via DeviceDesc::backend, constructs and initializes
     *  the concrete device, and returns an OwnedDevice that manages its lifetime.
     *
     *  @param iDesc Device creation parameters.
     *  @return OwnedDevice on success, ErrorCode on failure.
     */
    [[nodiscard]] auto CreateDevice(const DeviceDesc& iDesc) -> core::Result<OwnedDevice>;

}  // namespace miki::rhi
