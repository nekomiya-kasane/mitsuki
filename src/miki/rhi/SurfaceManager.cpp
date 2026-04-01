/** @file SurfaceManager.cpp
 *  @brief SurfaceManager PIMPL implementation.
 *
 *  Internal data structure: flat_hash_map<WindowHandle, SurfaceEntry>.
 *  SurfaceEntry owns a unique_ptr<RenderSurface> + FrameManager per window.
 *
 *  All operations are main-thread only — no locking required.
 */

#include "miki/rhi/SurfaceManager.h"

#include <cassert>
#include <unordered_map>

#include "miki/platform/WindowManager.h"

namespace miki::rhi {

    // =========================================================================
    // WindowHandle hash for unordered_map
    // =========================================================================

    struct WindowHandleHash {
        auto operator()(platform::WindowHandle h) const noexcept -> std::size_t {
            // Combine id + generation into a single hash.
            // id is the primary discriminator; generation prevents ABA collisions.
            return std::hash<uint64_t>{}(static_cast<uint64_t>(h.id) | (static_cast<uint64_t>(h.generation) << 32));
        }
    };

    // =========================================================================
    // SurfaceEntry — per-window owned resources
    // =========================================================================

    // TODO(architecture): Resource overhead of per-window FrameManager
    //
    // Current design: Each window has an independent FrameManager with its own CommandPoolAllocator.
    // Resource overhead: N windows × (slots × queues × threads) CommandPools + synchronization primitives.
    //
    // Advantages:
    //   - Complete isolation, no synchronization issues
    //   - Independent lifecycle management per window
    //   - Simple implementation
    //
    // Disadvantages:
    //   - 8 windows = 16 CommandPools + 16 Fences + 32 Semaphores (default config)
    //   - Resource waste in extreme multi-window scenarios (CAD multi-viewport)
    //
    // Potential optimizations (if needed):
    //   - Share CommandPoolAllocator, keep only FrameContext (sync primitives) per window
    //   - References: UE5 global CommandPool pool, Godot 4 per-window RenderingDevice
    //
    // Conclusion: Current design is correct and safe, overhead is acceptable for 8-window limit. No changes for now.
    // References: Vulkan Guide multithreading, ARM Mobile Best Practices
    //
    struct SurfaceEntry {
        std::unique_ptr<RenderSurface> surface;
        frame::FrameManager frameManager;
        RenderSurfaceConfig config;

        SurfaceEntry(
            std::unique_ptr<RenderSurface> iSurface, frame::FrameManager iFrameManager, RenderSurfaceConfig iConfig
        )
            : surface(std::move(iSurface)), frameManager(std::move(iFrameManager)), config(iConfig) {}
    };

    // =========================================================================
    // SurfaceManager::Impl
    // =========================================================================

    struct SurfaceManager::Impl {
        DeviceHandle device;
        platform::WindowManager* windowManager = nullptr;  // TODO (Nekomiya) bad smell
        std::unordered_map<platform::WindowHandle, SurfaceEntry, WindowHandleHash> surfaces;

        Impl(DeviceHandle iDevice, platform::WindowManager& iWM) : device(iDevice), windowManager(&iWM) {}

        auto Find(platform::WindowHandle iWindow) -> SurfaceEntry* {
            auto it = surfaces.find(iWindow);
            return it != surfaces.end() ? &it->second : nullptr;
        }

        auto Find(platform::WindowHandle iWindow) const -> const SurfaceEntry* {
            auto it = surfaces.find(iWindow);
            return it != surfaces.end() ? &it->second : nullptr;
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SurfaceManager::~SurfaceManager() {
        if (impl_) {
            // Drain all surfaces before destruction.
            WaitAll();
        }
    }

    SurfaceManager::SurfaceManager(SurfaceManager&&) noexcept = default;
    auto SurfaceManager::operator=(SurfaceManager&&) noexcept -> SurfaceManager& = default;
    SurfaceManager::SurfaceManager(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto SurfaceManager::Create(DeviceHandle iDevice, platform::WindowManager& iWindowManager)
        -> core::Result<SurfaceManager> {
        if (!iDevice.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return SurfaceManager(std::make_unique<Impl>(iDevice, iWindowManager));
    }

    // =========================================================================
    // Surface lifecycle
    // =========================================================================

    auto SurfaceManager::AttachSurface(platform::WindowHandle iWindow, const RenderSurfaceConfig& iConfig)
        -> core::Result<void> {
        assert(impl_ && "SurfaceManager used after move");

        if (!iWindow.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (impl_->Find(iWindow)) {
            return std::unexpected(core::ErrorCode::InvalidState);  // Double-attach
        }

        // 1. Query window state from WindowManager (authoritative source for native handle + size)
        auto info = impl_->windowManager->GetWindowInfo(iWindow);
        if (!info.alive) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        uint32_t w = info.extent.width;
        uint32_t h = info.extent.height;
        if (w == 0 || h == 0) {
            return std::unexpected(core::ErrorCode::InvalidState);  // Minimized — cannot attach
        }

        // 2. Create RenderSurface (platform surface handle)
        auto surfaceResult = RenderSurface::Create(impl_->device, info.nativeWindow);
        if (!surfaceResult) {
            return std::unexpected(surfaceResult.error());
        }

        auto& surface = *surfaceResult;

        // 3. Configure swapchain (RenderSurfaceConfig -> SwapchainDesc internally)
        auto configResult = surface->Configure(iConfig, w, h);
        if (!configResult) {
            return std::unexpected(configResult.error());
        }

        // 4. Create FrameManager bound to this surface
        auto fmResult = frame::FrameManager::Create(impl_->device, *surface);
        if (!fmResult) {
            return std::unexpected(fmResult.error());
        }

        // 5. Insert into map
        impl_->surfaces.emplace(iWindow, SurfaceEntry{std::move(surface), std::move(*fmResult), iConfig});
        return {};
    }

    auto SurfaceManager::DetachSurface(platform::WindowHandle iWindow) -> core::Result<void> {
        assert(impl_ && "SurfaceManager used after move");

        auto it = impl_->surfaces.find(iWindow);
        if (it == impl_->surfaces.end()) {
            return std::unexpected(core::ErrorCode::ResourceNotFound);
        }

        // Per-surface timeline wait — only this window's GPU work is drained.
        it->second.frameManager.WaitAll();

        impl_->surfaces.erase(it);
        return {};
    }

    auto SurfaceManager::DetachSurfaces(std::span<const platform::WindowHandle> iWindows) -> core::Result<void> {
        assert(impl_ && "SurfaceManager used after move");

        for (auto w : iWindows) {
            auto it = impl_->surfaces.find(w);
            if (it == impl_->surfaces.end()) {
                continue;  // Silently skip windows without surfaces
            }

            it->second.frameManager.WaitAll();
            impl_->surfaces.erase(it);
        }
        return {};
    }

    auto SurfaceManager::HasSurface(platform::WindowHandle iWindow) const -> bool {
        assert(impl_ && "SurfaceManager used after move");
        return impl_->Find(iWindow) != nullptr;
    }

    // =========================================================================
    // Per-window access
    // =========================================================================

    auto SurfaceManager::GetRenderSurface(platform::WindowHandle iWindow) -> RenderSurface* {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        return entry ? entry->surface.get() : nullptr;
    }

    auto SurfaceManager::GetFrameManager(platform::WindowHandle iWindow) -> frame::FrameManager* {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        return entry ? &entry->frameManager : nullptr;
    }

    // =========================================================================
    // Frame operations
    // =========================================================================

    auto SurfaceManager::BeginFrame(platform::WindowHandle iWindow) -> core::Result<frame::FrameContext> {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        if (!entry) {
            return std::unexpected(core::ErrorCode::ResourceNotFound);
        }
        return entry->frameManager.BeginFrame();
    }

    auto SurfaceManager::EndFrame(platform::WindowHandle iWindow, CommandBufferHandle iCmd) -> core::Result<void> {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        if (!entry) {
            return std::unexpected(core::ErrorCode::ResourceNotFound);
        }
        return entry->frameManager.EndFrame(iCmd);
    }

    auto SurfaceManager::ResizeSurface(platform::WindowHandle iWindow, uint32_t iWidth, uint32_t iHeight)
        -> core::Result<void> {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        if (!entry) {
            return std::unexpected(core::ErrorCode::ResourceNotFound);
        }

        // FrameManager::Resize handles WaitAll + swapchain recreation internally.
        return entry->frameManager.Resize(iWidth, iHeight);
    }

    // =========================================================================
    // Dynamic present configuration
    // =========================================================================

    auto SurfaceManager::SetPresentMode(platform::WindowHandle iWindow, PresentMode iMode) -> core::Result<void> {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        if (!entry) {
            return std::unexpected(core::ErrorCode::ResourceNotFound);
        }

        entry->config.presentMode = iMode;
        return entry->frameManager.Reconfigure(entry->config);
    }

    auto SurfaceManager::SetColorSpace(platform::WindowHandle iWindow, SurfaceColorSpace iSpace) -> core::Result<void> {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        if (!entry) {
            return std::unexpected(core::ErrorCode::ResourceNotFound);
        }

        entry->config.colorSpace = iSpace;
        return entry->frameManager.Reconfigure(entry->config);
    }

    auto SurfaceManager::GetSupportedPresentModes(platform::WindowHandle iWindow) const -> std::vector<PresentMode> {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        if (!entry) {
            return {PresentMode::Fifo};  // Fifo is universally required
        }
        auto caps = entry->surface->GetCapabilities();
        return caps.supportedPresentModes;
    }

    auto SurfaceManager::GetSupportedColorSpaces(platform::WindowHandle iWindow) const
        -> std::vector<SurfaceColorSpace> {
        assert(impl_ && "SurfaceManager used after move");
        auto* entry = impl_->Find(iWindow);
        if (!entry) {
            return {SurfaceColorSpace::SRGB};  // SRGB is universally required
        }
        auto caps = entry->surface->GetCapabilities();
        return caps.supportedColorSpaces;
    }

    // =========================================================================
    // Bulk operations
    // =========================================================================

    auto SurfaceManager::WaitAll() -> void {
        assert(impl_ && "SurfaceManager used after move");
        for (auto& [handle, entry] : impl_->surfaces) {
            entry.frameManager.WaitAll();
        }
    }

}  // namespace miki::rhi
