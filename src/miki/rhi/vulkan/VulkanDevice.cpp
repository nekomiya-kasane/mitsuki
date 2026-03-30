/** @file VulkanDevice.cpp
 *  @brief Vulkan 1.4 backend — instance, device, queue init, VMA, timeline semaphores,
 *         capability population, sync primitives, submit.
 */

#include "miki/rhi/backend/VulkanDevice.h"
#include "miki/rhi/backend/VulkanCommandBuffer.h"

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wnullability-extension"
#    pragma clang diagnostic ignored "-Wnullability-completeness"
#elif defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 5105)
#endif

#include <vk_mem_alloc.h>

#if defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(_MSC_VER)
#    pragma warning(pop)
#endif

#include "miki/debug/StructuredLogger.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace miki::rhi {

    // =========================================================================
    // Debug messenger callback
    // =========================================================================

    namespace {
        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT /*type*/,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* /*userData*/
        ) {
            using enum ::miki::debug::LogCategory;
            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
                MIKI_LOG_ERROR(Rhi, "[Vulkan] {}", callbackData->pMessage);
            } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
                MIKI_LOG_WARN(Rhi, "[Vulkan] {}", callbackData->pMessage);
            } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
                MIKI_LOG_INFO(Rhi, "[Vulkan] {}", callbackData->pMessage);
            } else {
                MIKI_LOG_TRACE(Rhi, "[Vulkan] {}", callbackData->pMessage);
            }
            return VK_FALSE;
        }
    }  // namespace

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
        appInfo.apiVersion = (tier_ == BackendType::VulkanCompat) ? VK_API_VERSION_1_1 : VK_API_VERSION_1_4;

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

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            auto flags = queueFamilies[i].queueFlags;

            if ((flags & VK_QUEUE_GRAPHICS_BIT) && queueFamilies_.graphics == UINT32_MAX) {
                queueFamilies_.graphics = i;
                queueFamilies_.present = i;
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

        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        // =====================================================================
        // Probe physical device features to decide tier
        // =====================================================================
        VkPhysicalDeviceVulkan12Features supported12{};
        supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceVulkan13Features supported13{};
        supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        supported13.pNext = &supported12;
        VkPhysicalDeviceFeatures2 supported2{};
        supported2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supported2.pNext = &supported13;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &supported2);

        // Check API version
        VkPhysicalDeviceProperties devProps{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &devProps);
        bool has14Api
            = VK_API_VERSION_MAJOR(devProps.apiVersion) >= 1 && VK_API_VERSION_MINOR(devProps.apiVersion) >= 4;

        // If user requested Vulkan14, check for critical features and auto-downgrade
        if (tier_ == BackendType::Vulkan14) {
            using enum ::miki::debug::LogCategory;
            bool missingCritical = false;
            auto check = [&](VkBool32 supported, const char* name) {
                if (!supported) {
                    MIKI_LOG_WARN(
                        Rhi, "[Vulkan] Tier1 feature '{}' not supported by GPU, will downgrade to Compat", name
                    );
                    missingCritical = true;
                }
            };
            if (!has14Api) {
                MIKI_LOG_WARN(
                    Rhi, "[Vulkan] GPU does not support Vulkan 1.4 (reported {}.{}), will downgrade to Compat",
                    VK_API_VERSION_MAJOR(devProps.apiVersion), VK_API_VERSION_MINOR(devProps.apiVersion)
                );
                missingCritical = true;
            }
            check(supported12.timelineSemaphore, "timelineSemaphore");
            check(supported12.bufferDeviceAddress, "bufferDeviceAddress");
            check(supported12.descriptorIndexing, "descriptorIndexing");
            check(supported13.dynamicRendering, "dynamicRendering");
            check(supported13.synchronization2, "synchronization2");
            check(supported13.maintenance4, "maintenance4");

            if (missingCritical) {
                MIKI_LOG_WARN(Rhi, "[Vulkan] Downgrading from Vulkan14 to VulkanCompat due to missing features");
                tier_ = BackendType::VulkanCompat;
            }
        }

        // =====================================================================
        // Build feature request chain based on final tier_
        // =====================================================================
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        // 1.0 core features — requested for both tiers (only if supported)
        features2.features.samplerAnisotropy = supported2.features.samplerAnisotropy;
        features2.features.fillModeNonSolid = supported2.features.fillModeNonSolid;
        features2.features.wideLines = supported2.features.wideLines;
        features2.features.depthClamp = supported2.features.depthClamp;
        features2.features.depthBiasClamp = supported2.features.depthBiasClamp;
        features2.features.multiDrawIndirect = supported2.features.multiDrawIndirect;
        features2.features.drawIndirectFirstInstance = supported2.features.drawIndirectFirstInstance;

        VkPhysicalDeviceVulkan11Features features11{};
        VkPhysicalDeviceVulkan12Features features12{};
        VkPhysicalDeviceVulkan13Features features13{};

        if (tier_ == BackendType::Vulkan14) {
            // Full Vulkan 1.4 — request all critical + nice-to-have features
            features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            features11.shaderDrawParameters = VK_TRUE;  // Required for SPIR-V DrawParameters (gl_DrawID)

            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features12.pNext = &features11;
            features12.timelineSemaphore = VK_TRUE;
            features12.bufferDeviceAddress = VK_TRUE;
            features12.descriptorIndexing = VK_TRUE;
            features12.runtimeDescriptorArray = VK_TRUE;
            features12.descriptorBindingPartiallyBound = VK_TRUE;
            features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
            features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;

            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            features13.pNext = &features12;
            features13.dynamicRendering = VK_TRUE;
            features13.synchronization2 = VK_TRUE;
            features13.maintenance4 = VK_TRUE;

            features2.pNext = &features13;
            // Optional 1.0 features (only if GPU supports)
            features2.features.shaderFloat64 = supported2.features.shaderFloat64;
            features2.features.shaderInt64 = supported2.features.shaderInt64;
        } else {
            // VulkanCompat — only 1.0/1.1 core, no pNext chain for 1.2/1.3 features
            features2.pNext = nullptr;

            // Compat may still need VK_KHR_timeline_semaphore if available as extension
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> availableExts(extCount);
            vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, availableExts.data());
            auto hasExt = [&](const char* name) {
                return std::ranges::any_of(availableExts, [name](const VkExtensionProperties& e) {
                    return std::strcmp(e.extensionName, name) == 0;
                });
            };
            if (hasExt(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
            }
            if (hasExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            }
            if (hasExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            }
            if (hasExt(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);
            }
        }

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
    // VMA allocator creation
    // =========================================================================

    auto VulkanDevice::CreateVmaAllocator() -> RhiResult<void> {
        // Manually fill VMA function table from volk-loaded function pointers.
        // VMA_STATIC_VULKAN_FUNCTIONS=0 and VMA_DYNAMIC_VULKAN_FUNCTIONS=0
        // require us to provide every function pointer explicitly.
        VmaVulkanFunctions vmaFunctions{};
        vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        vmaFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
        vmaFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
        vmaFunctions.vkAllocateMemory = vkAllocateMemory;
        vmaFunctions.vkFreeMemory = vkFreeMemory;
        vmaFunctions.vkMapMemory = vkMapMemory;
        vmaFunctions.vkUnmapMemory = vkUnmapMemory;
        vmaFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
        vmaFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
        vmaFunctions.vkBindBufferMemory = vkBindBufferMemory;
        vmaFunctions.vkBindImageMemory = vkBindImageMemory;
        vmaFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
        vmaFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
        vmaFunctions.vkCreateBuffer = vkCreateBuffer;
        vmaFunctions.vkDestroyBuffer = vkDestroyBuffer;
        vmaFunctions.vkCreateImage = vkCreateImage;
        vmaFunctions.vkDestroyImage = vkDestroyImage;
        vmaFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
        vmaFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
        vmaFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
        vmaFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
        vmaFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
        vmaFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
        vmaFunctions.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
        vmaFunctions.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        allocatorInfo.physicalDevice = physicalDevice_;
        allocatorInfo.device = device_;
        allocatorInfo.instance = instance_;
        allocatorInfo.pVulkanFunctions = &vmaFunctions;
        allocatorInfo.vulkanApiVersion = tier_ == BackendType::VulkanCompat ? VK_API_VERSION_1_1 : VK_API_VERSION_1_4;

        VkResult r = vmaCreateAllocator(&allocatorInfo, &allocator_);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }
        return {};
    }

    // =========================================================================
    // Timeline semaphore creation (specs/03-sync.md §3.2)
    // =========================================================================

    auto VulkanDevice::CreateTimelineSemaphores() -> RhiResult<void> {
        auto createAndRegister = [&](SemaphoreHandle& outHandle) -> RhiResult<void> {
            VkSemaphoreTypeCreateInfo typeInfo{};
            typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            typeInfo.initialValue = 0;

            VkSemaphoreCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            info.pNext = &typeInfo;

            VkSemaphore sem = VK_NULL_HANDLE;
            if (vkCreateSemaphore(device_, &info, nullptr, &sem) != VK_SUCCESS) {
                return std::unexpected(RhiError::OutOfDeviceMemory);
            }

            auto [handle, data] = semaphores_.Allocate();
            if (!data) {
                vkDestroySemaphore(device_, sem, nullptr);
                return std::unexpected(RhiError::TooManyObjects);
            }
            data->semaphore = sem;
            data->type = SemaphoreType::Timeline;
            outHandle = handle;
            return {};
        };

        if (auto r = createAndRegister(queueTimelines_.graphics); !r) {
            return r;
        }
        if (auto r = createAndRegister(queueTimelines_.compute); !r) {
            return r;
        }
        if (auto r = createAndRegister(queueTimelines_.transfer); !r) {
            return r;
        }

        graphicsTimelineValue_ = 0;
        computeTimelineValue_ = 0;
        transferTimelineValue_ = 0;
        return {};
    }

    // =========================================================================
    // Capability population
    // =========================================================================

    void VulkanDevice::PopulateCapabilities() {
        using enum ::miki::debug::LogCategory;

        // =====================================================================
        // Tier & Backend — based on final tier_ (may have been downgraded)
        // =====================================================================
        const bool isTier1 = (tier_ == BackendType::Vulkan14);
        capabilities_.tier = isTier1 ? CapabilityTier::Tier1_Vulkan : CapabilityTier::Tier2_Compat;
        capabilities_.backendType = tier_;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &props);

        capabilities_.deviceName = props.deviceName;
        capabilities_.driverVersion = std::to_string(VK_API_VERSION_MAJOR(props.driverVersion)) + "."
                                      + std::to_string(VK_API_VERSION_MINOR(props.driverVersion)) + "."
                                      + std::to_string(VK_API_VERSION_PATCH(props.driverVersion));
        capabilities_.vendorId = props.vendorID;
        capabilities_.deviceId = props.deviceID;

        // Limits (universal — same for both tiers)
        capabilities_.maxTextureSize2D = props.limits.maxImageDimension2D;
        capabilities_.maxTextureSizeCube = props.limits.maxImageDimensionCube;
        capabilities_.maxFramebufferWidth = props.limits.maxFramebufferWidth;
        capabilities_.maxFramebufferHeight = props.limits.maxFramebufferHeight;
        capabilities_.maxViewports = props.limits.maxViewports;
        capabilities_.maxClipDistances = props.limits.maxClipDistances;
        capabilities_.maxColorAttachments = props.limits.maxColorAttachments;
        capabilities_.maxPushConstantSize = props.limits.maxPushConstantsSize;
        capabilities_.maxBoundDescriptorSets = props.limits.maxBoundDescriptorSets;
        capabilities_.maxDrawIndirectCount = props.limits.maxDrawIndirectCount;
        capabilities_.subgroupSize = props.limits.maxComputeWorkGroupSize[0];  // approximate

        capabilities_.maxComputeWorkGroupCount = {
            props.limits.maxComputeWorkGroupCount[0],
            props.limits.maxComputeWorkGroupCount[1],
            props.limits.maxComputeWorkGroupCount[2],
        };
        capabilities_.maxComputeWorkGroupSize = {
            props.limits.maxComputeWorkGroupSize[0],
            props.limits.maxComputeWorkGroupSize[1],
            props.limits.maxComputeWorkGroupSize[2],
        };

        // Vulkan 1.1 subgroup properties
        VkPhysicalDeviceSubgroupProperties subgroupProps{};
        subgroupProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &subgroupProps;
        vkGetPhysicalDeviceProperties2(physicalDevice_, &props2);
        capabilities_.subgroupSize = subgroupProps.subgroupSize;

        // Memory info
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                capabilities_.deviceLocalMemoryBytes += memProps.memoryHeaps[i].size;
            } else {
                capabilities_.hostVisibleMemoryBytes += memProps.memoryHeaps[i].size;
            }
        }

        // Common feature probing
        VkPhysicalDeviceFeatures deviceFeatures{};
        vkGetPhysicalDeviceFeatures(physicalDevice_, &deviceFeatures);

        capabilities_.hasAsyncCompute = (queueFamilies_.compute != queueFamilies_.graphics);
        capabilities_.hasAsyncTransfer = (queueFamilies_.transfer != queueFamilies_.graphics);
        capabilities_.hasMultiDrawIndirect = (deviceFeatures.multiDrawIndirect == VK_TRUE);
        capabilities_.hasSubgroupOps = true;  // Vulkan 1.1+ always has subgroup ops
        capabilities_.hasPushDescriptors = false;

        // Extension list (shared by tier-specific methods)
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> availableExts(extCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, availableExts.data());

        auto hasExt = [&](const char* name) -> bool {
            return std::ranges::any_of(availableExts, [name](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, name) == 0;
            });
        };

        // =====================================================================
        // Dispatch to tier-specific population
        // =====================================================================
        if (isTier1) {
            PopulateCapabilities_Tier1(deviceFeatures, availableExts);
        } else {
            PopulateCapabilities_Tier2(deviceFeatures, availableExts);
        }

        // =====================================================================
        // Common features & extensions (both tiers)
        // =====================================================================
        capabilities_.enabledFeatures.Add(DeviceFeature::Present);
        if (capabilities_.hasAsyncCompute) {
            capabilities_.enabledFeatures.Add(DeviceFeature::AsyncCompute);
        }

        // Sparse binding
        capabilities_.hasSparseBinding = (deviceFeatures.sparseBinding == VK_TRUE);
        if (capabilities_.hasSparseBinding) {
            capabilities_.enabledFeatures.Add(DeviceFeature::SparseBinding);
        }

        // ReBAR detection
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            auto flags = memProps.memoryTypes[i].propertyFlags;
            if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                auto heapSize = memProps.memoryHeaps[memProps.memoryTypes[i].heapIndex].size;
                constexpr uint64_t kRebarThreshold = 256ULL * 1024 * 1024;
                if (heapSize >= kRebarThreshold) {
                    capabilities_.hasResizableBAR = true;
                    break;
                }
            }
        }

        // Memory budget query
        if (hasExt(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) {
            capabilities_.hasMemoryBudgetQuery = true;
        }

        // Push descriptors
        if (hasExt(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
            capabilities_.hasPushDescriptors = true;
            capabilities_.enabledFeatures.Add(DeviceFeature::PushDescriptors);
        }

        MIKI_LOG_INFO(
            Rhi, "[Vulkan] Capability tier: {} (backend: {}, GPU: {})", isTier1 ? "Tier1_Vulkan" : "Tier2_Compat",
            isTier1 ? "Vulkan14" : "VulkanCompat", capabilities_.deviceName
        );

        PopulateFormatSupport();
    }

    // =========================================================================
    // Tier1 (Vulkan 1.4) capability population
    // =========================================================================

    void VulkanDevice::PopulateCapabilities_Tier1(
        const VkPhysicalDeviceFeatures& deviceFeatures, const std::vector<VkExtensionProperties>& availableExts
    ) {
        auto hasExt = [&](const char* name) -> bool {
            return std::ranges::any_of(availableExts, [name](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, name) == 0;
            });
        };

        // Features guaranteed by Vulkan 1.4 device creation
        capabilities_.hasTimelineSemaphore = true;
        capabilities_.hasMultiDrawIndirectCount = true;
        capabilities_.hasFloat64 = (deviceFeatures.shaderFloat64 == VK_TRUE);
        capabilities_.hasBindless = true;
        capabilities_.maxStorageBufferSize = UINT64_MAX;

        // Descriptor model
        if (hasExt("VK_EXT_descriptor_heap")) {
            capabilities_.descriptorModel = DescriptorModel::DescriptorHeap;
        } else if (hasExt(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)) {
            capabilities_.descriptorModel = DescriptorModel::DescriptorBuffer;
            capabilities_.enabledFeatures.Add(DeviceFeature::DescriptorBuffer);
        } else {
            capabilities_.descriptorModel = DescriptorModel::DescriptorSet;
        }

        // Mesh shader
        if (hasExt(VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
            capabilities_.hasMeshShader = true;
            capabilities_.hasTaskShader = true;
            capabilities_.enabledFeatures.Add(DeviceFeature::MeshShader);

            VkPhysicalDeviceMeshShaderPropertiesEXT meshProps{};
            meshProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
            VkPhysicalDeviceProperties2 meshProps2{};
            meshProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            meshProps2.pNext = &meshProps;
            vkGetPhysicalDeviceProperties2(physicalDevice_, &meshProps2);

            capabilities_.maxMeshWorkGroupSize = {
                meshProps.maxMeshWorkGroupSize[0],
                meshProps.maxMeshWorkGroupSize[1],
                meshProps.maxMeshWorkGroupSize[2],
            };
            capabilities_.maxMeshWorkGroupInvocations = meshProps.maxMeshWorkGroupInvocations;
            capabilities_.maxTaskWorkGroupSize = {
                meshProps.maxTaskWorkGroupSize[0],
                meshProps.maxTaskWorkGroupSize[1],
                meshProps.maxTaskWorkGroupSize[2],
            };
            capabilities_.maxTaskWorkGroupInvocations = meshProps.maxTaskWorkGroupInvocations;
            capabilities_.maxMeshOutputVertices = meshProps.maxMeshOutputVertices;
            capabilities_.maxMeshOutputPrimitives = meshProps.maxMeshOutputPrimitives;
            capabilities_.maxTaskPayloadSize = meshProps.maxTaskPayloadSize;
            capabilities_.maxMeshSharedMemorySize = meshProps.maxMeshSharedMemorySize;
            capabilities_.maxMeshPayloadAndSharedMemorySize = meshProps.maxMeshPayloadAndSharedMemorySize;
        }

        // Ray tracing
        if (hasExt(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
            capabilities_.hasAccelerationStructure = true;
        }
        if (hasExt(VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
            capabilities_.hasRayQuery = true;
        }
        if (hasExt(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
            capabilities_.hasRayTracingPipeline = true;
            capabilities_.enabledFeatures.Add(DeviceFeature::RayTracingPipeline);
        }

        // Variable rate shading
        if (hasExt(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) {
            capabilities_.hasVariableRateShading = true;
            capabilities_.enabledFeatures.Add(DeviceFeature::VariableRateShading);
        }

        // DeviceFeatureSet — always-on for Tier1
        capabilities_.enabledFeatures.Add(DeviceFeature::TimelineSemaphore);
        capabilities_.enabledFeatures.Add(DeviceFeature::DynamicRendering);
        capabilities_.enabledFeatures.Add(DeviceFeature::Synchronization2);
        capabilities_.enabledFeatures.Add(DeviceFeature::BufferDeviceAddress);
        capabilities_.enabledFeatures.Add(DeviceFeature::DescriptorIndexing);
        capabilities_.enabledFeatures.Add(DeviceFeature::MultiDrawIndirect);
        capabilities_.enabledFeatures.Add(DeviceFeature::MultiDrawIndirectCount);
        if (capabilities_.hasSubgroupOps) {
            capabilities_.enabledFeatures.Add(DeviceFeature::SubgroupOps);
        }
        if (capabilities_.hasFloat64) {
            capabilities_.enabledFeatures.Add(DeviceFeature::Float64);
        }
    }

    // =========================================================================
    // Tier2 (VulkanCompat) capability population
    // =========================================================================

    void VulkanDevice::PopulateCapabilities_Tier2(
        const VkPhysicalDeviceFeatures& deviceFeatures, const std::vector<VkExtensionProperties>& availableExts
    ) {
        auto hasExt = [&](const char* name) -> bool {
            return std::ranges::any_of(availableExts, [name](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, name) == 0;
            });
        };

        // Compat: probe what's actually available
        capabilities_.hasTimelineSemaphore = false;
        capabilities_.hasMultiDrawIndirectCount = false;
        capabilities_.hasFloat64 = (deviceFeatures.shaderFloat64 == VK_TRUE);
        capabilities_.hasBindless = false;
        capabilities_.maxStorageBufferSize = 128ULL * 1024 * 1024;
        capabilities_.descriptorModel = DescriptorModel::DescriptorSet;

        // DeviceFeatureSet — probe optional extensions
        if (hasExt(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
            capabilities_.hasTimelineSemaphore = true;
            capabilities_.enabledFeatures.Add(DeviceFeature::TimelineSemaphore);
        }
        if (hasExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
            capabilities_.enabledFeatures.Add(DeviceFeature::DynamicRendering);
        }
        if (hasExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
            capabilities_.enabledFeatures.Add(DeviceFeature::Synchronization2);
        }
        if (capabilities_.hasMultiDrawIndirect) {
            capabilities_.enabledFeatures.Add(DeviceFeature::MultiDrawIndirect);
        }
        if (capabilities_.hasSubgroupOps) {
            capabilities_.enabledFeatures.Add(DeviceFeature::SubgroupOps);
        }
    }

    void VulkanDevice::PopulateFormatSupport() {
        // Mapping from miki::rhi::Format to VkFormat (indexed by Format enum value)
        static constexpr VkFormat kFormatMap[] = {
            VK_FORMAT_UNDEFINED,                 // Undefined
            VK_FORMAT_R8_UNORM,                  // R8_UNORM
            VK_FORMAT_R8_SNORM,                  // R8_SNORM
            VK_FORMAT_R8_UINT,                   // R8_UINT
            VK_FORMAT_R8_SINT,                   // R8_SINT
            VK_FORMAT_R8G8_UNORM,                // RG8_UNORM
            VK_FORMAT_R8G8_SNORM,                // RG8_SNORM
            VK_FORMAT_R8G8_UINT,                 // RG8_UINT
            VK_FORMAT_R8G8_SINT,                 // RG8_SINT
            VK_FORMAT_R8G8B8A8_UNORM,            // RGBA8_UNORM
            VK_FORMAT_R8G8B8A8_SNORM,            // RGBA8_SNORM
            VK_FORMAT_R8G8B8A8_UINT,             // RGBA8_UINT
            VK_FORMAT_R8G8B8A8_SINT,             // RGBA8_SINT
            VK_FORMAT_R8G8B8A8_SRGB,             // RGBA8_SRGB
            VK_FORMAT_B8G8R8A8_UNORM,            // BGRA8_UNORM
            VK_FORMAT_B8G8R8A8_SRGB,             // BGRA8_SRGB
            VK_FORMAT_R16_UNORM,                 // R16_UNORM
            VK_FORMAT_R16_SNORM,                 // R16_SNORM
            VK_FORMAT_R16_UINT,                  // R16_UINT
            VK_FORMAT_R16_SINT,                  // R16_SINT
            VK_FORMAT_R16_SFLOAT,                // R16_FLOAT
            VK_FORMAT_R16G16_UNORM,              // RG16_UNORM
            VK_FORMAT_R16G16_SNORM,              // RG16_SNORM
            VK_FORMAT_R16G16_UINT,               // RG16_UINT
            VK_FORMAT_R16G16_SINT,               // RG16_SINT
            VK_FORMAT_R16G16_SFLOAT,             // RG16_FLOAT
            VK_FORMAT_R16G16B16A16_UNORM,        // RGBA16_UNORM
            VK_FORMAT_R16G16B16A16_SNORM,        // RGBA16_SNORM
            VK_FORMAT_R16G16B16A16_UINT,         // RGBA16_UINT
            VK_FORMAT_R16G16B16A16_SINT,         // RGBA16_SINT
            VK_FORMAT_R16G16B16A16_SFLOAT,       // RGBA16_FLOAT
            VK_FORMAT_R32_UINT,                  // R32_UINT
            VK_FORMAT_R32_SINT,                  // R32_SINT
            VK_FORMAT_R32_SFLOAT,                // R32_FLOAT
            VK_FORMAT_R32G32_UINT,               // RG32_UINT
            VK_FORMAT_R32G32_SINT,               // RG32_SINT
            VK_FORMAT_R32G32_SFLOAT,             // RG32_FLOAT
            VK_FORMAT_R32G32B32_UINT,            // RGB32_UINT
            VK_FORMAT_R32G32B32_SINT,            // RGB32_SINT
            VK_FORMAT_R32G32B32_SFLOAT,          // RGB32_FLOAT
            VK_FORMAT_R32G32B32A32_UINT,         // RGBA32_UINT
            VK_FORMAT_R32G32B32A32_SINT,         // RGBA32_SINT
            VK_FORMAT_R32G32B32A32_SFLOAT,       // RGBA32_FLOAT
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,  // RGB10A2_UNORM
            VK_FORMAT_B10G11R11_UFLOAT_PACK32,   // RG11B10_FLOAT
            VK_FORMAT_D16_UNORM,                 // D16_UNORM
            VK_FORMAT_D32_SFLOAT,                // D32_FLOAT
            VK_FORMAT_D24_UNORM_S8_UINT,         // D24_UNORM_S8_UINT
            VK_FORMAT_D32_SFLOAT_S8_UINT,        // D32_FLOAT_S8_UINT
            VK_FORMAT_BC1_RGBA_UNORM_BLOCK,      // BC1_UNORM
            VK_FORMAT_BC1_RGBA_SRGB_BLOCK,       // BC1_SRGB
            VK_FORMAT_BC2_UNORM_BLOCK,           // BC2_UNORM
            VK_FORMAT_BC2_SRGB_BLOCK,            // BC2_SRGB
            VK_FORMAT_BC3_UNORM_BLOCK,           // BC3_UNORM
            VK_FORMAT_BC3_SRGB_BLOCK,            // BC3_SRGB
            VK_FORMAT_BC4_UNORM_BLOCK,           // BC4_UNORM
            VK_FORMAT_BC4_SNORM_BLOCK,           // BC4_SNORM
            VK_FORMAT_BC5_UNORM_BLOCK,           // BC5_UNORM
            VK_FORMAT_BC5_SNORM_BLOCK,           // BC5_SNORM
            VK_FORMAT_BC6H_UFLOAT_BLOCK,         // BC6H_UFLOAT
            VK_FORMAT_BC6H_SFLOAT_BLOCK,         // BC6H_SFLOAT
            VK_FORMAT_BC7_UNORM_BLOCK,           // BC7_UNORM
            VK_FORMAT_BC7_SRGB_BLOCK,            // BC7_SRGB
            VK_FORMAT_ASTC_4x4_UNORM_BLOCK,      // ASTC_4x4_UNORM
            VK_FORMAT_ASTC_4x4_SRGB_BLOCK,       // ASTC_4x4_SRGB
        };
        static_assert(std::size(kFormatMap) == GpuCapabilityProfile::kFormatCount);

        for (uint32_t i = 1; i < GpuCapabilityProfile::kFormatCount; ++i) {
            VkFormatProperties fmtProps{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, kFormatMap[i], &fmtProps);

            // Use optimalTilingFeatures for texture formats, linearTilingFeatures for buffer
            auto vkFlags = fmtProps.optimalTilingFeatures;
            FormatFeatureFlags flags = FormatFeatureFlags::None;

            if (vkFlags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
                flags = flags | FormatFeatureFlags::Sampled;
            }
            if (vkFlags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) {
                flags = flags | FormatFeatureFlags::Storage;
            }
            if (vkFlags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
                flags = flags | FormatFeatureFlags::ColorAttachment;
            }
            if (vkFlags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                flags = flags | FormatFeatureFlags::DepthStencil;
            }
            if (vkFlags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) {
                flags = flags | FormatFeatureFlags::BlendSrc;
            }
            if (vkFlags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) {
                flags = flags | FormatFeatureFlags::Filter;
            }

            capabilities_.formatSupport[i] = flags;
        }
    }

    // =========================================================================
    // Init / Destroy
    // =========================================================================

    auto VulkanDevice::Init(const VulkanDeviceDesc& desc) -> RhiResult<void> {
        tier_ = desc.tier;

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

        auto r4 = CreateVmaAllocator();
        if (!r4) {
            return r4;
        }

        auto r5 = CreateTimelineSemaphores();
        if (!r5) {
            return r5;
        }

        PopulateCapabilities();

        return {};
    }

    VulkanDevice::VulkanDevice() = default;

    VulkanDevice::~VulkanDevice() {
        if (device_) {
            vkDeviceWaitIdle(device_);
        }

        // Timeline semaphores (device-global, registered in HandlePool)
        if (queueTimelines_.graphics.IsValid()) {
            DestroySemaphoreImpl(queueTimelines_.graphics);
        }
        if (queueTimelines_.compute.IsValid()) {
            DestroySemaphoreImpl(queueTimelines_.compute);
        }
        if (queueTimelines_.transfer.IsValid()) {
            DestroySemaphoreImpl(queueTimelines_.transfer);
        }

        // Descriptor pools
        if (activeDescriptorPool_) {
            vkDestroyDescriptorPool(device_, activeDescriptorPool_, nullptr);
        }
        for (auto pool : retiredDescriptorPools_) {
            vkDestroyDescriptorPool(device_, pool, nullptr);
        }

        // VMA allocator
        if (allocator_) {
            vmaDestroyAllocator(allocator_);
            allocator_ = nullptr;
        }

        if (device_) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (debugMessenger_ && instance_) {
            if (vkDestroyDebugUtilsMessengerEXT) {
                vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
            }
        }
        if (instance_) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    // =========================================================================
    // Sync primitives — HandlePool-based
    // =========================================================================

    auto VulkanDevice::CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle> {
        VkFenceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (signaled) {
            info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        }

        VkFence fence = VK_NULL_HANDLE;
        VkResult r = vkCreateFence(device_, &info, nullptr, &fence);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = fences_.Allocate();
        if (!data) {
            vkDestroyFence(device_, fence, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->fence = fence;
        return handle;
    }

    void VulkanDevice::DestroyFenceImpl(FenceHandle h) {
        auto* data = fences_.Lookup(h);
        if (data) {
            vkDestroyFence(device_, data->fence, nullptr);
            fences_.Free(h);
        }
    }

    void VulkanDevice::WaitFenceImpl(FenceHandle h, uint64_t timeout) {
        auto* data = fences_.Lookup(h);
        if (data) {
            vkWaitForFences(device_, 1, &data->fence, VK_TRUE, timeout);
        }
    }

    void VulkanDevice::ResetFenceImpl(FenceHandle h) {
        auto* data = fences_.Lookup(h);
        if (data) {
            vkResetFences(device_, 1, &data->fence);
        }
    }

    auto VulkanDevice::GetFenceStatusImpl(FenceHandle h) -> bool {
        auto* data = fences_.Lookup(h);
        if (data) {
            return vkGetFenceStatus(device_, data->fence) == VK_SUCCESS;
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

        VkSemaphore sem = VK_NULL_HANDLE;
        VkResult r = vkCreateSemaphore(device_, &info, nullptr, &sem);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = semaphores_.Allocate();
        if (!data) {
            vkDestroySemaphore(device_, sem, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->semaphore = sem;
        data->type = desc.type;
        return handle;
    }

    void VulkanDevice::DestroySemaphoreImpl(SemaphoreHandle h) {
        auto* data = semaphores_.Lookup(h);
        if (data) {
            vkDestroySemaphore(device_, data->semaphore, nullptr);
            semaphores_.Free(h);
        }
    }

    void VulkanDevice::SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value) {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return;
        }

        VkSemaphoreSignalInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        signalInfo.semaphore = data->semaphore;
        signalInfo.value = value;
        vkSignalSemaphore(device_, &signalInfo);
    }

    void VulkanDevice::WaitSemaphoreImpl(SemaphoreHandle h, uint64_t value, uint64_t timeout) {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return;
        }

        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &data->semaphore;
        waitInfo.pValues = &value;
        vkWaitSemaphores(device_, &waitInfo, timeout);
    }

    auto VulkanDevice::GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return 0;
        }
        uint64_t value = 0;
        vkGetSemaphoreCounterValue(device_, data->semaphore, &value);
        return value;
    }

    void VulkanDevice::WaitIdleImpl() {
        if (device_) {
            vkDeviceWaitIdle(device_);
        }
    }

    void VulkanDevice::SubmitImpl(QueueType queue, const SubmitDesc& desc) {
        // Select target queue
        VkQueue targetQueue = graphicsQueue_;
        if (queue == QueueType::Compute) {
            targetQueue = computeQueue_;
        } else if (queue == QueueType::Transfer) {
            targetQueue = transferQueue_;
        }

        // Marshal command buffers
        std::vector<VkCommandBufferSubmitInfo> cmdInfos;
        cmdInfos.reserve(desc.commandBuffers.size());
        for (auto h : desc.commandBuffers) {
            auto* data = commandBuffers_.Lookup(h);
            if (!data) {
                continue;
            }
            VkCommandBufferSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            info.commandBuffer = data->buffer;
            cmdInfos.push_back(info);
        }

        // Marshal wait semaphores
        std::vector<VkSemaphoreSubmitInfo> waitInfos;
        waitInfos.reserve(desc.waitSemaphores.size());
        for (auto& w : desc.waitSemaphores) {
            auto* semData = semaphores_.Lookup(w.semaphore);
            if (!semData) {
                continue;
            }
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = semData->semaphore;
            info.value = w.value;
            info.stageMask = static_cast<VkPipelineStageFlags2>(w.stageMask);
            waitInfos.push_back(info);
        }

        // Marshal signal semaphores
        std::vector<VkSemaphoreSubmitInfo> signalInfos;
        signalInfos.reserve(desc.signalSemaphores.size());
        for (auto& s : desc.signalSemaphores) {
            auto* semData = semaphores_.Lookup(s.semaphore);
            if (!semData) {
                continue;
            }
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = semData->semaphore;
            info.value = s.value;
            info.stageMask = static_cast<VkPipelineStageFlags2>(s.stageMask);
            signalInfos.push_back(info);
        }

        // Resolve signal fence
        VkFence vkFence = VK_NULL_HANDLE;
        if (desc.signalFence.IsValid()) {
            auto* fenceData = fences_.Lookup(desc.signalFence);
            if (fenceData) {
                vkFence = fenceData->fence;
            }
        }

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
        submitInfo.pWaitSemaphoreInfos = waitInfos.data();
        submitInfo.commandBufferInfoCount = static_cast<uint32_t>(cmdInfos.size());
        submitInfo.pCommandBufferInfos = cmdInfos.data();
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
        submitInfo.pSignalSemaphoreInfos = signalInfos.data();

        vkQueueSubmit2(targetQueue, 1, &submitInfo, vkFence);
    }

    // =========================================================================
    // Memory stats
    // =========================================================================

    auto VulkanDevice::GetMemoryStatsImpl() const -> MemoryStats {
        if (!allocator_) {
            return {};
        }
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator_, &stats);
        MemoryStats result{};
        result.totalAllocationCount = static_cast<uint32_t>(stats.total.statistics.allocationCount);
        result.totalAllocatedBytes = stats.total.statistics.allocationBytes;
        result.totalUsedBytes = stats.total.statistics.allocationBytes;
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        result.heapCount = memProps.memoryHeapCount;
        return result;
    }

    auto VulkanDevice::GetMemoryHeapBudgetsImpl(std::span<MemoryHeapBudget> out) const -> uint32_t {
        if (!allocator_) {
            return 0;
        }
        constexpr uint32_t kMaxHeaps = 16;  // VK_MAX_MEMORY_HEAPS
        VmaBudget budgets[kMaxHeaps]{};
        vmaGetHeapBudgets(allocator_, budgets);

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);

        uint32_t outSize = static_cast<uint32_t>(out.size());
        uint32_t count = (outSize < memProps.memoryHeapCount) ? outSize : memProps.memoryHeapCount;
        for (uint32_t i = 0; i < count; ++i) {
            out[i].heapIndex = i;
            out[i].budgetBytes = budgets[i].budget;
            out[i].usageBytes = budgets[i].usage;
            out[i].isDeviceLocal = (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        }
        return count;
    }

}  // namespace miki::rhi
