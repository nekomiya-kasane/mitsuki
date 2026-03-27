/** @brief WindowManager implementation.
 *
 * Owns per-window WindowSlot (native token + state). No GPU resources.
 * Delegates OS window ops to IWindowBackend. Stable monotonic handle IDs.
 * PollEvents() collects per-window events from backend, detects close requests,
 * and updates minimized state.
 */

#include "miki/platform/WindowManager.h"

#include "miki/core/ErrorCode.h"

#include <algorithm>
#include <ranges>

namespace miki::platform {

    // ===========================================================================
    // Internal WindowSlot
    // ===========================================================================

    struct WindowSlot {
        uint32_t id = 0;
        void* nativeToken = nullptr;
        rhi::NativeWindowHandle nativeWindow = rhi::Win32Window{};
        bool alive = false;
        bool minimized = false;
    };

    // ===========================================================================
    // WindowManager::Impl
    // ===========================================================================

    struct WindowManager::Impl {
        std::unique_ptr<IWindowBackend> backend;
        std::vector<WindowSlot> slots;
        std::vector<WindowEvent> eventBuffer;
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

    WindowManager::WindowManager(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    WindowManager::~WindowManager() {
        if (!impl_) {
            return;
        }
        for (auto& slot : impl_->slots) {
            if (!slot.alive) {
                continue;
            }
            if (impl_->backend && slot.nativeToken) {
                impl_->backend->DestroyNativeWindow(slot.nativeToken);
            }
            slot.alive = false;
        }
    }

    WindowManager::WindowManager(WindowManager&&) noexcept = default;
    auto WindowManager::operator=(WindowManager&&) noexcept -> WindowManager& = default;

    auto WindowManager::Create(std::unique_ptr<IWindowBackend> iBackend) -> miki::core::Result<WindowManager> {
        if (!iBackend) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }
        auto impl = std::make_unique<Impl>();
        impl->backend = std::move(iBackend);
        return WindowManager{std::move(impl)};
    }

    // ===========================================================================
    // Window lifecycle
    // ===========================================================================

    auto WindowManager::CreateWindow(const WindowDesc& iDesc) -> miki::core::Result<WindowHandle> {
        if (!impl_ || !impl_->backend) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }
        if (impl_->AliveCount() >= kMaxWindows) {
            return std::unexpected(miki::core::ErrorCode::ResourceExhausted);
        }
        if (iDesc.width == 0 || iDesc.height == 0) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }

        void* nativeToken = nullptr;
        auto nativeResult = impl_->backend->CreateNativeWindow(iDesc, nativeToken);
        if (!nativeResult) {
            return std::unexpected(nativeResult.error());
        }

        uint32_t id = impl_->nextId++;
        WindowHandle handle{id};

        // Inform backend of the assigned handle (for event tagging in callbacks)
        impl_->backend->SetWindowHandle(nativeToken, handle);

        impl_->slots.push_back(
            WindowSlot{
                .id = id,
                .nativeToken = nativeToken,
                .nativeWindow = *nativeResult,
                .alive = true,
                .minimized = false,
            }
        );
        return handle;
    }

    auto WindowManager::DestroyWindow(WindowHandle iHandle) -> miki::core::Result<void> {
        if (!impl_) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }

        auto* slot = impl_->FindSlot(iHandle);
        if (!slot) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }

        if (impl_->backend && slot->nativeToken) {
            impl_->backend->DestroyNativeWindow(slot->nativeToken);
            slot->nativeToken = nullptr;
        }

        slot->alive = false;
        return {};
    }

    // ===========================================================================
    // Query
    // ===========================================================================

    auto WindowManager::GetWindowInfo(WindowHandle iHandle) const -> WindowInfo {
        if (!impl_) {
            return {};
        }
        auto* slot = impl_->FindSlotConst(iHandle);
        if (!slot) {
            return {};
        }

        auto ext = (impl_->backend && slot->nativeToken) ? impl_->backend->GetFramebufferSize(slot->nativeToken)
                                                         : rhi::Extent2D{};

        return WindowInfo{
            .handle = {slot->id},
            .extent = ext,
            .nativeWindow = slot->nativeWindow,
            .alive = slot->alive,
            .minimized = slot->minimized,
        };
    }

    auto WindowManager::GetNativeHandle(WindowHandle iHandle) const -> rhi::NativeWindowHandle {
        if (!impl_) {
            return rhi::Win32Window{};
        }
        auto* slot = impl_->FindSlotConst(iHandle);
        return slot ? slot->nativeWindow : rhi::NativeWindowHandle{rhi::Win32Window{}};
    }

    auto WindowManager::GetNativeToken(WindowHandle iHandle) const -> void* {
        if (!impl_) {
            return nullptr;
        }
        auto* slot = impl_->FindSlotConst(iHandle);
        return slot ? slot->nativeToken : nullptr;
    }

    // ===========================================================================
    // Enumeration
    // ===========================================================================

    auto WindowManager::GetWindowCount() const noexcept -> uint32_t {
        return impl_ ? impl_->AliveCount() : 0;
    }

    auto WindowManager::GetAllWindows() const -> std::vector<WindowHandle> {
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

    auto WindowManager::GetActiveWindows() -> std::vector<WindowHandle> {
        std::vector<WindowHandle> result;
        if (!impl_) {
            return result;
        }
        for (auto& s : impl_->slots) {
            if (!s.alive) {
                continue;
            }
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

    auto WindowManager::PollEvents() -> std::span<const WindowEvent> {
        if (!impl_ || !impl_->backend) {
            return {};
        }

        impl_->eventBuffer.clear();
        impl_->backend->PollEvents(impl_->eventBuffer);

        // Post-poll: detect close requests and update minimized state
        for (auto& slot : impl_->slots) {
            if (!slot.alive || !slot.nativeToken) {
                continue;
            }

            if (impl_->backend->ShouldClose(slot.nativeToken)) {
                impl_->backend->DestroyNativeWindow(slot.nativeToken);
                slot.nativeToken = nullptr;
                slot.alive = false;
                continue;
            }

            slot.minimized = impl_->backend->IsMinimized(slot.nativeToken);
        }

        return impl_->eventBuffer;
    }

    auto WindowManager::ShouldClose() const -> bool {
        if (!impl_) {
            return true;
        }
        return impl_->AliveCount() == 0;
    }

}  // namespace miki::platform
