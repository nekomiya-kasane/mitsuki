/** @file VulkanDevice.cpp
 *  @brief Vulkan 1.4 backend — instance, device, queue init + sync primitives.
 */

#include "miki/rhi/backend/VulkanDevice.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace miki::rhi {

    // =========================================================================
    // Debug messenger callback
    // =========================================================================

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT /*type*/,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* /*userData*/
    ) {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            std::cerr << "[Vulkan] " << callbackData->pMessage << "\n";
        }
        return VK_FALSE;
    }

    // =========================================================================
    // Instance creation
    // =========================================================================

    auto VulkanDevice::CreateInstance(const VulkanDeviceDesc& desc) -> RhiResult<void> {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = desc.appName;
        appInfo.applicationVersion = desc.appVersion;
        appInfo.pEngineName = "miki";
        appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        // Required extensions
        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
            VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
            VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_METAL_EXT
            VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#endif
        };

        std::vector<const char*> layers;
        if (desc.enableValidation) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }
        if (desc.enableDebugMessenger) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        createInfo.ppEnabledLayerNames = layers.data();

        VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
        if (result != VK_SUCCESS) {
            return std::unexpected(RhiError::DeviceLost);
        }

        volkLoadInstance(instance_);

        // Setup debug messenger
        if (desc.enableDebugMessenger) {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
            messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messengerInfo.messageSeverity
                = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                                        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messengerInfo.pfnUserCallback = DebugCallback;

            if (vkCreateDebugUtilsMessengerEXT) {
                vkCreateDebugUtilsMessengerEXT(instance_, &messengerInfo, nullptr, &debugMessenger_);
            }
        }

        return {};
    }

    // =========================================================================
    // Physical device selection
    // =========================================================================

    auto VulkanDevice::SelectPhysicalDevice() -> RhiResult<void> {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (deviceCount == 0) {
            return std::unexpected(RhiError::DeviceLost);
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

        // Prefer discrete GPU, then integrated
        VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
        int bestScore = -1;

        for (auto& dev : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);

            int score = 0;
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                score = 1000;
            } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                score = 100;
            }

            // Check Vulkan 1.4 support
            if (VK_API_VERSION_MAJOR(props.apiVersion) >= 1 && VK_API_VERSION_MINOR(props.apiVersion) >= 4) {
                score += 500;
            }

            if (score > bestScore) {
                bestScore = score;
                bestDevice = dev;
            }
        }

        if (bestDevice == VK_NULL_HANDLE) {
            return std::unexpected(RhiError::DeviceLost);
        }
        physicalDevice_ = bestDevice;

        // Find queue families
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            auto flags = queueFamilies[i].queueFlags;

            if ((flags & VK_QUEUE_GRAPHICS_BIT) && queueFamilies_.graphics == UINT32_MAX) {
                queueFamilies_.graphics = i;
                queueFamilies_.present = i;  // Graphics queue typically supports present
            }
            if ((flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT)
                && queueFamilies_.compute == UINT32_MAX) {
                queueFamilies_.compute = i;
            }
            if ((flags & VK_QUEUE_TRANSFER_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && !(flags & VK_QUEUE_COMPUTE_BIT)
                && queueFamilies_.transfer == UINT32_MAX) {
                queueFamilies_.transfer = i;
            }
        }

        // Fallback: compute/transfer to graphics if not found
        if (queueFamilies_.compute == UINT32_MAX) {
            queueFamilies_.compute = queueFamilies_.graphics;
        }
        if (queueFamilies_.transfer == UINT32_MAX) {
            queueFamilies_.transfer = queueFamilies_.graphics;
        }

        if (queueFamilies_.graphics == UINT32_MAX) {
            return std::unexpected(RhiError::DeviceLost);
        }

        return {};
    }

    // =========================================================================
    // Logical device creation
    // =========================================================================

    auto VulkanDevice::CreateLogicalDevice() -> RhiResult<void> {
        // Collect unique queue family indices
        std::vector<uint32_t> uniqueFamilies = {queueFamilies_.graphics};
        auto addUnique = [&](uint32_t idx) {
            if (idx != UINT32_MAX && std::ranges::find(uniqueFamilies, idx) == uniqueFamilies.end()) {
                uniqueFamilies.push_back(idx);
            }
        };
        addUnique(queueFamilies_.compute);
        addUnique(queueFamilies_.transfer);
        addUnique(queueFamilies_.present);

        float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueFamilies.size());
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueInfo);
        }

        // Enable required device extensions
        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        // Vulkan 1.4 features (timeline semaphores, dynamic rendering are core)
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.timelineSemaphore = VK_TRUE;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.descriptorIndexing = VK_TRUE;

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.pNext = &features12;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &features2;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        VkResult result = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
        if (result != VK_SUCCESS) {
            return std::unexpected(RhiError::DeviceLost);
        }

        volkLoadDevice(device_);

        // Retrieve queues
        vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
        if (queueFamilies_.compute != queueFamilies_.graphics) {
            vkGetDeviceQueue(device_, queueFamilies_.compute, 0, &computeQueue_);
        } else {
            computeQueue_ = graphicsQueue_;
        }
        if (queueFamilies_.transfer != queueFamilies_.graphics) {
            vkGetDeviceQueue(device_, queueFamilies_.transfer, 0, &transferQueue_);
        } else {
            transferQueue_ = graphicsQueue_;
        }

        return {};
    }

    // =========================================================================
    // Init / Destroy
    // =========================================================================

    auto VulkanDevice::Init(const VulkanDeviceDesc& desc) -> RhiResult<void> {
        VkResult vkResult = volkInitialize();
        if (vkResult != VK_SUCCESS) {
            return std::unexpected(RhiError::DeviceLost);
        }

        auto r1 = CreateInstance(desc);
        if (!r1) {
            return r1;
        }

        auto r2 = SelectPhysicalDevice();
        if (!r2) {
            return r2;
        }

        auto r3 = CreateLogicalDevice();
        if (!r3) {
            return r3;
        }

        return {};
    }

    VulkanDevice::~VulkanDevice() {
        if (device_) {
            vkDeviceWaitIdle(device_);

            // Destroy all tracked sync objects
            for (auto& [id, fence] : fences_) {
                vkDestroyFence(device_, fence, nullptr);
            }
            for (auto& [id, sem] : semaphores_) {
                vkDestroySemaphore(device_, sem, nullptr);
            }
            // Destroy all tracked swapchains
            for (auto& [id, sc] : swapchains_) {
                vkDestroySwapchainKHR(device_, sc.swapchain, nullptr);
                vkDestroySurfaceKHR(instance_, sc.surface, nullptr);
            }

            vkDestroyDevice(device_, nullptr);
        }
        if (debugMessenger_ && instance_) {
            if (vkDestroyDebugUtilsMessengerEXT) {
                vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
            }
        }
        if (instance_) {
            vkDestroyInstance(instance_, nullptr);
        }
    }

    // =========================================================================
    // Sync primitives
    // =========================================================================

    auto VulkanDevice::CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle> {
        VkFenceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (signaled) {
            info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        }

        VkFence fence;
        VkResult r = vkCreateFence(device_, &info, nullptr, &fence);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto id = AllocHandle();
        fences_[id] = fence;
        return FenceHandle{id};
    }

    void VulkanDevice::DestroyFenceImpl(FenceHandle h) {
        auto it = fences_.find(h.IsValid() ? h.value : 0);
        if (it != fences_.end()) {
            vkDestroyFence(device_, it->second, nullptr);
            fences_.erase(it);
        }
    }

    void VulkanDevice::WaitFenceImpl(FenceHandle h, uint64_t timeout) {
        auto it = fences_.find(h.value);
        if (it != fences_.end()) {
            vkWaitForFences(device_, 1, &it->second, VK_TRUE, timeout);
        }
    }

    void VulkanDevice::ResetFenceImpl(FenceHandle h) {
        auto it = fences_.find(h.value);
        if (it != fences_.end()) {
            vkResetFences(device_, 1, &it->second);
        }
    }

    auto VulkanDevice::GetFenceStatusImpl(FenceHandle h) -> bool {
        auto it = fences_.find(h.value);
        if (it != fences_.end()) {
            return vkGetFenceStatus(device_, it->second) == VK_SUCCESS;
        }
        return false;
    }

    auto VulkanDevice::CreateSemaphoreImpl(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle> {
        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.semaphoreType
            = (desc.type == SemaphoreType::Timeline) ? VK_SEMAPHORE_TYPE_TIMELINE : VK_SEMAPHORE_TYPE_BINARY;
        typeInfo.initialValue = desc.initialValue;

        VkSemaphoreCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        info.pNext = &typeInfo;

        VkSemaphore sem;
        VkResult r = vkCreateSemaphore(device_, &info, nullptr, &sem);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto id = AllocHandle();
        semaphores_[id] = sem;
        return SemaphoreHandle{id};
    }

    void VulkanDevice::DestroySemaphoreImpl(SemaphoreHandle h) {
        auto it = semaphores_.find(h.value);
        if (it != semaphores_.end()) {
            vkDestroySemaphore(device_, it->second, nullptr);
            semaphores_.erase(it);
        }
    }

    void VulkanDevice::SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value) {
        auto it = semaphores_.find(h.value);
        if (it == semaphores_.end()) {
            return;
        }

        VkSemaphoreSignalInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        signalInfo.semaphore = it->second;
        signalInfo.value = value;
        vkSignalSemaphore(device_, &signalInfo);
    }

    void VulkanDevice::WaitSemaphoreImpl(SemaphoreHandle h, uint64_t value, uint64_t timeout) {
        auto it = semaphores_.find(h.value);
        if (it == semaphores_.end()) {
            return;
        }

        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &it->second;
        waitInfo.pValues = &value;
        vkWaitSemaphores(device_, &waitInfo, timeout);
    }

    auto VulkanDevice::GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t {
        auto it = semaphores_.find(h.value);
        if (it == semaphores_.end()) {
            return 0;
        }
        uint64_t value = 0;
        vkGetSemaphoreCounterValue(device_, it->second, &value);
        return value;
    }

    void VulkanDevice::WaitIdleImpl() {
        if (device_) {
            vkDeviceWaitIdle(device_);
        }
    }

    void VulkanDevice::SubmitImpl(QueueType queue, const SubmitDesc& /*desc*/) {
        // TODO(Phase-Backend): Full submit implementation with semaphore/fence marshalling.
        // For now, this is a placeholder that will be filled when command buffers are implemented.
        (void)queue;
    }

}  // namespace miki::rhi
