/** @brief MultiWindowManager implementation.
 *
 * Owns per-window WindowSlot (RenderSurface + FrameManager + native token).
 * Delegates OS window ops to IWindowBackend. Stable monotonic handle IDs.
 */

#include "miki/rhi/MultiWindowManager.h"

#include "miki/core/ErrorCode.h"
#include "miki/rhi/IDevice.h"

#include <algorithm>
#include <optional>
#include <ranges>

namespace miki::rhi {

    // ===========================================================================
    // Internal WindowSlot
    // ===========================================================================

    struct WindowSlot {
        uint32_t id = 0;
        void* nativeToken = nullptr;
        NativeWindowHandle nativeWindow = Win32Window{};
        std::unique_ptr<RenderSurface> surface;
        std::optional<FrameManager> frameManager;
        bool alive = false;
        bool minimized = false;
    };

    // ===========================================================================
    // MultiWindowManager::Impl
    // ===========================================================================

    struct MultiWindowManager::Impl {
        IDevice* device = nullptr;
        std::unique_ptr<IWindowBackend> backend;
        std::vector<WindowSlot> slots;
        uint32_t nextId = 1;  // 0 = invalid sentinel

        [[nodiscard]] auto FindSlot(WindowHandle iHandle) -> WindowSlot* {
            for (auto& s : slots) {
                if (s.id == iHandle.id && s.alive) {
                    return &s;
                }
            }
            return nullptr;
        }

        [[nodiscard]] auto FindSlotConst(WindowHandle iHandle) const -> const WindowSlot* {
            for (auto const& s : slots) {
                if (s.id == iHandle.id && s.alive) {
                    return &s;
                }
            }
            return nullptr;
        }

        [[nodiscard]] auto AliveCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(std::ranges::count_if(slots, [](auto const& s) { return s.alive; }));
        }
    };

    // ===========================================================================
    // Lifecycle
    // ===========================================================================

    MultiWindowManager::MultiWindowManager(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    MultiWindowManager::~MultiWindowManager() {
        if (!impl_) {
            return;
        }
        // Destroy all alive windows
        for (auto& slot : impl_->slots) {
            if (!slot.alive) {
                continue;
            }
            if (slot.frameManager) {
                slot.frameManager->WaitAll();
            }
            slot.surface.reset();
            slot.frameManager.reset();
            if (impl_->backend && slot.nativeToken) {
                impl_->backend->DestroyNativeWindow(slot.nativeToken);
            }
            slot.alive = false;
        }
    }

    MultiWindowManager::MultiWindowManager(MultiWindowManager&&) noexcept = default;
    auto MultiWindowManager::operator=(MultiWindowManager&&) noexcept -> MultiWindowManager& = default;

    auto MultiWindowManager::Create(IDevice& iDevice, std::unique_ptr<IWindowBackend> iBackend)
        -> miki::core::Result<MultiWindowManager> {
        if (!iBackend) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }
        auto impl = std::make_unique<Impl>();
        impl->device = &iDevice;
        impl->backend = std::move(iBackend);
        return MultiWindowManager{std::move(impl)};
    }

    // ===========================================================================
    // Window lifecycle
    // ===========================================================================

    auto MultiWindowManager::CreateWindow(const WindowDesc& iDesc) -> miki::core::Result<WindowHandle> {
        if (!impl_ || !impl_->device || !impl_->backend) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }
        if (impl_->AliveCount() >= kMaxWindows) {
            return std::unexpected(miki::core::ErrorCode::ResourceExhausted);
        }
        if (iDesc.width == 0 || iDesc.height == 0) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }

        // Create OS window via backend
        void* nativeToken = nullptr;
        auto nativeResult = impl_->backend->CreateNativeWindow(iDesc, nativeToken);
        if (!nativeResult) {
            return std::unexpected(nativeResult.error());
        }

        // Build RenderSurfaceDesc from native window handle + config
        auto config = iDesc.surfaceConfig;
        if (config.width == 0) {
            config.width = iDesc.width;
        }
        if (config.height == 0) {
            config.height = iDesc.height;
        }

        RenderSurfaceDesc rsDesc{
            .window = *nativeResult,
            .initialConfig = config,
        };

        // Create RenderSurface
        auto surfaceResult = RenderSurface::Create(*impl_->device, rsDesc);
        if (!surfaceResult) {
            impl_->backend->DestroyNativeWindow(nativeToken);
            return std::unexpected(surfaceResult.error());
        }

        // Create FrameManager bound to surface
        auto fmResult = FrameManager::Create(*impl_->device, **surfaceResult);
        if (!fmResult) {
            impl_->backend->DestroyNativeWindow(nativeToken);
            return std::unexpected(fmResult.error());
        }

        // Allocate slot
        uint32_t id = impl_->nextId++;
        WindowSlot slot;
        slot.id = id;
        slot.nativeToken = nativeToken;
        slot.nativeWindow = *nativeResult;
        slot.surface = std::move(*surfaceResult);
        slot.frameManager = std::move(*fmResult);
        slot.alive = true;
        slot.minimized = false;

        impl_->slots.push_back(std::move(slot));
        return WindowHandle{id};
    }

    auto MultiWindowManager::DestroyWindow(WindowHandle iHandle) -> miki::core::Result<void> {
        if (!impl_) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }

        auto* slot = impl_->FindSlot(iHandle);
        if (!slot) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }

        // Per-surface WaitIdle (not global)
        if (slot->frameManager) {
            slot->frameManager->WaitAll();
        }
        slot->surface.reset();
        slot->frameManager.reset();

        if (impl_->backend && slot->nativeToken) {
            impl_->backend->DestroyNativeWindow(slot->nativeToken);
            slot->nativeToken = nullptr;
        }

        slot->alive = false;
        return {};
    }

    // ===========================================================================
    // Per-window resource access
    // ===========================================================================

    auto MultiWindowManager::GetRenderSurface(WindowHandle iHandle) -> RenderSurface* {
        if (!impl_) {
            return nullptr;
        }
        auto* slot = impl_->FindSlot(iHandle);
        return slot ? slot->surface.get() : nullptr;
    }

    auto MultiWindowManager::GetFrameManager(WindowHandle iHandle) -> FrameManager* {
        if (!impl_) {
            return nullptr;
        }
        auto* slot = impl_->FindSlot(iHandle);
        return (slot && slot->frameManager) ? &*slot->frameManager : nullptr;
    }

    auto MultiWindowManager::GetWindowInfo(WindowHandle iHandle) const -> WindowInfo {
        if (!impl_) {
            return {};
        }
        auto* slot = impl_->FindSlotConst(iHandle);
        if (!slot) {
            return {};
        }
        auto ext = slot->surface ? slot->surface->GetExtent() : Extent2D{};
        return WindowInfo{
            .handle = {slot->id},
            .extent = ext,
            .alive = slot->alive,
            .minimized = slot->minimized,
        };
    }

    // ===========================================================================
    // Enumeration
    // ===========================================================================

    auto MultiWindowManager::GetWindowCount() const noexcept -> uint32_t {
        return impl_ ? impl_->AliveCount() : 0;
    }

    auto MultiWindowManager::GetAllWindows() const -> std::vector<WindowHandle> {
        std::vector<WindowHandle> result;
        if (!impl_) {
            return result;
        }
        for (auto const& s : impl_->slots) {
            if (s.alive) {
                result.push_back({s.id});
            }
        }
        return result;
    }

    auto MultiWindowManager::GetActiveWindows() -> std::vector<WindowHandle> {
        std::vector<WindowHandle> result;
        if (!impl_) {
            return result;
        }
        for (auto& s : impl_->slots) {
            if (!s.alive) {
                continue;
            }
            // Update minimized state from backend
            if (impl_->backend && s.nativeToken) {
                s.minimized = impl_->backend->IsMinimized(s.nativeToken);
            }
            if (!s.minimized) {
                result.push_back({s.id});
            }
        }
        return result;
    }

    // ===========================================================================
    // Frame-level operations
    // ===========================================================================

    auto MultiWindowManager::PollEvents() -> void {
        if (impl_ && impl_->backend) {
            impl_->backend->PollEvents();
        }
    }

    auto MultiWindowManager::ShouldClose() const -> bool {
        if (!impl_) {
            return true;
        }
        return impl_->AliveCount() == 0;
    }

    auto MultiWindowManager::ProcessWindowEvents() -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }

        for (auto& slot : impl_->slots) {
            if (!slot.alive || !slot.nativeToken) {
                continue;
            }

            // Check close request
            if (impl_->backend->ShouldClose(slot.nativeToken)) {
                if (slot.frameManager) {
                    slot.frameManager->WaitAll();
                }
                slot.surface.reset();
                slot.frameManager.reset();
                impl_->backend->DestroyNativeWindow(slot.nativeToken);
                slot.nativeToken = nullptr;
                slot.alive = false;
                continue;
            }

            // Update minimized state
            slot.minimized = impl_->backend->IsMinimized(slot.nativeToken);

            // Detect resize
            if (slot.surface && !slot.minimized) {
                auto fbSize = impl_->backend->GetFramebufferSize(slot.nativeToken);
                auto curExt = slot.surface->GetExtent();
                if (fbSize.width != curExt.width || fbSize.height != curExt.height) {
                    if (fbSize.width > 0 && fbSize.height > 0) {
                        (void)slot.surface->Resize(fbSize.width, fbSize.height);
                    }
                }
            }
        }
    }

}  // namespace miki::rhi
