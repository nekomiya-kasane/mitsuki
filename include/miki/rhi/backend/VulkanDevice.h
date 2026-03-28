/** @file VulkanDevice.h
 *  @brief Vulkan 1.4 (Tier 1) backend device.
 *
 *  This header is conditionally included by AllBackends.h only when
 *  MIKI_BUILD_VULKAN=1. It freely includes volk.h and Vulkan types.
 *  No PIMPL — all members are direct for zero-overhead Dispatch inlining.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/backend/BackendStub.h"

#include <volk.h>

#include <unordered_map>
#include <vector>

namespace miki::rhi {

    struct VulkanQueueFamilies {
        uint32_t graphics = UINT32_MAX;
        uint32_t compute = UINT32_MAX;
        uint32_t transfer = UINT32_MAX;
        uint32_t present = UINT32_MAX;
    };

    struct VulkanSwapchainData {
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        std::vector<VkImage> images;
        std::vector<TextureHandle> textureHandles;
        VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;
        VkExtent2D extent{};
    };

    struct VulkanDeviceDesc {
        bool enableValidation = true;
        bool enableDebugMessenger = true;
        const char* appName = "miki";
        uint32_t appVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    };

    class VulkanDevice : public DeviceBase<VulkanDevice> {
       public:
        VulkanDevice() = default;
        ~VulkanDevice();

        VulkanDevice(const VulkanDevice&) = delete;
        auto operator=(const VulkanDevice&) -> VulkanDevice& = delete;
        VulkanDevice(VulkanDevice&&) = delete;
        auto operator=(VulkanDevice&&) -> VulkanDevice& = delete;

        [[nodiscard]] auto Init(const VulkanDeviceDesc& desc = {}) -> RhiResult<void>;

        [[nodiscard]] auto GetVkInstance() const noexcept -> VkInstance { return instance_; }
        [[nodiscard]] auto GetVkPhysicalDevice() const noexcept -> VkPhysicalDevice { return physicalDevice_; }
        [[nodiscard]] auto GetVkDevice() const noexcept -> VkDevice { return device_; }
        [[nodiscard]] auto GetGraphicsQueue() const noexcept -> VkQueue { return graphicsQueue_; }
        [[nodiscard]] auto GetPresentQueue() const noexcept -> VkQueue { return presentQueue_; }
        [[nodiscard]] auto GetQueueFamilies() const noexcept -> const VulkanQueueFamilies& { return queueFamilies_; }

        // -- Swapchain (VulkanSwapchain.cpp) --
        auto CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle>;
        void DestroySwapchainImpl(SwapchainHandle h);
        auto ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void>;
        auto AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle fence) -> RhiResult<uint32_t>;
        auto GetSwapchainTextureImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle;
        void PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores);

        // -- Sync (VulkanDevice.cpp) --
        auto CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle>;
        void DestroyFenceImpl(FenceHandle h);
        void WaitFenceImpl(FenceHandle h, uint64_t timeout);
        void ResetFenceImpl(FenceHandle h);
        auto GetFenceStatusImpl(FenceHandle h) -> bool;

        auto CreateSemaphoreImpl(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle>;
        void DestroySemaphoreImpl(SemaphoreHandle h);
        void SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value);
        void WaitSemaphoreImpl(SemaphoreHandle h, uint64_t value, uint64_t timeout);
        auto GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t;

        void WaitIdleImpl();
        void SubmitImpl(QueueType queue, const SubmitDesc& desc);

        // -- Capability --
        auto GetBackendTypeImpl() const -> BackendType { return BackendType::Vulkan14; }
        auto GetCapabilitiesImpl() const -> const GpuCapabilityProfile& { return capabilities_; }

        // -- Stub methods for unimplemented subsystems --
        MIKI_DEVICE_STUB_RESOURCE_IMPL

       private:
        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue graphicsQueue_ = VK_NULL_HANDLE;
        VkQueue presentQueue_ = VK_NULL_HANDLE;
        VkQueue computeQueue_ = VK_NULL_HANDLE;
        VkQueue transferQueue_ = VK_NULL_HANDLE;
        VulkanQueueFamilies queueFamilies_;
        GpuCapabilityProfile capabilities_;

        uint64_t nextHandleId_ = 1;
        std::unordered_map<uint64_t, VulkanSwapchainData> swapchains_;
        std::unordered_map<uint64_t, VkFence> fences_;
        std::unordered_map<uint64_t, VkSemaphore> semaphores_;

        auto AllocHandle() -> uint64_t { return nextHandleId_++; }

        auto CreateInstance(const VulkanDeviceDesc& desc) -> RhiResult<void>;
        auto SelectPhysicalDevice() -> RhiResult<void>;
        auto CreateLogicalDevice() -> RhiResult<void>;
    };

}  // namespace miki::rhi
