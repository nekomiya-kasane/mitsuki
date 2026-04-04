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

    static auto FromWGPUTextureFormat(WGPUTextureFormat fmt) -> std::optional<Format> {
        switch (fmt) {
            case WGPUTextureFormat_BGRA8Unorm: return Format::BGRA8_UNORM;
            case WGPUTextureFormat_BGRA8UnormSrgb: return Format::BGRA8_SRGB;
            case WGPUTextureFormat_RGBA8Unorm: return Format::RGBA8_UNORM;
            case WGPUTextureFormat_RGBA8UnormSrgb: return Format::RGBA8_SRGB;
            case WGPUTextureFormat_RGBA16Float: return Format::RGBA16_FLOAT;
            case WGPUTextureFormat_RGB10A2Unorm: return Format::RGB10A2_UNORM;
            default: return std::nullopt;
        }
    }

    static auto FromWGPUPresentMode(WGPUPresentMode mode) -> std::optional<PresentMode> {
        switch (mode) {
            case WGPUPresentMode_Immediate: return PresentMode::Immediate;
            case WGPUPresentMode_Mailbox: return PresentMode::Mailbox;
            case WGPUPresentMode_Fifo: return PresentMode::Fifo;
            case WGPUPresentMode_FifoRelaxed: return PresentMode::FifoRelaxed;
            default: return std::nullopt;
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

        // Pre-create TextureView wrapper (will be updated on AcquireNextImage)
        // WebGPU creates the actual view dynamically, but we need a handle for API consistency
        {
            auto [viewHandle, viewData] = textureViews_.Allocate();
            if (viewHandle.IsValid()) {
                viewData->view = nullptr;  // Will be set on AcquireNextImage
                viewData->parentTexture = data->colorTexture;
                data->colorTextureView = viewHandle;
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

        // Free texture view handle (the actual WGPUTextureView was released above)
        if (data->colorTextureView.IsValid()) {
            auto* viewData = textureViews_.Lookup(data->colorTextureView);
            if (viewData) {
                viewData->view = nullptr;  // Already released
            }
            textureViews_.Free(data->colorTextureView);
        }

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

        // Update the wrapper texture view handle to point to this frame's view
        auto* viewData = textureViews_.Lookup(data->colorTextureView);
        if (viewData) {
            viewData->view = data->currentView;
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

    auto WebGPUDevice::GetSwapchainTextureViewImpl(SwapchainHandle h, [[maybe_unused]] uint32_t imageIndex)
        -> TextureViewHandle {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return {};
        }
        return data->colorTextureView;
    }

    auto WebGPUDevice::GetSwapchainImageCountImpl([[maybe_unused]] SwapchainHandle h) -> uint32_t {
        return 1;  // WebGPU surface model: one texture at a time
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

        // Clear wrapper handles so stale pointers are never dereferenced between frames
        auto* texData = textures_.Lookup(data->colorTexture);
        if (texData) {
            texData->texture = nullptr;
        }
        auto* viewData = textureViews_.Lookup(data->colorTextureView);
        if (viewData) {
            viewData->view = nullptr;
        }
    }

    // =========================================================================
    // Surface capability query
    // =========================================================================

    auto WebGPUDevice::GetSurfaceCapabilitiesImpl(const NativeWindowHandle& window) const -> RenderSurfaceCapabilities {
        RenderSurfaceCapabilities caps;

        // Hardcoded fallbacks (used if real query fails)
        caps.supportedFormats = {Format::BGRA8_UNORM};
        caps.supportedPresentModes = {PresentMode::Fifo};
        caps.supportedColorSpaces = {SurfaceColorSpace::SRGB};
        caps.minExtent = {.width = 1, .height = 1};
        caps.maxExtent = {.width = 16384, .height = 16384};
        caps.minImageCount = 2;
        caps.maxImageCount = 3;

        // Create a temporary WGPUSurface to query real adapter capabilities.
        // Dawn requires (surface, adapter) pair for capability queries.
        WGPUSurface tempSurface = CreateWGPUSurface(instance_, window, nullptr);
        if (!tempSurface) {
            return caps;
        }

        WGPUSurfaceCapabilities wgpuCaps = WGPU_SURFACE_CAPABILITIES_INIT;
        WGPUStatus status = wgpuSurfaceGetCapabilities(tempSurface, adapter_, &wgpuCaps);
        if (status != WGPUStatus_Success) {
            wgpuSurfaceRelease(tempSurface);
            return caps;
        }

        // Convert formats
        caps.supportedFormats.clear();
        for (size_t i = 0; i < wgpuCaps.formatCount; ++i) {
            if (auto fmt = FromWGPUTextureFormat(wgpuCaps.formats[i])) {
                caps.supportedFormats.push_back(*fmt);
            }
        }
        if (caps.supportedFormats.empty()) {
            caps.supportedFormats.push_back(Format::BGRA8_UNORM);
        }

        // Convert present modes
        caps.supportedPresentModes.clear();
        for (size_t i = 0; i < wgpuCaps.presentModeCount; ++i) {
            if (auto pm = FromWGPUPresentMode(wgpuCaps.presentModes[i])) {
                caps.supportedPresentModes.push_back(*pm);
            }
        }
        if (caps.supportedPresentModes.empty()) {
            caps.supportedPresentModes.push_back(PresentMode::Fifo);
        }

        wgpuSurfaceCapabilitiesFreeMembers(wgpuCaps);
        wgpuSurfaceRelease(tempSurface);
        return caps;
    }

}  // namespace miki::rhi
