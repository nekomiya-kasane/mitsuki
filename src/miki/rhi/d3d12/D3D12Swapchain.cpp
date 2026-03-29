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
            texData->format = swapDesc.Format;
            texData->width = desc.width;
            texData->height = desc.height;
            texData->depth = 1;
            texData->mipLevels = 1;
            texData->arrayLayers = 1;
            texData->ownsResource = false;
            textureHandles.push_back(texHandle);

            // Create RTV
            uint32_t rtvOffset = rtvHeap_.Allocate(1);
            if (rtvOffset == UINT32_MAX) {
                return std::unexpected(RhiError::TooManyObjects);
            }
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_.GetCpuHandle(rtvOffset);
            device_->CreateRenderTargetView(backBuffers[i].Get(), nullptr, rtvHandle);
            rtvHandles.push_back(rtvHandle);
        }

        auto [handle, data] = swapchains_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->swapchain = std::move(swapchain4);
        data->backBuffers = std::move(backBuffers);
        data->textureHandles = std::move(textureHandles);
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

        uint32_t bufferCount = 2;  // Preserve buffer count
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

        // Signal the semaphore (D3D12 doesn't have a separate acquire sync point)
        if (signal.IsValid()) {
            auto* semData = semaphores_.Lookup(signal);
            if (semData) {
                ++semData->value;
                queues_.graphics->Signal(semData->fence.Get(), semData->value);
            }
        }

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

    // =========================================================================
    // PresentImpl
    // =========================================================================

    void D3D12Device::PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores) {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return;
        }

        // Wait for rendering to complete (GPU-side waits)
        for (auto& sem : waitSemaphores) {
            auto* semData = semaphores_.Lookup(sem);
            if (semData) {
                queues_.graphics->Wait(semData->fence.Get(), semData->value);
            }
        }

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

}  // namespace miki::rhi

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif