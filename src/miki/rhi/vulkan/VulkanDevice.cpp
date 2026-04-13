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

#include "miki/debug/StackTrace.h"
#include "miki/debug/StructuredLogger.h"

#include "miki/core/EnumStrings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace miki::rhi {

    // =========================================================================
    // volk KHR/EXT → core aliasing
    // =========================================================================

    namespace {
        // Patch volk function pointers: if a core function is NULL but its KHR/EXT
        // alias exists, assign the alias to the core name. This allows all call sites
        // to use canonical (non-suffixed) names regardless of API version.
        // Called after volkLoadDevice() for any tier.
        void volkPatchPromotedExtensions() {
            // clang-format off
            // ----- Vulkan 1.1 promoted (from KHR) -----
            if (!vkBindBufferMemory2 && vkBindBufferMemory2KHR) vkBindBufferMemory2 = vkBindBufferMemory2KHR;
            if (!vkBindImageMemory2 && vkBindImageMemory2KHR) vkBindImageMemory2 = vkBindImageMemory2KHR;
            if (!vkCmdDispatchBase && vkCmdDispatchBaseKHR) vkCmdDispatchBase = vkCmdDispatchBaseKHR;
            if (!vkCmdSetDeviceMask && vkCmdSetDeviceMaskKHR) vkCmdSetDeviceMask = vkCmdSetDeviceMaskKHR;
            if (!vkCreateDescriptorUpdateTemplate && vkCreateDescriptorUpdateTemplateKHR) vkCreateDescriptorUpdateTemplate = vkCreateDescriptorUpdateTemplateKHR;
            if (!vkCreateSamplerYcbcrConversion && vkCreateSamplerYcbcrConversionKHR) vkCreateSamplerYcbcrConversion = vkCreateSamplerYcbcrConversionKHR;
            if (!vkDestroyDescriptorUpdateTemplate && vkDestroyDescriptorUpdateTemplateKHR) vkDestroyDescriptorUpdateTemplate = vkDestroyDescriptorUpdateTemplateKHR;
            if (!vkDestroySamplerYcbcrConversion && vkDestroySamplerYcbcrConversionKHR) vkDestroySamplerYcbcrConversion = vkDestroySamplerYcbcrConversionKHR;
            if (!vkGetBufferMemoryRequirements2 && vkGetBufferMemoryRequirements2KHR) vkGetBufferMemoryRequirements2 = vkGetBufferMemoryRequirements2KHR;
            if (!vkGetDescriptorSetLayoutSupport && vkGetDescriptorSetLayoutSupportKHR) vkGetDescriptorSetLayoutSupport = vkGetDescriptorSetLayoutSupportKHR;
            if (!vkGetDeviceGroupPeerMemoryFeatures && vkGetDeviceGroupPeerMemoryFeaturesKHR) vkGetDeviceGroupPeerMemoryFeatures = vkGetDeviceGroupPeerMemoryFeaturesKHR;
            if (!vkGetImageMemoryRequirements2 && vkGetImageMemoryRequirements2KHR) vkGetImageMemoryRequirements2 = vkGetImageMemoryRequirements2KHR;
            if (!vkGetImageSparseMemoryRequirements2 && vkGetImageSparseMemoryRequirements2KHR) vkGetImageSparseMemoryRequirements2 = vkGetImageSparseMemoryRequirements2KHR;
            if (!vkTrimCommandPool && vkTrimCommandPoolKHR) vkTrimCommandPool = vkTrimCommandPoolKHR;
            if (!vkUpdateDescriptorSetWithTemplate && vkUpdateDescriptorSetWithTemplateKHR) vkUpdateDescriptorSetWithTemplate = vkUpdateDescriptorSetWithTemplateKHR;

            // ----- Vulkan 1.2 promoted (from KHR/EXT) -----
            if (!vkCmdBeginRenderPass2 && vkCmdBeginRenderPass2KHR) vkCmdBeginRenderPass2 = vkCmdBeginRenderPass2KHR;
            if (!vkCmdDrawIndexedIndirectCount && vkCmdDrawIndexedIndirectCountKHR) vkCmdDrawIndexedIndirectCount = vkCmdDrawIndexedIndirectCountKHR;
            if (!vkCmdDrawIndirectCount && vkCmdDrawIndirectCountKHR) vkCmdDrawIndirectCount = vkCmdDrawIndirectCountKHR;
            if (!vkCmdEndRenderPass2 && vkCmdEndRenderPass2KHR) vkCmdEndRenderPass2 = vkCmdEndRenderPass2KHR;
            if (!vkCmdNextSubpass2 && vkCmdNextSubpass2KHR) vkCmdNextSubpass2 = vkCmdNextSubpass2KHR;
            if (!vkCreateRenderPass2 && vkCreateRenderPass2KHR) vkCreateRenderPass2 = vkCreateRenderPass2KHR;
            if (!vkGetBufferDeviceAddress && vkGetBufferDeviceAddressKHR) vkGetBufferDeviceAddress = vkGetBufferDeviceAddressKHR;
            if (!vkGetBufferOpaqueCaptureAddress && vkGetBufferOpaqueCaptureAddressKHR) vkGetBufferOpaqueCaptureAddress = vkGetBufferOpaqueCaptureAddressKHR;
            if (!vkGetDeviceMemoryOpaqueCaptureAddress && vkGetDeviceMemoryOpaqueCaptureAddressKHR) vkGetDeviceMemoryOpaqueCaptureAddress = vkGetDeviceMemoryOpaqueCaptureAddressKHR;
            if (!vkGetSemaphoreCounterValue && vkGetSemaphoreCounterValueKHR) vkGetSemaphoreCounterValue = vkGetSemaphoreCounterValueKHR;
            if (!vkResetQueryPool && vkResetQueryPoolEXT) vkResetQueryPool = vkResetQueryPoolEXT;
            if (!vkSignalSemaphore && vkSignalSemaphoreKHR) vkSignalSemaphore = vkSignalSemaphoreKHR;
            if (!vkWaitSemaphores && vkWaitSemaphoresKHR) vkWaitSemaphores = vkWaitSemaphoresKHR;

            // ----- Vulkan 1.3 promoted (from KHR/EXT) -----
            if (!vkCmdBeginRendering && vkCmdBeginRenderingKHR) vkCmdBeginRendering = vkCmdBeginRenderingKHR;
            if (!vkCmdBindVertexBuffers2 && vkCmdBindVertexBuffers2EXT) vkCmdBindVertexBuffers2 = vkCmdBindVertexBuffers2EXT;
            if (!vkCmdBlitImage2 && vkCmdBlitImage2KHR) vkCmdBlitImage2 = vkCmdBlitImage2KHR;
            if (!vkCmdCopyBuffer2 && vkCmdCopyBuffer2KHR) vkCmdCopyBuffer2 = vkCmdCopyBuffer2KHR;
            if (!vkCmdCopyBufferToImage2 && vkCmdCopyBufferToImage2KHR) vkCmdCopyBufferToImage2 = vkCmdCopyBufferToImage2KHR;
            if (!vkCmdCopyImage2 && vkCmdCopyImage2KHR) vkCmdCopyImage2 = vkCmdCopyImage2KHR;
            if (!vkCmdCopyImageToBuffer2 && vkCmdCopyImageToBuffer2KHR) vkCmdCopyImageToBuffer2 = vkCmdCopyImageToBuffer2KHR;
            if (!vkCmdEndRendering && vkCmdEndRenderingKHR) vkCmdEndRendering = vkCmdEndRenderingKHR;
            if (!vkCmdPipelineBarrier2 && vkCmdPipelineBarrier2KHR) vkCmdPipelineBarrier2 = vkCmdPipelineBarrier2KHR;
            if (!vkCmdResetEvent2 && vkCmdResetEvent2KHR) vkCmdResetEvent2 = vkCmdResetEvent2KHR;
            if (!vkCmdResolveImage2 && vkCmdResolveImage2KHR) vkCmdResolveImage2 = vkCmdResolveImage2KHR;
            if (!vkCmdSetCullMode && vkCmdSetCullModeEXT) vkCmdSetCullMode = vkCmdSetCullModeEXT;
            if (!vkCmdSetDepthBiasEnable && vkCmdSetDepthBiasEnableEXT) vkCmdSetDepthBiasEnable = vkCmdSetDepthBiasEnableEXT;
            if (!vkCmdSetDepthBoundsTestEnable && vkCmdSetDepthBoundsTestEnableEXT) vkCmdSetDepthBoundsTestEnable = vkCmdSetDepthBoundsTestEnableEXT;
            if (!vkCmdSetDepthCompareOp && vkCmdSetDepthCompareOpEXT) vkCmdSetDepthCompareOp = vkCmdSetDepthCompareOpEXT;
            if (!vkCmdSetDepthTestEnable && vkCmdSetDepthTestEnableEXT) vkCmdSetDepthTestEnable = vkCmdSetDepthTestEnableEXT;
            if (!vkCmdSetDepthWriteEnable && vkCmdSetDepthWriteEnableEXT) vkCmdSetDepthWriteEnable = vkCmdSetDepthWriteEnableEXT;
            if (!vkCmdSetEvent2 && vkCmdSetEvent2KHR) vkCmdSetEvent2 = vkCmdSetEvent2KHR;
            if (!vkCmdSetFrontFace && vkCmdSetFrontFaceEXT) vkCmdSetFrontFace = vkCmdSetFrontFaceEXT;
            if (!vkCmdSetPrimitiveRestartEnable && vkCmdSetPrimitiveRestartEnableEXT) vkCmdSetPrimitiveRestartEnable = vkCmdSetPrimitiveRestartEnableEXT;
            if (!vkCmdSetPrimitiveTopology && vkCmdSetPrimitiveTopologyEXT) vkCmdSetPrimitiveTopology = vkCmdSetPrimitiveTopologyEXT;
            if (!vkCmdSetRasterizerDiscardEnable && vkCmdSetRasterizerDiscardEnableEXT) vkCmdSetRasterizerDiscardEnable = vkCmdSetRasterizerDiscardEnableEXT;
            if (!vkCmdSetScissorWithCount && vkCmdSetScissorWithCountEXT) vkCmdSetScissorWithCount = vkCmdSetScissorWithCountEXT;
            if (!vkCmdSetStencilOp && vkCmdSetStencilOpEXT) vkCmdSetStencilOp = vkCmdSetStencilOpEXT;
            if (!vkCmdSetStencilTestEnable && vkCmdSetStencilTestEnableEXT) vkCmdSetStencilTestEnable = vkCmdSetStencilTestEnableEXT;
            if (!vkCmdSetViewportWithCount && vkCmdSetViewportWithCountEXT) vkCmdSetViewportWithCount = vkCmdSetViewportWithCountEXT;
            if (!vkCmdWaitEvents2 && vkCmdWaitEvents2KHR) vkCmdWaitEvents2 = vkCmdWaitEvents2KHR;
            if (!vkCmdWriteTimestamp2 && vkCmdWriteTimestamp2KHR) vkCmdWriteTimestamp2 = vkCmdWriteTimestamp2KHR;
            if (!vkCreatePrivateDataSlot && vkCreatePrivateDataSlotEXT) vkCreatePrivateDataSlot = vkCreatePrivateDataSlotEXT;
            if (!vkDestroyPrivateDataSlot && vkDestroyPrivateDataSlotEXT) vkDestroyPrivateDataSlot = vkDestroyPrivateDataSlotEXT;
            if (!vkGetDeviceBufferMemoryRequirements && vkGetDeviceBufferMemoryRequirementsKHR) vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirementsKHR;
            if (!vkGetDeviceImageMemoryRequirements && vkGetDeviceImageMemoryRequirementsKHR) vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirementsKHR;
            if (!vkGetDeviceImageSparseMemoryRequirements && vkGetDeviceImageSparseMemoryRequirementsKHR) vkGetDeviceImageSparseMemoryRequirements = vkGetDeviceImageSparseMemoryRequirementsKHR;
            if (!vkGetPrivateData && vkGetPrivateDataEXT) vkGetPrivateData = vkGetPrivateDataEXT;
            if (!vkQueueSubmit2 && vkQueueSubmit2KHR) vkQueueSubmit2 = vkQueueSubmit2KHR;
            if (!vkSetPrivateData && vkSetPrivateDataEXT) vkSetPrivateData = vkSetPrivateDataEXT;

            // ----- Vulkan 1.4 promoted (from KHR/EXT) -----
            if (!vkCmdBindDescriptorSets2 && vkCmdBindDescriptorSets2KHR) vkCmdBindDescriptorSets2 = vkCmdBindDescriptorSets2KHR;
            if (!vkCmdBindIndexBuffer2 && vkCmdBindIndexBuffer2KHR) vkCmdBindIndexBuffer2 = vkCmdBindIndexBuffer2KHR;
            if (!vkCmdPushConstants2 && vkCmdPushConstants2KHR) vkCmdPushConstants2 = vkCmdPushConstants2KHR;
            if (!vkCmdPushDescriptorSet && vkCmdPushDescriptorSetKHR) vkCmdPushDescriptorSet = vkCmdPushDescriptorSetKHR;
            if (!vkCmdPushDescriptorSetWithTemplate && vkCmdPushDescriptorSetWithTemplateKHR) vkCmdPushDescriptorSetWithTemplate = vkCmdPushDescriptorSetWithTemplateKHR;
            if (!vkCmdSetLineStipple && vkCmdSetLineStippleKHR) vkCmdSetLineStipple = vkCmdSetLineStippleKHR;
            if (!vkCmdSetRenderingAttachmentLocations && vkCmdSetRenderingAttachmentLocationsKHR) vkCmdSetRenderingAttachmentLocations = vkCmdSetRenderingAttachmentLocationsKHR;
            if (!vkCmdSetRenderingInputAttachmentIndices && vkCmdSetRenderingInputAttachmentIndicesKHR) vkCmdSetRenderingInputAttachmentIndices = vkCmdSetRenderingInputAttachmentIndicesKHR;
            if (!vkCopyImageToImage && vkCopyImageToImageEXT) vkCopyImageToImage = vkCopyImageToImageEXT;
            if (!vkCopyImageToMemory && vkCopyImageToMemoryEXT) vkCopyImageToMemory = vkCopyImageToMemoryEXT;
            if (!vkCopyMemoryToImage && vkCopyMemoryToImageEXT) vkCopyMemoryToImage = vkCopyMemoryToImageEXT;
            if (!vkGetDeviceImageSubresourceLayout && vkGetDeviceImageSubresourceLayoutKHR) vkGetDeviceImageSubresourceLayout = vkGetDeviceImageSubresourceLayoutKHR;
            if (!vkGetImageSubresourceLayout2 && vkGetImageSubresourceLayout2KHR) vkGetImageSubresourceLayout2 = vkGetImageSubresourceLayout2KHR;
            if (!vkGetRenderingAreaGranularity && vkGetRenderingAreaGranularityKHR) vkGetRenderingAreaGranularity = vkGetRenderingAreaGranularityKHR;
            if (!vkMapMemory2 && vkMapMemory2KHR) vkMapMemory2 = vkMapMemory2KHR;
            if (!vkTransitionImageLayout && vkTransitionImageLayoutEXT) vkTransitionImageLayout = vkTransitionImageLayoutEXT;
            if (!vkUnmapMemory2 && vkUnmapMemory2KHR) vkUnmapMemory2 = vkUnmapMemory2KHR;
            // clang-format on
        }
    }  // namespace

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
                MIKI_LOG_FLUSH();
                auto trace = ::miki::debug::StackTrace::Capture(1);
                trace.PrintColored("Vulkan Validation Error");
            } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
                MIKI_LOG_WARN(Rhi, "[Vulkan] {}", callbackData->pMessage);
                MIKI_LOG_FLUSH();
                auto trace = ::miki::debug::StackTrace::Capture(1);
                trace.PrintColored("Vulkan Validation Warning");
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

        // Enable synchronization validation for debugging semaphore/fence issues
        VkValidationFeatureEnableEXT enabledFeatures[] = {
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        };
        VkValidationFeaturesEXT validationFeatures{};
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.enabledValidationFeatureCount = desc.enableValidation ? 1 : 0;
        validationFeatures.pEnabledValidationFeatures = enabledFeatures;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pNext = desc.enableValidation ? &validationFeatures : nullptr;
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

        uint32_t computeOnlyFamilyCount = 0;
        uint32_t computeFamilyQueueCount = 0;
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            auto flags = queueFamilies[i].queueFlags;

            if ((flags & VK_QUEUE_GRAPHICS_BIT) && queueFamilies_.graphics == UINT32_MAX) {
                queueFamilies_.graphics = i;
                queueFamilies_.present = i;
            }
            if ((flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT)) {
                ++computeOnlyFamilyCount;
                if (queueFamilies_.compute == UINT32_MAX) {
                    queueFamilies_.compute = i;
                    computeFamilyQueueCount = queueFamilies[i].queueCount;
                }
            }
            if ((flags & VK_QUEUE_TRANSFER_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && !(flags & VK_QUEUE_COMPUTE_BIT)
                && queueFamilies_.transfer == UINT32_MAX) {
                queueFamilies_.transfer = i;
            }
        }
        computeOnlyFamilyCount_ = computeOnlyFamilyCount;
        computeFamilyQueueCount_ = computeFamilyQueueCount;

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
        using enum ::miki::debug::LogCategory;

        // -- Determine dual-queue eligibility for async compute (Level A) --
        bool canDualComputeQueue
            = (queueFamilies_.compute != queueFamilies_.graphics) && (computeFamilyQueueCount_ >= 2);

        std::vector<uint32_t> uniqueFamilies = {queueFamilies_.graphics};
        auto addUnique = [&](uint32_t idx) {
            if (idx != UINT32_MAX && std::ranges::find(uniqueFamilies, idx) == uniqueFamilies.end()) {
                uniqueFamilies.push_back(idx);
            }
        };
        addUnique(queueFamilies_.compute);
        addUnique(queueFamilies_.transfer);
        addUnique(queueFamilies_.present);

        // Priority arrays: dual compute gets {1.0f (frame-sync), kAsyncComputePriority (async)}
        static constexpr float kAsyncComputePriority = 0.5f;
        float singlePriority = 1.0f;
        std::array<float, 2> dualComputePriorities = {1.0f, kAsyncComputePriority};

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueFamilies.size());
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            if (family == queueFamilies_.compute && canDualComputeQueue) {
                queueInfo.queueCount = 2;
                queueInfo.pQueuePriorities = dualComputePriorities.data();
            } else {
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &singlePriority;
            }
            queueCreateInfos.push_back(queueInfo);
        }

        // =====================================================================
        // Step 1: Enumerate available extensions (needed for tier decision + feature enabling)
        // =====================================================================
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> availableExts(extCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, availableExts.data());

        auto hasExt = [&](const char* name) {
            return std::ranges::any_of(availableExts, [name](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, name) == 0;
            });
        };

        // Extension list — swapchain is always required
        std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        // Global priority extension (for ComputeQueueLevel A/B)
        bool hasGlobalPriorityExt = false;
        if (hasExt("VK_KHR_global_priority")) {
            deviceExtensions.push_back("VK_KHR_global_priority");
            hasGlobalPriorityExt = true;
        } else if (hasExt("VK_EXT_global_priority")) {
            deviceExtensions.push_back("VK_EXT_global_priority");
            hasGlobalPriorityExt = true;
        }

        // =====================================================================
        // Step 2: Probe physical device features + API version for tier decision
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

        VkPhysicalDeviceProperties devProps{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &devProps);
        bool has14Api
            = VK_API_VERSION_MAJOR(devProps.apiVersion) >= 1 && VK_API_VERSION_MINOR(devProps.apiVersion) >= 4;

        // =====================================================================
        // Step 3: Tier downgrade check (Vulkan14 → VulkanCompat if missing critical features)
        // =====================================================================
        if (tier_ == BackendType::Vulkan14) {
            using enum ::miki::debug::LogCategory;
            bool missingCritical = false;
            auto check = [&](VkBool32 supported, const char* name) {
                if (!supported) {
                    MIKI_LOG_WARN(Rhi, "[Vulkan] Tier1 feature '{}' not supported, will downgrade to Compat", name);
                    missingCritical = true;
                }
            };
            if (!has14Api) {
                MIKI_LOG_WARN(
                    Rhi, "[Vulkan] GPU reports Vulkan {}.{}, need 1.4 — will downgrade to Compat",
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
                MIKI_LOG_WARN(Rhi, "[Vulkan] Downgrading from Vulkan14 to VulkanCompat");
                tier_ = BackendType::VulkanCompat;
            }
        }

        // =====================================================================
        // Step 4: Build feature request chain based on final tier
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
        features2.features.textureCompressionASTC_LDR = supported2.features.textureCompressionASTC_LDR;

        // Sparse binding + residency (optional, both tiers)
        features2.features.sparseBinding = supported2.features.sparseBinding;
        features2.features.sparseResidencyBuffer = supported2.features.sparseResidencyBuffer;
        features2.features.sparseResidencyImage2D = supported2.features.sparseResidencyImage2D;
        features2.features.sparseResidencyImage3D = supported2.features.sparseResidencyImage3D;

        // Feature structs — declared here, conditionally initialized below
        VkPhysicalDeviceVulkan11Features features11{};
        VkPhysicalDeviceVulkan12Features features12{};
        VkPhysicalDeviceVulkan13Features features13{};
        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynRenderFeature{};
        VkPhysicalDeviceSynchronization2FeaturesKHR sync2Feature{};
        VkPhysicalDeviceShaderDrawParametersFeatures drawParamsFeature{};
        VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeature{};
        VkPhysicalDeviceTextureCompressionASTCHDRFeaturesEXT astcHdrFeature{};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeature{};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{};
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeature{};

        if (tier_ == BackendType::Vulkan14) {
            // -----------------------------------------------------------------
            // Tier1: Vulkan 1.4 core — features are promoted, no KHR extensions needed
            // -----------------------------------------------------------------
            features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            features11.shaderDrawParameters = VK_TRUE;

            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features12.pNext = &features11;
            features12.descriptorIndexing = VK_TRUE;
            features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
            features12.descriptorBindingPartiallyBound = VK_TRUE;
            features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
            features12.runtimeDescriptorArray = VK_TRUE;
            // UpdateAfterBind per descriptor type — required by CreateDescriptorLayoutImpl
            // which sets VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT on all bindings
            features12.descriptorBindingSampledImageUpdateAfterBind
                = supported12.descriptorBindingSampledImageUpdateAfterBind;
            features12.descriptorBindingStorageImageUpdateAfterBind
                = supported12.descriptorBindingStorageImageUpdateAfterBind;
            features12.descriptorBindingStorageBufferUpdateAfterBind
                = supported12.descriptorBindingStorageBufferUpdateAfterBind;
            features12.descriptorBindingUniformTexelBufferUpdateAfterBind
                = supported12.descriptorBindingUniformTexelBufferUpdateAfterBind;
            features12.descriptorBindingStorageTexelBufferUpdateAfterBind
                = supported12.descriptorBindingStorageTexelBufferUpdateAfterBind;
            features12.timelineSemaphore = VK_TRUE;
            features12.bufferDeviceAddress = VK_TRUE;

            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            features13.pNext = &features12;
            features13.synchronization2 = VK_TRUE;
            features13.dynamicRendering = VK_TRUE;
            features13.maintenance4 = VK_TRUE;
            // ASTC HDR — promoted to core 1.3, enable if GPU supports it
            features13.textureCompressionASTC_HDR = supported13.textureCompressionASTC_HDR;

            // Optional Tier1 extensions — append to end of chain (features11.pNext)
            void** pNextTail = &features11.pNext;

            if (hasExt(VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
                meshShaderFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
                meshShaderFeature.taskShader = VK_TRUE;
                meshShaderFeature.meshShader = VK_TRUE;
                *pNextTail = &meshShaderFeature;
                pNextTail = &meshShaderFeature.pNext;
            }

            // Ray tracing extensions (VK_KHR_acceleration_structure requires VK_KHR_deferred_host_operations)
            if (hasExt(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                && hasExt(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
                deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
                accelStructFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
                accelStructFeature.accelerationStructure = VK_TRUE;
                *pNextTail = &accelStructFeature;
                pNextTail = &accelStructFeature.pNext;

                if (hasExt(VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
                    deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
                    rayQueryFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
                    rayQueryFeature.rayQuery = VK_TRUE;
                    *pNextTail = &rayQueryFeature;
                    pNextTail = &rayQueryFeature.pNext;
                }
                if (hasExt(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
                    deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
                    rtPipelineFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
                    rtPipelineFeature.rayTracingPipeline = VK_TRUE;
                    *pNextTail = &rtPipelineFeature;
                    pNextTail = &rtPipelineFeature.pNext;
                }
            }

            // Chain: features2 -> features13 -> features12 -> features11 -> [optional extensions]
            features2.pNext = &features13;
            features2.features.shaderFloat64 = supported2.features.shaderFloat64;
            features2.features.shaderInt64 = supported2.features.shaderInt64;

        } else {
            // -----------------------------------------------------------------
            // Tier2: VulkanCompat — Vulkan 1.1 core + KHR extensions
            // Sync model: native timeline (if VK_KHR_timeline_semaphore) or CPU-emulated
            // -----------------------------------------------------------------
            if (!hasExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
                MIKI_LOG_ERROR(Rhi, "[Vulkan] VulkanCompat requires VK_KHR_dynamic_rendering; use OpenGL (Tier4)");
                return std::unexpected(RhiError::FeatureNotSupported);
            }

            // Mandatory: dynamic rendering
            deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            dynRenderFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
            dynRenderFeature.dynamicRendering = VK_TRUE;
            features2.pNext = &dynRenderFeature;
            void** pNextTail = &dynRenderFeature.pNext;

            // Dependency chain for dynamic_rendering
            if (hasExt(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
            }
            if (hasExt(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
            }

            // Optional: timeline semaphore (enables unified timeline sync model for Tier2)
            VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timelineSemFeature{};
            if (hasExt(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
                timelineSemFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
                timelineSemFeature.timelineSemaphore = VK_TRUE;
                *pNextTail = &timelineSemFeature;
                pNextTail = &timelineSemFeature.pNext;
            }

            // Optional extensions with feature structs
            if (hasExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
                sync2Feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
                sync2Feature.synchronization2 = VK_TRUE;
                *pNextTail = &sync2Feature;
                pNextTail = &sync2Feature.pNext;
            }
            if (hasExt(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);
                drawParamsFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
                drawParamsFeature.shaderDrawParameters = VK_TRUE;
                *pNextTail = &drawParamsFeature;
                pNextTail = &drawParamsFeature.pNext;
            }

            // Optional: ASTC HDR (VK_EXT_texture_compression_astc_hdr, pre-1.3 path)
            if (hasExt(VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME)) {
                deviceExtensions.push_back(VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME);
                astcHdrFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES_EXT;
                astcHdrFeature.textureCompressionASTC_HDR = VK_TRUE;
                *pNextTail = &astcHdrFeature;
                pNextTail = &astcHdrFeature.pNext;
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
        volkPatchPromotedExtensions();

        vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
        if (queueFamilies_.compute != queueFamilies_.graphics) {
            vkGetDeviceQueue(device_, queueFamilies_.compute, 0, &computeQueue_);
            if (canDualComputeQueue && hasGlobalPriorityExt) {
                // Level A: queue[0] = frame-sync (high priority), queue[1] = async (low priority)
                vkGetDeviceQueue(device_, queueFamilies_.compute, 1, &computeAsyncQueue_);
            } else {
                computeAsyncQueue_ = computeQueue_;
            }
        } else {
            computeQueue_ = graphicsQueue_;
            computeAsyncQueue_ = graphicsQueue_;
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
        // Level A: asyncCompute gets its own timeline semaphore (separate VkQueue, independent scheduling)
        // Level B/C/D: alias compute semaphore (same VkQueue, shared FIFO ordering)
        if (computeAsyncQueue_ != VK_NULL_HANDLE && computeAsyncQueue_ != computeQueue_) {
            if (auto r = createAndRegister(queueTimelines_.asyncCompute); !r) {
                return r;
            }
        } else {
            queueTimelines_.asyncCompute = queueTimelines_.compute;
        }

        // Name the timeline semaphores via the non-invasive debug API
        SetObjectDebugNameImpl(queueTimelines_.graphics, "GraphicsTimeline");
        SetObjectDebugNameImpl(queueTimelines_.compute, "ComputeTimeline");
        SetObjectDebugNameImpl(queueTimelines_.transfer, "TransferTimeline");
        if (queueTimelines_.asyncCompute.value != queueTimelines_.compute.value) {
            SetObjectDebugNameImpl(queueTimelines_.asyncCompute, "AsyncComputeTimeline");
        }

        graphicsTimelineValue_ = 0;
        computeTimelineValue_ = 0;
        transferTimelineValue_ = 0;
        return {};
    }

    // =========================================================================
    // Emulated timeline semaphores (Tier2 without VK_KHR_timeline_semaphore)
    // Uses binary VkSemaphores + CPU-side counters. SignalSemaphoreImpl/WaitSemaphoreImpl
    // on these handles are CPU-only value updates (no GPU timeline), matching OpenGL/WebGPU.
    // =========================================================================

    void VulkanDevice::CreateEmulatedTimelineSemaphores() {
        using enum ::miki::debug::LogCategory;
        MIKI_LOG_WARN(Rhi, "[Vulkan] Timeline semaphore not available; using CPU-emulated timelines (Tier2 compat)");

        auto createEmulated = [&](SemaphoreHandle& outHandle) {
            auto [handle, data] = semaphores_.Allocate();
            if (!data) {
                MIKI_LOG_ERROR(Rhi, "[Vulkan] Failed to allocate emulated timeline semaphore");
                return;
            }
            data->semaphore = VK_NULL_HANDLE;  // no GPU semaphore — CPU-only counter
            data->type = SemaphoreType::Timeline;
            outHandle = handle;
        };

        createEmulated(queueTimelines_.graphics);
        createEmulated(queueTimelines_.compute);
        createEmulated(queueTimelines_.transfer);
        // Tier2 compat: no dual compute queue, always alias
        queueTimelines_.asyncCompute = queueTimelines_.compute;

        graphicsTimelineValue_ = 0;
        computeTimelineValue_ = 0;
        transferTimelineValue_ = 0;
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
        capabilities_.computeQueueFamilyCount = computeOnlyFamilyCount_;
        capabilities_.hasMultiDrawIndirect = (deviceFeatures.multiDrawIndirect == VK_TRUE);
        capabilities_.hasSubgroupOps = true;   // Vulkan 1.1+ always has subgroup ops
        capabilities_.hasSpirvShaders = true;  // Vulkan always consumes SPIR-V
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

        // Sparse binding + residency
        capabilities_.hasSparseBinding = (deviceFeatures.sparseBinding == VK_TRUE);
        capabilities_.hasSparseResidencyBuffer = (deviceFeatures.sparseResidencyBuffer == VK_TRUE);
        capabilities_.hasSparseResidencyImage2D = (deviceFeatures.sparseResidencyImage2D == VK_TRUE);
        capabilities_.hasSparseResidencyImage3D = (deviceFeatures.sparseResidencyImage3D == VK_TRUE);
        capabilities_.hasStandardSparseBlockShape = (props.sparseProperties.residencyStandard2DBlockShape == VK_TRUE);
        if (capabilities_.hasSparseBinding) {
            capabilities_.enabledFeatures.Add(DeviceFeature::SparseBinding);
        }
        if (capabilities_.hasSparseResidencyBuffer || capabilities_.hasSparseResidencyImage2D) {
            capabilities_.enabledFeatures.Add(DeviceFeature::SparseResidency);
        }

        // ASTC HDR detection
        if (isTier1) {
            VkPhysicalDeviceVulkan13Features features13Query{};
            features13Query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceFeatures2 features2Query{};
            features2Query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2Query.pNext = &features13Query;
            vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2Query);
            capabilities_.hasTextureCompressionASTC_HDR = (features13Query.textureCompressionASTC_HDR == VK_TRUE);
        } else {
            capabilities_.hasTextureCompressionASTC_HDR = hasExt(VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME);
        }
        if (capabilities_.hasTextureCompressionASTC_HDR) {
            capabilities_.enabledFeatures.Add(DeviceFeature::TextureCompressionASTC_HDR);
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

        // Global priority (VK_KHR_global_priority promoted in Vulkan 1.4, check both KHR and EXT)
        capabilities_.hasGlobalPriority
            = hasExt(VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME) || hasExt(VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME);

        MIKI_LOG_INFO(
            Rhi, "[Vulkan] Capability tier: {} (backend: {}, GPU: {})", isTier1 ? "Tier1_Vulkan" : "Tier2_Compat",
            isTier1 ? "Vulkan14" : "VulkanCompat", capabilities_.deviceName
        );
        MIKI_LOG_INFO(
            Rhi,
            "[Vulkan] Sparse: binding={}, residencyBuffer={}, residencyImage2D={}, residencyImage3D={}, "
            "standardBlockShape={}",
            capabilities_.hasSparseBinding, capabilities_.hasSparseResidencyBuffer,
            capabilities_.hasSparseResidencyImage2D, capabilities_.hasSparseResidencyImage3D,
            capabilities_.hasStandardSparseBlockShape
        );
        MIKI_LOG_INFO(Rhi, "[Vulkan] ASTC HDR: {}", capabilities_.hasTextureCompressionASTC_HDR);

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

        // Ray tracing (VK_KHR_acceleration_structure requires VK_KHR_deferred_host_operations)
        if (hasExt(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
            && hasExt(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
            capabilities_.hasAccelerationStructure = true;
            capabilities_.enabledFeatures.Add(DeviceFeature::AccelerationStructure);

            if (hasExt(VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
                capabilities_.hasRayQuery = true;
                capabilities_.enabledFeatures.Add(DeviceFeature::RayQuery);
            }
            if (hasExt(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
                capabilities_.hasRayTracingPipeline = true;
                capabilities_.enabledFeatures.Add(DeviceFeature::RayTracingPipeline);
            }
        }

        // Variable rate shading
        if (hasExt(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) {
            capabilities_.hasVariableRateShading = true;
            capabilities_.enabledFeatures.Add(DeviceFeature::VariableRateShading);
        }

        // Graphics pipeline library (split compilation)
        if (hasExt(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME)) {
            capabilities_.hasGraphicsPipelineLibrary = true;
            capabilities_.enabledFeatures.Add(DeviceFeature::GraphicsPipelineLibrary);
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
            VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK,     // ASTC_4x4_HDR
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

        PopulateCapabilities();

        // Create timeline semaphores for any tier that supports them (native).
        // Tier2 with VK_KHR_timeline_semaphore gets native timelines just like Tier1.
        // Tier2 without the extension gets CPU-emulated timelines (see CreateEmulatedTimelineSemaphores).
        if (capabilities_.hasTimelineSemaphore) {
            auto r5 = CreateTimelineSemaphores();
            if (!r5) {
                return r5;
            }
        } else {
            CreateEmulatedTimelineSemaphores();
        }

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
        // Only destroy asyncCompute if it's a separate semaphore (Level A), not aliased to compute
        if (queueTimelines_.asyncCompute.IsValid()
            && queueTimelines_.asyncCompute.value != queueTimelines_.compute.value) {
            DestroySemaphoreImpl(queueTimelines_.asyncCompute);
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
            if (data->semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, data->semaphore, nullptr);
            }
            semaphores_.Free(h);
        }
    }

    void VulkanDevice::SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value) {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->semaphore == VK_NULL_HANDLE) {
            return;  // emulated timeline — CPU-only, no GPU signal
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
        if (data->semaphore == VK_NULL_HANDLE) {
            return;  // emulated timeline — CPU-only, no GPU wait
        }

        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &data->semaphore;
        waitInfo.pValues = &value;
        vkWaitSemaphores(device_, &waitInfo, timeout);
    }

    // TODO (Nekomiya) maybe we should return RhiResult to flag invalid semaphore
    auto VulkanDevice::GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return 0;
        }
        if (data->semaphore == VK_NULL_HANDLE) {
            return 0;  // emulated timeline — value tracked by SyncScheduler, not queryable via GPU
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
        // Select target queue and corresponding mutex.
        // When compute/transfer fall back to the graphics queue, we must lock the graphics mutex
        // to avoid concurrent vkQueueSubmit2 on the same VkQueue (Vulkan spec §3.3.1).
        VkQueue targetQueue = graphicsQueue_;
        std::mutex* queueMutex = &graphicsQueueMutex_;
        if (queue == QueueType::Compute) {
            targetQueue = computeQueue_;
            queueMutex = (computeQueue_ == graphicsQueue_) ? &graphicsQueueMutex_ : &computeQueueMutex_;
        } else if (queue == QueueType::AsyncCompute) {
            // Level A: separate VkQueue with NORMAL priority; Level B/C/D: same as computeQueue_
            targetQueue = computeAsyncQueue_;
            queueMutex = (computeAsyncQueue_ == computeQueue_)
                             ? ((computeQueue_ == graphicsQueue_) ? &graphicsQueueMutex_ : &computeQueueMutex_)
                             : &computeAsyncQueueMutex_;
        } else if (queue == QueueType::Transfer) {
            targetQueue = transferQueue_;
            queueMutex = (transferQueue_ == graphicsQueue_) ? &graphicsQueueMutex_ : &transferQueueMutex_;
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

        // Marshal wait semaphores (skip emulated timeline handles with VK_NULL_HANDLE)
        struct SemaphoreEntry {
            VkSemaphoreSubmitInfo info;
            const char* name;
        };
        std::vector<SemaphoreEntry> waitEntries;
        waitEntries.reserve(desc.waitSemaphores.size());
        for (auto& w : desc.waitSemaphores) {
            auto* semData = semaphores_.Lookup(w.semaphore);
            if (!semData || semData->semaphore == VK_NULL_HANDLE) {
                continue;
            }
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = semData->semaphore;
            info.value = w.value;
            info.stageMask = static_cast<VkPipelineStageFlags2>(w.stageMask);
            waitEntries.push_back({info, semaphores_.GetDebugName(w.semaphore)});
        }

        // Marshal signal semaphores (skip emulated timeline handles with VK_NULL_HANDLE)
        std::vector<SemaphoreEntry> signalEntries;
        signalEntries.reserve(desc.signalSemaphores.size());
        for (auto& s : desc.signalSemaphores) {
            auto* semData = semaphores_.Lookup(s.semaphore);
            if (!semData || semData->semaphore == VK_NULL_HANDLE) {
                continue;
            }
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = semData->semaphore;
            info.value = s.value;
            info.stageMask = static_cast<VkPipelineStageFlags2>(s.stageMask);
            signalEntries.push_back({info, semaphores_.GetDebugName(s.semaphore)});
        }

        // Build flat arrays for Vulkan
        std::vector<VkSemaphoreSubmitInfo> waitInfos;
        waitInfos.reserve(waitEntries.size());
        for (auto& e : waitEntries) waitInfos.push_back(e.info);

        std::vector<VkSemaphoreSubmitInfo> signalInfos;
        signalInfos.reserve(signalEntries.size());
        for (auto& e : signalEntries) signalInfos.push_back(e.info);

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

        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync][Vk] SubmitImpl: queue={} cmds=[{}] waits=[{}] signals=[{}] fence=[0x{:x}]",
            ToString(queue), cmdInfos.size(), waitInfos.size(), signalInfos.size(),
            reinterpret_cast<uintptr_t>(vkFence)
        );
        for (auto& e : waitEntries) {
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync][Vk]   wait: \"{}\" value=[{}] stage=[0x{:x}]",
                e.name ? e.name : "(unnamed)", e.info.value, static_cast<uint64_t>(e.info.stageMask)
            );
        }
        for (auto& e : signalEntries) {
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync][Vk]   signal: \"{}\" value=[{}] stage=[0x{:x}]",
                e.name ? e.name : "(unnamed)", e.info.value, static_cast<uint64_t>(e.info.stageMask)
            );
        }

        {
            std::lock_guard lock(*queueMutex);
            VkResult submitResult = vkQueueSubmit2(targetQueue, 1, &submitInfo, vkFence);
            if (submitResult != VK_SUCCESS) {
                MIKI_LOG_ERROR(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync][Vk] vkQueueSubmit2 FAILED: result={}", static_cast<int>(submitResult)
                );
            }
        }
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

        auto outSize = static_cast<uint32_t>(out.size());
        uint32_t count = (outSize < memProps.memoryHeapCount) ? outSize : memProps.memoryHeapCount;
        for (uint32_t i = 0; i < count; ++i) {
            out[i].heapIndex = i;
            out[i].budgetBytes = budgets[i].budget;
            out[i].usageBytes = budgets[i].usage;
            out[i].isDeviceLocal = (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        }
        return count;
    }

    // =========================================================================
    // Debug names — non-invasive, HandlePool-based
    // =========================================================================

    namespace {

        void TagVkObject(
            VkDevice device, VkObjectType objectType, uint64_t objectHandle, const char* name
        ) {
            if (name && vkSetDebugUtilsObjectNameEXT) {
                VkDebugUtilsObjectNameInfoEXT nameInfo{};
                nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                nameInfo.objectType = objectType;
                nameInfo.objectHandle = objectHandle;
                nameInfo.pObjectName = name;
                vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
            }
        }

    }  // anonymous namespace

    void VulkanDevice::SetObjectDebugNameImpl(SemaphoreHandle h, const char* name) {
        semaphores_.SetDebugName(h, name);
        if (auto* data = semaphores_.Lookup(h)) {
            TagVkObject(device_, VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(data->semaphore), name);
        }
        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync][Vk] SetDebugName: semaphore \"{}\" handle=[0x{:x}]",
            name ? name : "(null)", h.value
        );
    }

    void VulkanDevice::SetObjectDebugNameImpl(BufferHandle h, const char* name) {
        buffers_.SetDebugName(h, name);
        if (auto* data = buffers_.Lookup(h)) {
            TagVkObject(device_, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(data->buffer), name);
        }
    }

    void VulkanDevice::SetObjectDebugNameImpl(TextureHandle h, const char* name) {
        textures_.SetDebugName(h, name);
        if (auto* data = textures_.Lookup(h)) {
            TagVkObject(device_, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(data->image), name);
        }
    }

    void VulkanDevice::SetObjectDebugNameImpl(PipelineHandle h, const char* name) {
        pipelines_.SetDebugName(h, name);
        if (auto* data = pipelines_.Lookup(h)) {
            TagVkObject(device_, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(data->pipeline), name);
        }
    }

    auto VulkanDevice::GetDebugNameImpl(SemaphoreHandle h) const -> const char* {
        auto name = semaphores_.GetDebugName(h);
        return name ? name : "(unnamed)";
    }

}  // namespace miki::rhi
