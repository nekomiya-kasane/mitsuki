// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Parameterized test fixture for RHI tests.
// Creates a real Device for each enabled backend (Vulkan, D3D12, OpenGL, WebGPU).
// Each backend has its own Init() method with backend-specific desc structs.

#pragma once

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "miki/rhi/Device.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/RhiTypes.h"

#include "miki/rhi/backend/AllBackends.h"

namespace miki::rhi::test {

    // =========================================================================
    // Backend discovery — returns all backends enabled at compile time
    // =========================================================================

    inline auto GetAvailableBackends() -> std::vector<BackendType> {
        std::vector<BackendType> backends;
#if MIKI_BUILD_VULKAN
        backends.push_back(BackendType::Vulkan14);
#endif
#if MIKI_BUILD_D3D12
        backends.push_back(BackendType::D3D12);
#endif
#if MIKI_BUILD_OPENGL
        backends.push_back(BackendType::OpenGL43);
#endif
#if MIKI_BUILD_WEBGPU
        backends.push_back(BackendType::WebGPU);
#endif
        backends.push_back(BackendType::Mock);
        return backends;
    }

    inline auto BackendName(const ::testing::TestParamInfo<BackendType>& info) -> std::string {
        switch (info.param) {
            case BackendType::Vulkan14: return "Vulkan14";
            case BackendType::D3D12: return "D3D12";
            case BackendType::VulkanCompat: return "VulkanCompat";
            case BackendType::WebGPU: return "WebGPU";
            case BackendType::OpenGL43: return "OpenGL43";
            case BackendType::Mock: return "Mock";
        }
        return "Unknown";
    }

    // =========================================================================
    // DeviceOwner — RAII wrapper that creates and destroys the backend device
    // =========================================================================

    struct DeviceOwner {
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

        static auto Create(BackendType backend) -> std::pair<std::unique_ptr<DeviceOwner>, RhiError> {
            auto owner = std::make_unique<DeviceOwner>();
            switch (backend) {
#if MIKI_BUILD_VULKAN
                case BackendType::Vulkan14: {
                    owner->vulkan = std::make_unique<VulkanDevice>();
                    VulkanDeviceDesc desc{};
                    desc.enableValidation = true;
                    auto r = owner->vulkan->Init(desc);
                    if (!r.has_value()) {
                        return {nullptr, r.error()};
                    }
                    owner->handle = DeviceHandle(owner->vulkan.get(), BackendType::Vulkan14);
                    return {std::move(owner), RhiError{}};
                }
#endif
#if MIKI_BUILD_D3D12
                case BackendType::D3D12: {
                    owner->d3d12 = std::make_unique<D3D12Device>();
                    D3D12DeviceDesc desc{};
                    desc.enableValidation = true;
                    auto r = owner->d3d12->Init(desc);
                    if (!r.has_value()) {
                        return {nullptr, r.error()};
                    }
                    owner->handle = DeviceHandle(owner->d3d12.get(), BackendType::D3D12);
                    return {std::move(owner), RhiError{}};
                }
#endif
#if MIKI_BUILD_OPENGL
                case BackendType::OpenGL43: {
                    owner->opengl = std::make_unique<OpenGLDevice>();
                    auto r = owner->opengl->Init();
                    if (!r.has_value()) {
                        return {nullptr, r.error()};
                    }
                    owner->handle = DeviceHandle(owner->opengl.get(), BackendType::OpenGL43);
                    return {std::move(owner), RhiError{}};
                }
#endif
#if MIKI_BUILD_WEBGPU
                case BackendType::WebGPU: {
                    owner->webgpu = std::make_unique<WebGPUDevice>();
                    auto r = owner->webgpu->Init();
                    if (!r.has_value()) {
                        return {nullptr, r.error()};
                    }
                    owner->handle = DeviceHandle(owner->webgpu.get(), BackendType::WebGPU);
                    return {std::move(owner), RhiError{}};
                }
#endif
                case BackendType::Mock: {
                    owner->mock = std::make_unique<MockDevice>();
                    auto r = owner->mock->Init();
                    if (!r.has_value()) {
                        return {nullptr, r.error()};
                    }
                    owner->handle = DeviceHandle(owner->mock.get(), BackendType::Mock);
                    return {std::move(owner), RhiError{}};
                }
                default: return {nullptr, RhiError::NotImplemented};
            }
        }
    };

    // =========================================================================
    // RhiTest — parameterized fixture creating a real device per backend
    // =========================================================================

    class RhiTest : public ::testing::TestWithParam<BackendType> {
       protected:
        void SetUp() override {
            auto [owner, err] = DeviceOwner::Create(GetParam());
            if (!owner) {
                GTEST_SKIP() << "Cannot create device for backend " << static_cast<int>(GetParam())
                             << " (error: " << static_cast<int>(err) << ")";
                return;
            }
            owner_ = std::move(owner);
        }

        void TearDown() override { owner_.reset(); }

        [[nodiscard]] auto Dev() -> DeviceHandle& { return owner_->handle; }

        [[nodiscard]] auto Caps() const -> const GpuCapabilityProfile& {
            return owner_->handle.Dispatch([](const auto& dev) -> const GpuCapabilityProfile& {
                return dev.GetCapabilities();
            });
        }

        [[nodiscard]] auto IsTier1() const -> bool { return Caps().IsTier1(); }

        void RequireFeature(DeviceFeature feature) {
            if (!Caps().enabledFeatures.Has(feature)) {
                GTEST_SKIP() << "Feature " << static_cast<int>(feature) << " not supported";
            }
        }

        void RequireTier(CapabilityTier minTier) {
            if (!Caps().SupportsTier(minTier)) {
                GTEST_SKIP() << "Tier " << static_cast<int>(minTier) << " required";
            }
        }

       private:
        std::unique_ptr<DeviceOwner> owner_;
    };

    // =========================================================================
    // Tier1 specialization — only Vulkan14 + D3D12
    // =========================================================================

    inline auto GetTier1Backends() -> std::vector<BackendType> {
        std::vector<BackendType> backends;
#if MIKI_BUILD_VULKAN
        backends.push_back(BackendType::Vulkan14);
#endif
#if MIKI_BUILD_D3D12
        backends.push_back(BackendType::D3D12);
#endif
        return backends;
    }

}  // namespace miki::rhi::test
