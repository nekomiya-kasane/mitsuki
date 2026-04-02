/** @file D3D12Swapchain.cpp
 *  @brief D3D12 (Tier 1) backend — DXGI swapchain creation, resize, acquire, present.
 *
 *  Uses IDXGISwapChain4 with flip model (DXGI_SWAP_EFFECT_FLIP_DISCARD).
 *  Back buffers are registered in the texture pool as non-owning handles.
 */

#include "miki/rhi/backend/D3D12Device.h"

#include "miki/debug/StructuredLogger.h"

#include <algorithm>

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

namespace miki::rhi {

    // =========================================================================
    // Format conversion
    // =========================================================================

    namespace {
        auto ToDxgiSwapchainFormat(Format fmt) -> DXGI_FORMAT {
            switch (fmt) {
                case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;  // Swapchain uses UNORM, SRGB via RTV
                case Format::BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
                case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
                case Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
                case Format::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
                case Format::RGB10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
                default: return DXGI_FORMAT_B8G8R8A8_UNORM;
            }
        }

        auto ToDxgiRtvFormat(Format fmt) -> DXGI_FORMAT {
            switch (fmt) {
                case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                case Format::BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
                case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                case Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
                case Format::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
                case Format::RGB10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
                default: return DXGI_FORMAT_B8G8R8A8_UNORM;
            }
        }

        auto ToDxgiColorSpace(SurfaceColorSpace cs) -> DXGI_COLOR_SPACE_TYPE {
            switch (cs) {
                case SurfaceColorSpace::SRGB: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
                case SurfaceColorSpace::HDR10_ST2084: return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                case SurfaceColorSpace::scRGBLinear: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            }
            return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        }
    }  // namespace

    // =========================================================================
    // CreateSwapchainImpl
    // =========================================================================

    auto D3D12Device::CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle> {
        // Extract HWND from NativeWindowHandle variant
        HWND hwnd = nullptr;
        auto visitor = [&](auto&& win) {
            using T = std::decay_t<decltype(win)>;
            if constexpr (std::is_same_v<T, Win32Window>) {
                hwnd = static_cast<HWND>(win.hwnd);
            }
        };
        std::visit(visitor, desc.surface);

        if (!hwnd) {
            return std::unexpected(RhiError::InvalidParameter);
        }

        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = desc.width;
        swapDesc.Height = desc.height;
        swapDesc.Format = ToDxgiSwapchainFormat(desc.preferredFormat);
        swapDesc.Stereo = FALSE;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.SampleDesc.Quality = 0;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = (desc.imageCount > 2u) ? desc.imageCount : 2u;
        swapDesc.Scaling = DXGI_SCALING_STRETCH;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        ComPtr<IDXGISwapChain1> swapchain1;
        HRESULT hr
            = factory_->CreateSwapChainForHwnd(queues_.graphics.Get(), hwnd, &swapDesc, nullptr, nullptr, &swapchain1);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::DeviceLost);
        }

        // Disable Alt+Enter fullscreen toggle
        factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        ComPtr<IDXGISwapChain4> swapchain4;
        hr = swapchain1.As(&swapchain4);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::DeviceLost);
        }

        // Set HDR color space if requested
        if (desc.colorSpace != SurfaceColorSpace::SRGB) {
            swapchain4->SetColorSpace1(ToDxgiColorSpace(desc.colorSpace));
        }

        // Get back buffers and create RTVs
        uint32_t bufferCount = swapDesc.BufferCount;
        std::vector<ComPtr<ID3D12Resource>> backBuffers(bufferCount);
        std::vector<TextureHandle> textureHandles;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;

        for (uint32_t i = 0; i < bufferCount; ++i) {
            hr = swapchain4->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
            if (FAILED(hr)) {
                return std::unexpected(RhiError::DeviceLost);
            }

            // Register in texture pool as non-owning
            auto [texHandle, texData] = textures_.Allocate();
            if (!texData) {
                return std::unexpected(RhiError::TooManyObjects);
            }
            texData->resource = backBuffers[i];
            texData->allocation = nullptr;
            // Store the RTV format (SRGB) not the UNORM swapchain buffer format, so that CreateTextureViewImpl creates
            // an SRGB RTV for correct gamma.
            texData->format = ToDxgiRtvFormat(desc.preferredFormat);
            texData->width = desc.width;
            texData->height = desc.height;
            texData->depth = 1;
            texData->mipLevels = 1;
            texData->arrayLayers = 1;
            texData->dimension = TextureDimension::Tex2D;  // Swapchain images are always 2D
            texData->ownsResource = false;
            textureHandles.push_back(texHandle);

            // Create RTV with correct format (SRGB view for SRGB formats)
            uint32_t rtvOffset = rtvHeap_.Allocate(1);
            if (rtvOffset == UINT32_MAX) {
                return std::unexpected(RhiError::TooManyObjects);
            }
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_.GetCpuHandle(rtvOffset);
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = ToDxgiRtvFormat(desc.preferredFormat);
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;
            device_->CreateRenderTargetView(backBuffers[i].Get(), &rtvDesc, rtvHandle);
            rtvHandles.push_back(rtvHandle);
        }

        // Pre-create TextureViews for each back buffer (swapchain owns these views)
        std::vector<TextureViewHandle> textureViewHandles;
        textureViewHandles.reserve(bufferCount);
        for (uint32_t i = 0; i < bufferCount; ++i) {
            TextureViewDesc tvd{.texture = textureHandles[i]};
            auto viewResult = CreateTextureViewImpl(tvd);
            if (!viewResult) {
                // Rollback already created views
                for (auto& vh : textureViewHandles) {
                    DestroyTextureViewImpl(vh);
                }
                return std::unexpected(RhiError::TooManyObjects);
            }
            textureViewHandles.push_back(*viewResult);
        }

        auto [handle, data] = swapchains_.Allocate();
        if (!data) {
            // Rollback texture views
            for (auto& vh : textureViewHandles) {
                DestroyTextureViewImpl(vh);
            }
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->swapchain = std::move(swapchain4);
        data->backBuffers = std::move(backBuffers);
        data->textureHandles = std::move(textureHandles);
        data->textureViewHandles = std::move(textureViewHandles);
        data->rtvHandles = std::move(rtvHandles);
        data->format = swapDesc.Format;
        data->width = desc.width;
        data->height = desc.height;
        data->hwnd = hwnd;
        data->currentBackBufferIndex = data->swapchain->GetCurrentBackBufferIndex();

        return handle;
    }

    // =========================================================================
    // DestroySwapchainImpl
    // =========================================================================

    void D3D12Device::DestroySwapchainImpl(SwapchainHandle h) {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return;
        }

        WaitIdleImpl();

        // Free texture views first (they reference the textures)
        for (auto& vh : data->textureViewHandles) {
            if (vh.IsValid()) {
                DestroyTextureViewImpl(vh);
            }
        }

        // Free texture handles (non-owning — resource released by swapchain)
        for (auto& th : data->textureHandles) {
            auto* texData = textures_.Lookup(th);
            if (texData) {
                texData->resource = nullptr;  // Prevent double-free
                textures_.Free(th);
            }
        }

        swapchains_.Free(h);
    }

    // =========================================================================
    // ResizeSwapchainImpl
    // =========================================================================

    auto D3D12Device::ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void> {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        WaitIdleImpl();

        auto bufferCount = static_cast<uint32_t>(data->backBuffers.size());

        // Release old texture views first (they reference the textures)
        for (auto& vh : data->textureViewHandles) {
            if (vh.IsValid()) {
                DestroyTextureViewImpl(vh);
            }
        }
        data->textureViewHandles.clear();

        // Release old back buffer references from texture pool
        for (auto& th : data->textureHandles) {
            auto* texData = textures_.Lookup(th);
            if (texData) {
                texData->resource = nullptr;
                textures_.Free(th);
            }
        }
        data->backBuffers.clear();
        data->textureHandles.clear();
        data->rtvHandles.clear();
        HRESULT hr
            = data->swapchain->ResizeBuffers(bufferCount, w, ht, data->format, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::DeviceLost);
        }

        data->width = w;
        data->height = ht;

        // Re-acquire back buffers
        data->backBuffers.resize(bufferCount);
        for (uint32_t i = 0; i < bufferCount; ++i) {
            data->swapchain->GetBuffer(i, IID_PPV_ARGS(&data->backBuffers[i]));

            auto [texHandle, texData] = textures_.Allocate();
            if (!texData) {
                return std::unexpected(RhiError::TooManyObjects);
            }
            texData->resource = data->backBuffers[i];
            texData->allocation = nullptr;
            texData->format = data->format;
            texData->width = w;
            texData->height = ht;
            texData->depth = 1;
            texData->mipLevels = 1;
            texData->arrayLayers = 1;
            texData->dimension = TextureDimension::Tex2D;  // Swapchain images are always 2D
            texData->ownsResource = false;
            data->textureHandles.push_back(texHandle);

            uint32_t rtvOffset = rtvHeap_.Allocate(1);
            if (rtvOffset == UINT32_MAX) {
                return std::unexpected(RhiError::TooManyObjects);
            }
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_.GetCpuHandle(rtvOffset);
            device_->CreateRenderTargetView(data->backBuffers[i].Get(), nullptr, rtvHandle);
            data->rtvHandles.push_back(rtvHandle);
        }

        // Pre-create TextureViews for each new back buffer
        data->textureViewHandles.clear();
        data->textureViewHandles.reserve(bufferCount);
        for (uint32_t i = 0; i < bufferCount; ++i) {
            TextureViewDesc tvd{.texture = data->textureHandles[i]};
            auto viewResult = CreateTextureViewImpl(tvd);
            if (!viewResult) {
                // Partial failure — release already allocated views
                for (auto& vh : data->textureViewHandles) {
                    DestroyTextureViewImpl(vh);
                }
                data->textureViewHandles.clear();
                return std::unexpected(RhiError::TooManyObjects);
            }
            data->textureViewHandles.push_back(*viewResult);
        }

        data->currentBackBufferIndex = data->swapchain->GetCurrentBackBufferIndex();
        return {};
    }

    // =========================================================================
    // AcquireNextImageImpl
    // =========================================================================

    auto D3D12Device::AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle /*fence*/)
        -> RhiResult<uint32_t> {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // DXGI swapchain automatically manages back buffer index
        data->currentBackBufferIndex = data->swapchain->GetCurrentBackBufferIndex();

        // D3D12/DXGI: swapchain acquire is implicit — no semaphore signal needed.
        // Binary semaphore is a Vulkan concept; DXGI Present handles all sync internally.
        (void)signal;

        return data->currentBackBufferIndex;
    }

    // =========================================================================
    // GetSwapchainTextureImpl
    // =========================================================================

    auto D3D12Device::GetSwapchainTextureImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle {
        auto* data = swapchains_.Lookup(h);
        if (!data || imageIndex >= data->textureHandles.size()) {
            return {};
        }
        return data->textureHandles[imageIndex];
    }

    auto D3D12Device::GetSwapchainTextureViewImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureViewHandle {
        auto* data = swapchains_.Lookup(h);
        if (!data || imageIndex >= data->textureViewHandles.size()) {
            return {};
        }
        return data->textureViewHandles[imageIndex];
    }

    auto D3D12Device::GetSwapchainImageCountImpl(SwapchainHandle h) -> uint32_t {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return 0;
        }
        return static_cast<uint32_t>(data->backBuffers.size());
    }

    // =========================================================================
    // PresentImpl
    // =========================================================================

    void D3D12Device::PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores) {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return;
        }

        // D3D12/DXGI: Present implicitly waits for GPU work on the back buffer.
        // Binary semaphore waits are a Vulkan concept; skip them here.
        (void)waitSemaphores;

        // Present with tearing support (Immediate/Mailbox: syncInterval=0)
        UINT syncInterval = 0;  // VSync off by default; production code maps PresentMode
        UINT flags = DXGI_PRESENT_ALLOW_TEARING;

        HRESULT hr = data->swapchain->Present(syncInterval, flags);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "[D3D12] Device removed during Present");
        } else if (FAILED(hr)) {
            MIKI_LOG_WARN(
                ::miki::debug::LogCategory::Rhi, "[D3D12] Present failed: 0x{:08X}", static_cast<uint32_t>(hr)
            );
        }
    }

    // =========================================================================
    // Surface capability query
    // =========================================================================

    auto D3D12Device::GetSurfaceCapabilitiesImpl([[maybe_unused]] const NativeWindowHandle& window) const
        -> RenderSurfaceCapabilities {
        RenderSurfaceCapabilities caps;

        // DXGI flip model supports a limited set of formats.
        // SRGB formats are supported via UNORM swapchain + SRGB RTV view (handled in CreateSwapchain).
        caps.supportedFormats = {
            Format::BGRA8_UNORM, Format::BGRA8_SRGB,   Format::RGBA8_UNORM,
            Format::RGBA8_SRGB,  Format::RGBA16_FLOAT, Format::RGB10A2_UNORM,
        };
        caps.supportedPresentModes = {PresentMode::Fifo, PresentMode::Immediate};

        // Check tearing support (VRR / Mailbox-like)
        if (factory_) {
            BOOL tearingSupported = FALSE;
            HRESULT hr = factory_->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported)
            );
            if (SUCCEEDED(hr) && tearingSupported) {
                caps.supportedPresentModes.push_back(PresentMode::Mailbox);
            }
        }

        caps.supportedColorSpaces = {SurfaceColorSpace::SRGB};

        // Check HDR support via DXGI output
        if (factory_) {
            ComPtr<IDXGIAdapter1> adapter;
            if (SUCCEEDED(factory_->EnumAdapters1(0, &adapter))) {
                ComPtr<IDXGIOutput> output;
                if (SUCCEEDED(adapter->EnumOutputs(0, &output))) {
                    ComPtr<IDXGIOutput6> output6;
                    if (SUCCEEDED(output.As(&output6))) {
                        DXGI_OUTPUT_DESC1 desc1{};
                        if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                            if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                                caps.supportedColorSpaces.push_back(SurfaceColorSpace::HDR10_ST2084);
                            }
                            if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
                                || desc1.MaxLuminance > 0.0f) {
                                caps.supportedColorSpaces.push_back(SurfaceColorSpace::scRGBLinear);
                            }
                        }
                    }
                }
            }
        }

        caps.minExtent = {1, 1};
        caps.maxExtent = {16384, 16384};
        caps.minImageCount = 2;
        caps.maxImageCount = 16;

        return caps;
    }

}  // namespace miki::rhi

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif