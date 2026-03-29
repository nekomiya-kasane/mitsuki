/** @file WebGPUSwapchain.cpp
 *  @brief WebGPU / Dawn (Tier 3) backend — swapchain (WGPUSurface + configure).
 *
 *  Dawn uses WGPUSurface with configure/unconfigure model (not the deprecated WGPUSwapChain).
 *  Surface creation uses the NativeWindowHandle variant:
 *    - Win32Window      → WGPUSurfaceSourceWindowsHWND
 *    - X11Window        → WGPUSurfaceSourceXlibWindow
 *    - WaylandWindow    → WGPUSurfaceSourceWaylandSurface
 *    - CocoaWindow      → WGPUSurfaceSourceMetalLayer (via QuartzCore)
 *    - EmscriptenCanvas → WGPUSurfaceSourceCanvasHTMLSelector_Emscripten
 */

#include "miki/rhi/backend/WebGPUDevice.h"
#include "miki/debug/StructuredLogger.h"

#include <dawn/webgpu.h>

#if defined(__APPLE__)
#    include <QuartzCore/CAMetalLayer.h>
#endif

namespace miki::rhi {

    // =========================================================================
    // Helpers
    // =========================================================================

    static auto ToWGPUPresentMode(PresentMode mode) -> WGPUPresentMode {
        switch (mode) {
            case PresentMode::Immediate: return WGPUPresentMode_Immediate;
            case PresentMode::Mailbox: return WGPUPresentMode_Mailbox;
            case PresentMode::Fifo: return WGPUPresentMode_Fifo;
            case PresentMode::FifoRelaxed: return WGPUPresentMode_FifoRelaxed;
            default: return WGPUPresentMode_Fifo;
        }
    }

    static auto ToWGPUTextureFormat(Format fmt) -> WGPUTextureFormat {
        switch (fmt) {
            case Format::BGRA8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
            case Format::BGRA8_SRGB: return WGPUTextureFormat_BGRA8UnormSrgb;
            case Format::RGBA8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
            case Format::RGBA8_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
            case Format::RGBA16_FLOAT: return WGPUTextureFormat_RGBA16Float;
            case Format::RGB10A2_UNORM: return WGPUTextureFormat_RGB10A2Unorm;
            default: return WGPUTextureFormat_BGRA8Unorm;
        }
    }

    /// Create a WGPUSurface from the NativeWindowHandle variant.
    static auto CreateWGPUSurface(WGPUInstance instance, const NativeWindowHandle& nativeHandle, const char* debugName)
        -> WGPUSurface {
        WGPUSurfaceDescriptor surfDesc{};
        surfDesc.label = debugName ? WGPUStringView{.data = debugName, .length = WGPU_STRLEN}
                                   : WGPUStringView{.data = nullptr, .length = 0};

        return std::visit(
            [&](auto&& handle) -> WGPUSurface {
                using T = std::decay_t<decltype(handle)>;

                if constexpr (std::is_same_v<T, Win32Window>) {
                    WGPUSurfaceSourceWindowsHWND src{};
                    src.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
                    src.chain.next = nullptr;
                    src.hwnd = handle.hwnd;
                    src.hinstance = handle.hinstance;
                    surfDesc.nextInChain = &src.chain;
                    return wgpuInstanceCreateSurface(instance, &surfDesc);

                } else if constexpr (std::is_same_v<T, X11Window>) {
                    WGPUSurfaceSourceXlibWindow src{};
                    src.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
                    src.chain.next = nullptr;
                    src.display = handle.display;
                    src.window = static_cast<uint64_t>(handle.window);
                    surfDesc.nextInChain = &src.chain;
                    return wgpuInstanceCreateSurface(instance, &surfDesc);

                } else if constexpr (std::is_same_v<T, WaylandWindow>) {
                    WGPUSurfaceSourceWaylandSurface src{};
                    src.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
                    src.chain.next = nullptr;
                    src.display = handle.display;
                    src.surface = handle.surface;
                    surfDesc.nextInChain = &src.chain;
                    return wgpuInstanceCreateSurface(instance, &surfDesc);

                } else if constexpr (std::is_same_v<T, CocoaWindow>) {
#if defined(__APPLE__)
                    // nsWindow is NSWindow* — extract the CAMetalLayer from its contentView
                    id nsWin = (__bridge id)handle.nsWindow;
                    CAMetalLayer* layer = [CAMetalLayer layer];
                    [[nsWin contentView] setLayer:layer];
                    [[nsWin contentView] setWantsLayer:YES];
                    WGPUSurfaceSourceMetalLayer src{};
                    src.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
                    src.chain.next = nullptr;
                    src.layer = (__bridge void*)layer;
                    surfDesc.nextInChain = &src.chain;
                    return wgpuInstanceCreateSurface(instance, &surfDesc);
#else
                    return nullptr;
#endif

                } else if constexpr (std::is_same_v<T, EmscriptenCanvas>) {
#if defined(__EMSCRIPTEN__)
                    const char* sel = handle.selector ? handle.selector : "#canvas";
                    WGPUSurfaceSourceCanvasHTMLSelector_Emscripten src{};
                    src.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector_Emscripten;
                    src.chain.next = nullptr;
                    src.selector = sel;
                    surfDesc.nextInChain = &src.chain;
                    return wgpuInstanceCreateSurface(instance, &surfDesc);
#else
                    (void)handle;
                    return nullptr;
#endif
                } else {
                    return nullptr;
                }
            },
            nativeHandle
        );
    }

    // =========================================================================
    // Create / Destroy
    // =========================================================================

    auto WebGPUDevice::CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle> {
        auto [handle, data] = swapchains_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        // --- Create surface from NativeWindowHandle variant ---
        data->surface = CreateWGPUSurface(instance_, desc.surface, desc.debugName);

        if (!data->surface) {
            swapchains_.Free(handle);
            return std::unexpected(RhiError::SurfaceLost);
        }

        // --- Configure surface ---
        data->format = ToWGPUTextureFormat(desc.preferredFormat);
        data->presentMode = ToWGPUPresentMode(desc.presentMode);
        data->width = desc.width;
        data->height = desc.height;

        WGPUSurfaceConfiguration config{};
        config.device = device_;
        config.format = data->format;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.width = desc.width;
        config.height = desc.height;
        config.presentMode = data->presentMode;
        config.viewFormatCount = 0;
        config.viewFormats = nullptr;

        wgpuSurfaceConfigure(data->surface, &config);

        // --- Create wrapper texture handle for current back buffer ---
        {
            auto [texHandle, texData] = textures_.Allocate();
            if (texHandle.IsValid()) {
                texData->texture = nullptr;  // Will be set on AcquireNextImage
                texData->dimension = WGPUTextureDimension_2D;
                texData->format = data->format;
                texData->width = desc.width;
                texData->height = desc.height;
                texData->depthOrArrayLayers = 1;
                texData->mipLevels = 1;
                texData->sampleCount = 1;
                texData->ownsTexture = false;  // Swapchain-owned
                data->colorTexture = texHandle;
            }
        }

        return handle;
    }

    void WebGPUDevice::DestroySwapchainImpl(SwapchainHandle h) {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return;
        }

        if (data->currentView) {
            wgpuTextureViewRelease(data->currentView);
            data->currentView = nullptr;
        }
        // Don't destroy currentTexture — it's owned by the surface

        if (data->colorTexture.IsValid()) {
            textures_.Free(data->colorTexture);
        }

        if (data->surface) {
            wgpuSurfaceUnconfigure(data->surface);
            wgpuSurfaceRelease(data->surface);
        }

        swapchains_.Free(h);
    }

    auto WebGPUDevice::ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void> {
        auto* data = swapchains_.Lookup(h);
        if (!data || !data->surface) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        if (data->currentView) {
            wgpuTextureViewRelease(data->currentView);
            data->currentView = nullptr;
        }

        data->width = w;
        data->height = ht;

        WGPUSurfaceConfiguration config{};
        config.device = device_;
        config.format = data->format;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.width = w;
        config.height = ht;
        config.presentMode = data->presentMode;
        config.viewFormatCount = 0;
        config.viewFormats = nullptr;

        wgpuSurfaceConfigure(data->surface, &config);

        // Update texture wrapper dimensions
        auto* texData = textures_.Lookup(data->colorTexture);
        if (texData) {
            texData->width = w;
            texData->height = ht;
        }

        return {};
    }

    // =========================================================================
    // Acquire / Present
    // =========================================================================

    auto WebGPUDevice::AcquireNextImageImpl(
        SwapchainHandle h, [[maybe_unused]] SemaphoreHandle signal, [[maybe_unused]] FenceHandle fence
    ) -> RhiResult<uint32_t> {
        auto* data = swapchains_.Lookup(h);
        if (!data || !data->surface) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Release previous frame's view
        if (data->currentView) {
            wgpuTextureViewRelease(data->currentView);
            data->currentView = nullptr;
        }

        WGPUSurfaceTexture surfaceTex{};
        wgpuSurfaceGetCurrentTexture(data->surface, &surfaceTex);

        if (surfaceTex.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU: surface texture suboptimal, resize pending");
        } else if (surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal || !surfaceTex.texture) {
            MIKI_LOG_ERROR(
                ::miki::debug::LogCategory::Rhi, "WebGPU: failed to acquire surface texture, status={}",
                static_cast<int>(surfaceTex.status)
            );
            return std::unexpected(RhiError::SurfaceLost);
        }

        data->currentTexture = surfaceTex.texture;
        data->currentView = wgpuTextureCreateView(surfaceTex.texture, nullptr);

        // Update the wrapper texture handle to point to this frame's texture
        auto* texData = textures_.Lookup(data->colorTexture);
        if (texData) {
            texData->texture = surfaceTex.texture;
        }

        // WebGPU has no semaphore/fence for acquire — single-queue, implicit sync
        if (signal.IsValid()) {
            auto* semData = semaphores_.Lookup(signal);
            if (semData) {
                semData->value++;
            }
        }
        if (fence.IsValid()) {
            auto* fenceData = fences_.Lookup(fence);
            if (fenceData) {
                fenceData->signaled = true;
            }
        }

        data->currentImage = 0;  // WebGPU surface always has 1 logical image
        return 0u;
    }

    auto WebGPUDevice::GetSwapchainTextureImpl(SwapchainHandle h, [[maybe_unused]] uint32_t imageIndex)
        -> TextureHandle {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return {};
        }
        return data->colorTexture;
    }

    void WebGPUDevice::PresentImpl(
        SwapchainHandle h, [[maybe_unused]] std::span<const SemaphoreHandle> waitSemaphores
    ) {
        auto* data = swapchains_.Lookup(h);
        if (!data || !data->surface) {
            return;
        }

        wgpuSurfacePresent(data->surface);

        // Release this frame's texture view (will be re-acquired next frame)
        if (data->currentView) {
            wgpuTextureViewRelease(data->currentView);
            data->currentView = nullptr;
        }
        data->currentTexture = nullptr;
    }

    // =========================================================================
    // Surface capability query
    // =========================================================================

    auto WebGPUDevice::GetSurfaceCapabilitiesImpl(const NativeWindowHandle& window) const -> RenderSurfaceCapabilities {
        (void)window;
        RenderSurfaceCapabilities caps;
        // Dawn/WebGPU preferred surface format is BGRA8Unorm
        caps.supportedFormats = {Format::BGRA8_UNORM, Format::RGBA8_UNORM};
        caps.supportedPresentModes = {PresentMode::Fifo, PresentMode::Mailbox};
        caps.supportedColorSpaces = {SurfaceColorSpace::SRGB};
        caps.minExtent = {1, 1};
        caps.maxExtent = {16384, 16384};
        caps.minImageCount = 2;
        caps.maxImageCount = 3;
        return caps;
    }

}  // namespace miki::rhi
