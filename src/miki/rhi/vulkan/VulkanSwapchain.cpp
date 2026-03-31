/** @file VulkanSwapchain.cpp
 *  @brief Vulkan 1.4 backend — swapchain creation, resize, acquire, present.
 */

#include "miki/rhi/backend/VulkanDevice.h"

#include "miki/debug/StructuredLogger.h"

#include <algorithm>
#include <limits>

namespace miki::rhi {

    // =========================================================================
    // Format conversion helpers
    // =========================================================================

    static auto ToVkFormat(Format fmt) -> VkFormat {
        switch (fmt) {
            case Format::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
            case Format::BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
            case Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
            case Format::RGBA16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case Format::RGB10A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            default: return VK_FORMAT_B8G8R8A8_SRGB;
        }
    }

    static auto ToVkPresentMode(PresentMode mode) -> VkPresentModeKHR {
        switch (mode) {
            case PresentMode::Immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
            case PresentMode::Mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
            case PresentMode::Fifo: return VK_PRESENT_MODE_FIFO_KHR;
            case PresentMode::FifoRelaxed: return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    static auto ToVkColorSpace(SurfaceColorSpace cs) -> VkColorSpaceKHR {
        switch (cs) {
            case SurfaceColorSpace::SRGB: return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            case SurfaceColorSpace::HDR10_ST2084: return VK_COLOR_SPACE_HDR10_ST2084_EXT;
            case SurfaceColorSpace::scRGBLinear: return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        }
        return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }

    // =========================================================================
    // Surface creation from NativeWindowHandle variant
    // =========================================================================

    static auto CreateVkSurface(VkInstance instance, const NativeWindowHandle& window) -> VkSurfaceKHR {
        VkSurfaceKHR surface = VK_NULL_HANDLE;

        auto visitor = [&](auto&& win) -> void {
            using T = std::decay_t<decltype(win)>;

            if constexpr (std::is_same_v<T, Win32Window>) {
#ifdef VK_USE_PLATFORM_WIN32_KHR
                VkWin32SurfaceCreateInfoKHR ci{};
                ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                ci.hwnd = static_cast<HWND>(win.hwnd);
                ci.hinstance = static_cast<HINSTANCE>(win.hinstance);
                vkCreateWin32SurfaceKHR(instance, &ci, nullptr, &surface);
#endif
            } else if constexpr (std::is_same_v<T, X11Window>) {
#ifdef VK_USE_PLATFORM_XCB_KHR
                VkXcbSurfaceCreateInfoKHR ci{};
                ci.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
                ci.connection = static_cast<xcb_connection_t*>(win.display);
                ci.window = static_cast<xcb_window_t>(win.window);
                vkCreateXcbSurfaceKHR(instance, &ci, nullptr, &surface);
#endif
            } else if constexpr (std::is_same_v<T, WaylandWindow>) {
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
                VkWaylandSurfaceCreateInfoKHR ci{};
                ci.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
                ci.display = static_cast<wl_display*>(win.display);
                ci.surface = static_cast<wl_surface*>(win.surface);
                vkCreateWaylandSurfaceKHR(instance, &ci, nullptr, &surface);
#endif
            } else {
                // CocoaWindow and EmscriptenCanvas: not supported for Vulkan backend
                (void)win;
            }
        };
        std::visit(visitor, window);

        return surface;
    }

    // =========================================================================
    // CreateSwapchainImpl
    // =========================================================================

    auto VulkanDevice::CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle> {
        // Create VkSurfaceKHR from the NativeWindowHandle variant
        VkSurfaceKHR surface = CreateVkSurface(instance_, desc.surface);
        if (surface == VK_NULL_HANDLE) {
            return std::unexpected(RhiError::DeviceLost);
        }

        // Verify present support
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, queueFamilies_.present, surface, &presentSupport);
        if (!presentSupport) {
            vkDestroySurfaceKHR(instance_, surface, nullptr);
            return std::unexpected(RhiError::DeviceLost);
        }

        // Query surface capabilities
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface, &capabilities);

        // Resolve extent
        VkExtent2D extent;
        if (capabilities.currentExtent.width != UINT32_MAX) {
            extent = capabilities.currentExtent;
        } else {
            extent.width = std::clamp(desc.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height
                = std::clamp(desc.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        // Resolve image count
        uint32_t imageCount = std::max(desc.imageCount, capabilities.minImageCount);
        if (capabilities.maxImageCount > 0) {
            imageCount = std::min(imageCount, capabilities.maxImageCount);
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = ToVkFormat(desc.preferredFormat);
        createInfo.imageColorSpace = ToVkColorSpace(desc.colorSpace);
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        // Queue family sharing
        uint32_t queueFamilyIndices[] = {queueFamilies_.graphics, queueFamilies_.present};
        if (queueFamilies_.graphics != queueFamilies_.present) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = ToVkPresentMode(desc.presentMode);
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        VkSwapchainKHR swapchain;
        VkResult result = vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain);
        if (result != VK_SUCCESS) {
            vkDestroySurfaceKHR(instance_, surface, nullptr);
            return std::unexpected(RhiError::DeviceLost);
        }

        // Get swapchain images
        uint32_t actualImageCount = 0;
        vkGetSwapchainImagesKHR(device_, swapchain, &actualImageCount, nullptr);
        std::vector<VkImage> images(actualImageCount);
        vkGetSwapchainImagesKHR(device_, swapchain, &actualImageCount, images.data());

        // Register swapchain images in textures_ pool
        std::vector<TextureHandle> textureHandles;
        textureHandles.reserve(actualImageCount);
        for (uint32_t i = 0; i < actualImageCount; ++i) {
            auto [texHandle, texData] = textures_.Allocate();
            if (!texData) {
                // Rollback previously allocated texture handles
                for (auto& th : textureHandles) {
                    auto slotIdx = textures_.MarkDead(th);
                    textures_.Reclaim(slotIdx);
                }
                vkDestroySwapchainKHR(device_, swapchain, nullptr);
                vkDestroySurfaceKHR(instance_, surface, nullptr);
                return std::unexpected(RhiError::TooManyObjects);
            }
            texData->image = images[i];
            texData->allocation = nullptr;  // Not VMA-managed
            texData->format = createInfo.imageFormat;
            texData->extent = {extent.width, extent.height, 1};
            texData->mipLevels = 1;
            texData->arrayLayers = 1;
            texData->dimension = TextureDimension::Tex2D;  // Swapchain images are always 2D
            texData->ownsImage = false;                    // Swapchain owns the image
            textureHandles.push_back(texHandle);
        }

        auto [handle, data] = swapchains_.Allocate();
        if (!data) {
            // Rollback texture handles
            for (auto& th : textureHandles) {
                auto slotIdx = textures_.MarkDead(th);
                textures_.Reclaim(slotIdx);
            }
            vkDestroySwapchainKHR(device_, swapchain, nullptr);
            vkDestroySurfaceKHR(instance_, surface, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->swapchain = swapchain;
        data->surface = surface;
        data->images = std::move(images);
        data->textureHandles = std::move(textureHandles);
        data->format = createInfo.imageFormat;
        data->extent = extent;

        return handle;
    }

    // =========================================================================
    // DestroySwapchainImpl
    // =========================================================================

    void VulkanDevice::DestroySwapchainImpl(SwapchainHandle h) {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return;
        }

        vkDeviceWaitIdle(device_);

        // Release texture handles from pool
        for (auto& texHandle : data->textureHandles) {
            if (texHandle.IsValid()) {
                auto slotIdx = textures_.MarkDead(texHandle);
                textures_.Reclaim(slotIdx);
            }
        }

        vkDestroySwapchainKHR(device_, data->swapchain, nullptr);
        vkDestroySurfaceKHR(instance_, data->surface, nullptr);
        swapchains_.Free(h);
    }

    // =========================================================================
    // ResizeSwapchainImpl
    // =========================================================================

    auto VulkanDevice::ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void> {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }
        vkDeviceWaitIdle(device_);

        // Release old texture handles from pool (swapchain images are being destroyed)
        for (auto& texHandle : data->textureHandles) {
            if (texHandle.IsValid()) {
                auto slotIdx = textures_.MarkDead(texHandle);
                textures_.Reclaim(slotIdx);
            }
        }
        data->textureHandles.clear();

        // Query updated capabilities
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, data->surface, &capabilities);

        VkExtent2D extent;
        if (capabilities.currentExtent.width != UINT32_MAX) {
            extent = capabilities.currentExtent;
        } else {
            extent.width = std::clamp(w, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(ht, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = data->surface;
        createInfo.minImageCount = static_cast<uint32_t>(data->images.size());
        createInfo.imageFormat = data->format;
        createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;  // Preserved from original
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // Preserved from original
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = data->swapchain;

        VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
        VkResult result = vkCreateSwapchainKHR(device_, &createInfo, nullptr, &newSwapchain);

        vkDestroySwapchainKHR(device_, data->swapchain, nullptr);

        if (result != VK_SUCCESS) {
            data->swapchain = VK_NULL_HANDLE;
            return std::unexpected(RhiError::DeviceLost);
        }

        data->swapchain = newSwapchain;
        data->extent = extent;

        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(device_, newSwapchain, &imageCount, nullptr);
        data->images.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, newSwapchain, &imageCount, data->images.data());

        // Register new swapchain images in textures_ pool
        data->textureHandles.clear();
        data->textureHandles.reserve(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i) {
            auto [texHandle, texData] = textures_.Allocate();
            if (!texData) {
                // Partial failure — release already allocated handles
                for (auto& th : data->textureHandles) {
                    auto slotIdx = textures_.MarkDead(th);
                    textures_.Reclaim(slotIdx);
                }
                data->textureHandles.clear();
                return std::unexpected(RhiError::TooManyObjects);
            }
            texData->image = data->images[i];
            texData->allocation = nullptr;
            texData->format = data->format;
            texData->extent = {extent.width, extent.height, 1};
            texData->mipLevels = 1;
            texData->arrayLayers = 1;
            texData->dimension = TextureDimension::Tex2D;  // Swapchain images are always 2D
            texData->ownsImage = false;
            data->textureHandles.push_back(texHandle);
        }

        return {};
    }

    // =========================================================================
    // AcquireNextImageImpl
    // =========================================================================

    auto VulkanDevice::AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle fence)
        -> RhiResult<uint32_t> {
        using enum ::miki::debug::LogCategory;

        auto* scData = swapchains_.Lookup(h);
        if (!scData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        VkSemaphore vkSem = VK_NULL_HANDLE;
        if (signal.IsValid()) {
            auto* semData = semaphores_.Lookup(signal);
            if (semData) {
                vkSem = semData->semaphore;
            }
        }

        VkFence vkFence = VK_NULL_HANDLE;
        if (fence.IsValid()) {
            auto* fenceData = fences_.Lookup(fence);
            if (fenceData) {
                vkFence = fenceData->fence;
            }
        }

        MIKI_LOG_INFO(
            Rhi, "AcquireNextImage: vkSem = {} (0x{:x}), vkFence = {} (0x{:x})", static_cast<void*>(vkSem),
            reinterpret_cast<uintptr_t>(vkSem), static_cast<void*>(vkFence), reinterpret_cast<uintptr_t>(vkFence)
        );

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(
            device_, scData->swapchain, std::numeric_limits<uint64_t>::max(), vkSem, vkFence, &imageIndex
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                return std::unexpected(RhiError::DeviceLost);
            }
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return std::unexpected(RhiError::DeviceLost);
        }

        scData->lastAcquiredIndex = imageIndex;
        return imageIndex;
    }

    // =========================================================================
    // GetSwapchainTextureImpl
    // =========================================================================

    auto VulkanDevice::GetSwapchainTextureImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle {
        auto* data = swapchains_.Lookup(h);
        if (!data || imageIndex >= data->textureHandles.size()) {
            return {};
        }
        return data->textureHandles[imageIndex];
    }

    auto VulkanDevice::GetSwapchainTextureViewImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureViewHandle {
        auto* data = swapchains_.Lookup(h);
        if (!data || imageIndex >= data->textureViewHandles.size()) {
            return {};
        }
        return data->textureViewHandles[imageIndex];
    }

    // =========================================================================
    // PresentImpl
    // =========================================================================

    void VulkanDevice::PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores) {
        auto* scData = swapchains_.Lookup(h);
        if (!scData) {
            return;
        }

        std::vector<VkSemaphore> vkWaitSems;
        vkWaitSems.reserve(waitSemaphores.size());
        for (auto& sem : waitSemaphores) {
            if (sem.IsValid()) {
                auto* semData = semaphores_.Lookup(sem);
                if (semData) {
                    vkWaitSems.push_back(semData->semaphore);
                }
            }
        }

        uint32_t imageIndex = scData->lastAcquiredIndex;

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = static_cast<uint32_t>(vkWaitSems.size());
        presentInfo.pWaitSemaphores = vkWaitSems.data();
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &scData->swapchain;
        presentInfo.pImageIndices = &imageIndex;

        VkResult result = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "[Vulkan] Present: swapchain out of date or suboptimal");
        }
    }

    // =========================================================================
    // Surface capability query
    // =========================================================================

    static auto FromVkFormat(VkFormat fmt) -> std::optional<Format> {
        switch (fmt) {
            case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8_SRGB;
            case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8_UNORM;
            case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8_SRGB;
            case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNORM;
            case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::RGBA16_FLOAT;
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return Format::RGB10A2_UNORM;
            default: return std::nullopt;
        }
    }

    static auto FromVkPresentMode(VkPresentModeKHR mode) -> std::optional<PresentMode> {
        switch (mode) {
            case VK_PRESENT_MODE_IMMEDIATE_KHR: return PresentMode::Immediate;
            case VK_PRESENT_MODE_MAILBOX_KHR: return PresentMode::Mailbox;
            case VK_PRESENT_MODE_FIFO_KHR: return PresentMode::Fifo;
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return PresentMode::FifoRelaxed;
            default: return std::nullopt;
        }
    }

    static auto FromVkColorSpace(VkColorSpaceKHR cs) -> std::optional<SurfaceColorSpace> {
        switch (cs) {
            case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return SurfaceColorSpace::SRGB;
            case VK_COLOR_SPACE_HDR10_ST2084_EXT: return SurfaceColorSpace::HDR10_ST2084;
            case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return SurfaceColorSpace::scRGBLinear;
            default: return std::nullopt;
        }
    }

    auto VulkanDevice::GetSurfaceCapabilitiesImpl(const NativeWindowHandle& window) const -> RenderSurfaceCapabilities {
        RenderSurfaceCapabilities caps;

        VkSurfaceKHR surface = CreateVkSurface(instance_, window);
        if (surface == VK_NULL_HANDLE) {
            return caps;
        }

        // Query surface capabilities
        VkSurfaceCapabilitiesKHR vkCaps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface, &vkCaps);

        caps.minExtent = {.width = vkCaps.minImageExtent.width, .height = vkCaps.minImageExtent.height};
        caps.maxExtent = {.width = vkCaps.maxImageExtent.width, .height = vkCaps.maxImageExtent.height};
        caps.minImageCount = vkCaps.minImageCount;
        caps.maxImageCount = (vkCaps.maxImageCount == 0) ? 16 : vkCaps.maxImageCount;

        // Query surface formats
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface, &formatCount, nullptr);
        if (formatCount > 0) {
            std::vector<VkSurfaceFormatKHR> vkFormats(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface, &formatCount, vkFormats.data());
            for (auto& sf : vkFormats) {
                if (auto fmt = FromVkFormat(sf.format)) {
                    caps.supportedFormats.push_back(*fmt);
                }
                if (auto cs = FromVkColorSpace(sf.colorSpace)) {
                    if (std::ranges::find(caps.supportedColorSpaces, *cs) == caps.supportedColorSpaces.end()) {
                        caps.supportedColorSpaces.push_back(*cs);
                    }
                }
            }
        }

        // Query present modes
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface, &modeCount, nullptr);
        if (modeCount > 0) {
            std::vector<VkPresentModeKHR> vkModes(modeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface, &modeCount, vkModes.data());
            for (auto m : vkModes) {
                if (auto pm = FromVkPresentMode(m)) {
                    caps.supportedPresentModes.push_back(*pm);
                }
            }
        }

        vkDestroySurfaceKHR(instance_, surface, nullptr);
        return caps;
    }

}  // namespace miki::rhi
