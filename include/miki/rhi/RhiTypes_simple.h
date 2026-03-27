/** @brief Simplified RHI types for window management demo.
 */

#pragma once

#include <cstdint>

namespace miki {
namespace rhi {

    // Simplified backend type
    enum class BackendType : uint8_t {
        Vulkan = 0,
        OpenGL = 1,
        D3D12 = 2,
        WebGPU = 3
    };

    // Extent2D
    struct Extent2D {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    // Native window handles
    struct Win32Window { void* hwnd = nullptr; };
    struct X11Window { void* display = nullptr; void* window = nullptr; };
    struct WaylandWindow { void* display = nullptr; void* surface = nullptr; };
    struct WebCanvas { void* canvas = nullptr; };

    // Simplified native handle - just Win32 for now
    using NativeWindowHandle = Win32Window;

} // namespace rhi
} // namespace miki
