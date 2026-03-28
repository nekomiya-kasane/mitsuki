/** @file Swapchain.h
 *  @brief Swapchain creation, present, and surface management descriptors.
 *
 *  Low-level: SwapchainDesc (used by DeviceBase::CreateSwapchain).
 *  High-level: RenderSurfaceConfig (used by SurfaceManager::AttachSurface).
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>

#include "miki/rhi/Format.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

    // =========================================================================
    // SurfaceColorSpace — color space for swapchain output
    // =========================================================================

    /// Maps to VkColorSpaceKHR / DXGI_COLOR_SPACE_TYPE.
    /// Shared by both SwapchainDesc (low-level) and RenderSurfaceConfig (high-level).
    enum class SurfaceColorSpace : uint8_t {
        SRGB,          ///< sRGB transfer, Rec.709 gamut (default, universally supported)
        HDR10_ST2084,  ///< PQ transfer, Rec.2020 gamut (VK_COLOR_SPACE_HDR10_ST2084_EXT /
                       ///< DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
        scRGBLinear,   ///< Linear FP16 (VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT /
                       ///< DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) — T1 only
    };

    // =========================================================================
    // SwapchainDesc — low-level swapchain creation parameters (DeviceBase API)
    // =========================================================================

    /// Precise, fully-resolved parameters passed to DeviceBase::CreateSwapchain.
    /// Constructed internally by RenderSurface from RenderSurfaceConfig + runtime state.
    struct SwapchainDesc {
        NativeWindowHandle surface;
        uint32_t width = 0;
        uint32_t height = 0;
        Format preferredFormat = Format::BGRA8_SRGB;
        PresentMode presentMode = PresentMode::Fifo;
        SurfaceColorSpace colorSpace = SurfaceColorSpace::SRGB;
        uint32_t imageCount = 2;
        const char* debugName = nullptr;
    };

    // =========================================================================
    // VRRMode — Variable Refresh Rate mode
    // =========================================================================

    /// All VRR modes use the same underlying API (DXGI_PRESENT_ALLOW_TEARING / VK_PRESENT_MODE_*).
    /// G-Sync Compatible monitors work via AdaptiveSync; proprietary G-Sync modules also use this path.
    enum class VRRMode : uint8_t {
        Off,           ///< VSync on, no tearing, fixed refresh
        AdaptiveSync,  ///< VESA AdaptiveSync / AMD FreeSync / NVIDIA G-Sync Compatible
        GSync,         ///< NVIDIA G-Sync (same API path, explicit opt-in for G-Sync-specific features)
    };

    // =========================================================================
    // ImageCountHint — swapchain image count hint
    // =========================================================================

    /// Backend may adjust based on PresentMode constraints (e.g. Mailbox requires >= 3).
    enum class ImageCountHint : uint8_t {
        Auto,     ///< Backend chooses optimal (typically 3 for Mailbox, 2 for Fifo)
        Minimal,  ///< Minimize VRAM (2 images, may increase latency)
        Triple,   ///< Force triple buffering (3 images)
    };

    // =========================================================================
    // RenderSurfaceConfig — per-window surface configuration
    // =========================================================================

    /// Passed to SurfaceManager::AttachSurface; can be changed via detach-reattach or dynamically via
    /// SetPresentMode/SetColorSpace. Does NOT contain width/height — those are runtime state from resize events.
    struct RenderSurfaceConfig {
        PresentMode presentMode = PresentMode::Fifo;
        SurfaceColorSpace colorSpace = SurfaceColorSpace::SRGB;
        Format preferredFormat = Format::BGRA8_SRGB;
        VRRMode vrrMode = VRRMode::Off;
        ImageCountHint imageCount = ImageCountHint::Auto;
        // NOTE: Backend may override preferredFormat based on colorSpace.
        // e.g. scRGBLinear -> R16G16B16A16_SFLOAT on T1 (Vulkan/D3D12).
    };

}  // namespace miki::rhi
