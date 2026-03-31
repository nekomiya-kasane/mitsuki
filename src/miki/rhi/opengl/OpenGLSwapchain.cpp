/** @file OpenGLSwapchain.cpp
 *  @brief OpenGL 4.3+ (Tier 4) backend — Swapchain via GLFW default framebuffer.
 *
 *  OpenGL swapchain is the default framebuffer (FBO 0) managed by GLFW.
 *  "Acquire" is a no-op (single-buffered from RHI perspective).
 *  "Present" calls glfwSwapBuffers.
 *  A dummy texture handle is registered for GetSwapchainTexture compatibility.
 */

#include "miki/rhi/backend/OpenGLDevice.h"

#include "miki/platform/WindowManager.h"

namespace miki::rhi {

    auto OpenGLDevice::CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle> {
        // Create a dummy texture handle representing the default framebuffer
        auto [texHandle, texData] = textures_.Allocate();
        if (!texData) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        texData->texture = 0;  // Default framebuffer
        texData->target = GL_TEXTURE_2D;
        texData->internalFormat = GL_RGBA8;
        texData->width = desc.width;
        texData->height = desc.height;
        texData->depth = 1;
        texData->mipLevels = 1;
        texData->arrayLayers = 1;
        texData->dimension = TextureDimension::Tex2D;  // Swapchain images are always 2D
        texData->ownsTexture = false;

        // Pre-create TextureView for the color texture (swapchain owns this view)
        TextureViewDesc tvd{.texture = texHandle};
        auto viewResult = CreateTextureViewImpl(tvd);
        if (!viewResult) {
            textures_.Free(texHandle);
            return std::unexpected(RhiError::TooManyObjects);
        }
        TextureViewHandle texViewHandle = *viewResult;

        auto [handle, data] = swapchains_.Allocate();
        if (!data) {
            DestroyTextureViewImpl(texViewHandle);
            textures_.Free(texHandle);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->width = desc.width;
        data->height = desc.height;
        data->colorTexture = texHandle;
        data->colorTextureView = texViewHandle;
        data->windowBackend = windowBackend_;
        data->nativeToken = nativeToken_;
        data->currentImage = 0;

        return handle;
    }

    void OpenGLDevice::DestroySwapchainImpl(SwapchainHandle h) {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return;
        }
        // Destroy texture view first (it references the texture)
        if (data->colorTextureView.IsValid()) {
            DestroyTextureViewImpl(data->colorTextureView);
        }

        if (data->colorTexture.IsValid()) {
            auto* texData = textures_.Lookup(data->colorTexture);
            if (texData) {
                texData->texture = 0;
                textures_.Free(data->colorTexture);
            }
        }
        swapchains_.Free(h);
    }

    auto OpenGLDevice::ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void> {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }
        data->width = w;
        data->height = ht;

        // Update the dummy texture dimensions
        auto* texData = textures_.Lookup(data->colorTexture);
        if (texData) {
            texData->width = w;
            texData->height = ht;
        }

        // GL viewport will be set by CmdSetViewport at render time
        return {};
    }

    auto OpenGLDevice::AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle)
        -> RhiResult<uint32_t> {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Signal semaphore immediately (GL is single-queue, no acquire sync needed)
        if (signal.IsValid()) {
            auto* semData = semaphores_.Lookup(signal);
            if (semData) {
                if (semData->sync) {
                    gl_->DeleteSync(semData->sync);
                }
                semData->sync = gl_->FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                ++semData->value;
            }
        }

        return data->currentImage;  // Always 0 for default framebuffer
    }

    auto OpenGLDevice::GetSwapchainTextureImpl(SwapchainHandle h, uint32_t) -> TextureHandle {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return {};
        }
        return data->colorTexture;
    }

    auto OpenGLDevice::GetSwapchainTextureViewImpl(SwapchainHandle h, uint32_t) -> TextureViewHandle {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return {};
        }
        return data->colorTextureView;
    }

    auto OpenGLDevice::GetSwapchainImageCountImpl([[maybe_unused]] SwapchainHandle h) -> uint32_t {
        // TODO(Nekomiya) I'm not sure if this is true
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return 1;
        }
        // OpenGL default FBO is typically double-buffered, but we report 1 for RHI abstraction
        // since we treat it as a single logical image from the application's perspective
        return 1;
    }

    void OpenGLDevice::PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle>) {
        auto* data = swapchains_.Lookup(h);
        if (!data) {
            return;
        }

        // Bind default framebuffer before swap
        gl_->BindFramebuffer(GL_FRAMEBUFFER, 0);

        // Swap buffers via window backend
        if (data->windowBackend) {
            data->windowBackend->SwapBuffers(data->nativeToken);
        }
    }

    // =========================================================================
    // Surface capability query
    // =========================================================================

    auto OpenGLDevice::GetSurfaceCapabilitiesImpl(const NativeWindowHandle& window) const -> RenderSurfaceCapabilities {
        (void)window;
        RenderSurfaceCapabilities caps;
        // OpenGL default framebuffer is always SRGB 8-bit
        caps.supportedFormats = {Format::BGRA8_SRGB, Format::RGBA8_SRGB};
        caps.supportedPresentModes = {PresentMode::Fifo};  // glfwSwapInterval(1) only
        caps.supportedColorSpaces = {SurfaceColorSpace::SRGB};
        caps.minExtent = {1, 1};
        caps.maxExtent = {16384, 16384};
        caps.minImageCount = 2;
        caps.maxImageCount = 2;  // Double-buffered default FBO
        return caps;
    }

}  // namespace miki::rhi
