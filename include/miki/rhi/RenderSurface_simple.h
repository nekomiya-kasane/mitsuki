/** @brief Simplified RenderSurface for demo.
 */

#pragma once

#include <cstdint>
#include "miki/rhi/RhiTypes.h"
#include "miki/core/Result.h"

namespace miki {
namespace rhi {

    // Forward declarations
    struct RenderSurfaceConfig {
        uint32_t width = 1280;
        uint32_t height = 720;
        // Add other config as needed
    };

    struct RenderSurfaceDesc {
        NativeWindowHandle window;
        RenderSurfaceConfig initialConfig;
    };

    // Simplified RenderSurface class
    class RenderSurface {
    public:
        static auto Create(const RenderSurfaceDesc& desc) 
            -> core::Result<RenderSurface> {
            // Simplified - just return a valid surface
            return RenderSurface{};
        }
        
        Extent2D GetExtent() const { return Extent2D{1280, 720}; }
        
        bool Resize(uint32_t width, uint32_t height) { 
            // Simplified - always succeed
            return true; 
        }
    };

} // namespace rhi
} // namespace miki
