/** @file RhiTypes.h
 *  @brief Fundamental RHI types: BackendType, Extent, Offset, Rect, NativeWindowHandle.
 *
 *  This is the lowest-level RHI header — no other RHI header should be included here.
 *  All higher-level RHI headers may include this.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <variant>

namespace miki::rhi {

    // =========================================================================
    // Backend identification
    // =========================================================================

    enum class BackendType : uint8_t {
        Vulkan14,      ///< Vulkan 1.4 (Roadmap 2026) — Tier 1
        D3D12,         ///< Direct3D 12 (Agility SDK 1.719+) — Tier 1
        VulkanCompat,  ///< Vulkan 1.1 + select extensions — Tier 2
        WebGPU,        ///< Dawn / wgpu — Tier 3
        OpenGL43,      ///< OpenGL 4.3+ — Tier 4
        Mock,          ///< Test / headless mock backend
    };

    // =========================================================================
    // Geometry primitives
    // =========================================================================

    struct Extent2D {
        uint32_t width = 0;
        uint32_t height = 0;
        constexpr auto operator==(const Extent2D&) const noexcept -> bool = default;
    };

    struct Extent3D {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        constexpr auto operator==(const Extent3D&) const noexcept -> bool = default;
    };

    struct Offset2D {
        int32_t x = 0;
        int32_t y = 0;
        constexpr auto operator==(const Offset2D&) const noexcept -> bool = default;
    };

    struct Offset3D {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        constexpr auto operator==(const Offset3D&) const noexcept -> bool = default;
    };

    struct Rect2D {
        Offset2D offset;
        Extent2D extent;
        constexpr auto operator==(const Rect2D&) const noexcept -> bool = default;
    };

    struct Viewport {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    // =========================================================================
    // Native window handle (platform-specific, for swapchain creation)
    // =========================================================================

    struct Win32Window {
        void* hwnd = nullptr;
        void* hinstance = nullptr;
    };

    struct X11Window {
        void* display = nullptr;
        uint64_t window = 0;
    };

    struct WaylandWindow {
        void* display = nullptr;
        void* surface = nullptr;
    };

    struct CocoaWindow {
        void* nsWindow = nullptr;
    };

    struct EmscriptenCanvas {
        const char* selector = nullptr;
    };

    using NativeWindowHandle = std::variant<Win32Window, X11Window, WaylandWindow, CocoaWindow, EmscriptenCanvas>;

    // NativeSurfaceHandle removed — SwapchainDesc now uses NativeWindowHandle directly.
    // Backends extract the platform-specific handle via std::get<> on the variant.

    // =========================================================================
    // Clear values
    // =========================================================================

    struct ClearColor {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    };

    struct ClearDepthStencil {
        float depth = 1.0f;
        uint8_t stencil = 0;
    };

    struct ClearValue {
        ClearColor color;
        ClearDepthStencil depthStencil;
    };

}  // namespace miki::rhi
