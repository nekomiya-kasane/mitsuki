/** @brief WindowManager implementation — N-ary forest with cascade destruction.
 *
 * Uses ChunkedSlotMap<WindowNode, 16> for O(1) lookup with generation-counted handles.
 * Tree structure: each node stores parent handle + children vector.
 * Cascade destroy: post-order traversal (leaves first).
 * PollEvents() does NOT auto-destroy — emits CloseRequested events for user decision.
 *
 * See: specs/01-window-manager.md §3–§8.
 */

#include "miki/platform/WindowManager.h"

#include "miki/core/ChunkedSlotMap.h"
#include "miki/core/ErrorCode.h"

#include <cassert>

namespace miki::platform {

    // ===========================================================================
    // WindowNode — internal per-window state (stored in ChunkedSlotMap)
    // ===========================================================================

    struct WindowNode {
        WindowHandle handle;
        WindowHandle parent;
        std::vector<WindowHandle> children;
        void* nativeToken = nullptr;
        rhi::NativeWindowHandle nativeWindow = rhi::Win32Window{};
        rhi::Extent2D extent = {};
        core::EnumFlags<WindowFlags> flags = WindowFlags::None;
        bool alive = true;
        bool minimized = false;
    };

    // ===========================================================================
    // WindowManager::Impl
    // ===========================================================================

    struct WindowManager::Impl {
        std::unique_ptr<IWindowBackend> backend;
        core::ChunkedSlotMap<WindowNode, 16> nodes;
        std::vector<WindowEvent> eventBuffer;
        HasSurfaceFn hasSurfaceCallback = nullptr;

        [[nodiscard]] auto ToSlot(WindowHandle wh) const -> core::SlotHandle {
            return {.id = wh.id, .generation = wh.generation};
        }

        [[nodiscard]] auto ToWin(core::SlotHandle sh) const -> WindowHandle {
            return {.id = sh.id, .generation = sh.generation};
        }

        [[nodiscard]] auto Find(WindowHandle wh) -> WindowNode* { return nodes.Get(ToSlot(wh)); }

        [[nodiscard]] auto Find(WindowHandle wh) const -> const WindowNode* { return nodes.Get(ToSlot(wh)); }

        [[nodiscard]] auto ComputeDepth(WindowHandle wh) const -> uint32_t {
            uint32_t depth = 0;
            auto cur = wh;
            while (cur.IsValid()) {
                ++depth;
                auto* n = Find(cur);
                if (!n) {
                    break;
                }
                cur = n->parent;
            }
            return depth;
        }

        void CollectPostOrder(WindowHandle wh, std::vector<WindowHandle>& out) const {
            auto* n = Find(wh);
            if (!n) {
                return;
            }
            for (auto& child : n->children) {
                CollectPostOrder(child, out);
                out.push_back(child);
            }
        }

        void DestroyNodeOS(WindowNode& node) {
            if (backend && node.nativeToken) {
                backend->DestroyNativeWindow(node.nativeToken);
                node.nativeToken = nullptr;
            }
            node.alive = false;
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
        auto cleanup = [&](core::SlotHandle, WindowNode& node) {
            if (node.alive) {
                impl_->DestroyNodeOS(node);
            }
        };
        impl_->nodes.ForEach(cleanup);
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
        if (iDesc.width == 0 || iDesc.height == 0) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }

        // Validate parent & depth
        void* parentToken = nullptr;
        if (iDesc.parent.IsValid()) {
            auto* pn = impl_->Find(iDesc.parent);
            if (!pn || !pn->alive) {
                return std::unexpected(miki::core::ErrorCode::InvalidArgument);
            }
            if (impl_->ComputeDepth(iDesc.parent) >= kMaxDepth) {
                return std::unexpected(miki::core::ErrorCode::InvalidArgument);
            }
            parentToken = pn->nativeToken;
        }

        void* nativeToken = nullptr;
        auto nativeResult = impl_->backend->CreateNativeWindow(iDesc, parentToken, nativeToken);
        if (!nativeResult) {
            return std::unexpected(nativeResult.error());
        }

        auto extent = impl_->backend->GetFramebufferSize(nativeToken);

        WindowNode node{};
        node.parent = iDesc.parent;
        node.nativeToken = nativeToken;
        node.nativeWindow = *nativeResult;
        node.extent = extent;
        node.flags = iDesc.flags;
        node.alive = true;

        auto sh = impl_->nodes.Emplace(std::move(node));
        WindowHandle wh = impl_->ToWin(sh);

        // Set handle on the node itself
        impl_->nodes.Get(sh)->handle = wh;

        // Register as child of parent
        if (iDesc.parent.IsValid()) {
            if (auto* pn = impl_->Find(iDesc.parent)) {
                pn->children.push_back(wh);
            }
        }

        impl_->backend->SetWindowHandle(nativeToken, wh);
        return wh;
    }

    auto WindowManager::DestroyWindow(WindowHandle iHandle) -> miki::core::Result<std::vector<WindowHandle>> {
        if (!impl_) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }
        if (!iHandle.IsValid() || !impl_->Find(iHandle)) {
            return std::unexpected(miki::core::ErrorCode::InvalidArgument);
        }

        // Collect victims: descendants in post-order, then self
        std::vector<WindowHandle> victims;
        impl_->CollectPostOrder(iHandle, victims);
        victims.push_back(iHandle);

        // Assert no GPU surfaces attached (debug only)
        if (impl_->hasSurfaceCallback) {
            for (auto& v : victims) {
                assert(!impl_->hasSurfaceCallback(v) && "DetachSurface before DestroyWindow");
            }
        }

        // Destroy in post-order
        for (auto& v : victims) {
            auto* node = impl_->Find(v);
            if (!node) {
                continue;
            }
            impl_->DestroyNodeOS(*node);
            node->children.clear();
            // Remove from parent's children list
            if (node->parent.IsValid()) {
                if (auto* pn = impl_->Find(node->parent)) {
                    auto& pc = pn->children;
                    pc.erase(std::remove(pc.begin(), pc.end(), v), pc.end());
                }
            }
            impl_->nodes.Free(impl_->ToSlot(v));
        }

        return victims;
    }

    auto WindowManager::ShowWindow(WindowHandle iHandle) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->ShowWindow(node->nativeToken);
        }
    }

    auto WindowManager::HideWindow(WindowHandle iHandle) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->HideWindow(node->nativeToken);
        }
    }

    // ===========================================================================
    // Window state operations
    // ===========================================================================

    auto WindowManager::ResizeWindow(WindowHandle iHandle, uint32_t iWidth, uint32_t iHeight) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->ResizeWindow(node->nativeToken, iWidth, iHeight);
            node->extent = impl_->backend->GetFramebufferSize(node->nativeToken);
        }
    }

    auto WindowManager::SetWindowPosition(WindowHandle iHandle, int32_t iX, int32_t iY) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->SetWindowPosition(node->nativeToken, iX, iY);
        }
    }

    auto WindowManager::GetWindowPosition(WindowHandle iHandle) const -> std::pair<int32_t, int32_t> {
        if (!impl_ || !impl_->backend) {
            return {0, 0};
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            return impl_->backend->GetWindowPosition(node->nativeToken);
        }
        return {0, 0};
    }

    auto WindowManager::MinimizeWindow(WindowHandle iHandle) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->MinimizeWindow(node->nativeToken);
            node->minimized = true;
        }
    }

    auto WindowManager::MaximizeWindow(WindowHandle iHandle) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->MaximizeWindow(node->nativeToken);
        }
    }

    auto WindowManager::RestoreWindow(WindowHandle iHandle) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->RestoreWindow(node->nativeToken);
            node->minimized = false;
        }
    }

    auto WindowManager::FocusWindow(WindowHandle iHandle) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->FocusWindow(node->nativeToken);
        }
    }

    auto WindowManager::SetWindowTitle(WindowHandle iHandle, std::string_view iTitle) -> void {
        if (!impl_ || !impl_->backend) {
            return;
        }
        auto* node = impl_->Find(iHandle);
        if (node && node->alive && node->nativeToken) {
            impl_->backend->SetWindowTitle(node->nativeToken, iTitle);
        }
    }

    // ===========================================================================
    // Tree queries
    // ===========================================================================

    auto WindowManager::GetParent(WindowHandle iHandle) const -> WindowHandle {
        if (!impl_) {
            return {};
        }
        auto* n = impl_->Find(iHandle);
        return n ? n->parent : WindowHandle{};
    }

    auto WindowManager::GetChildren(WindowHandle iHandle) const -> std::span<const WindowHandle> {
        if (!impl_) {
            return {};
        }
        auto* n = impl_->Find(iHandle);
        return n ? std::span<const WindowHandle>{n->children} : std::span<const WindowHandle>{};
    }

    auto WindowManager::GetRoot(WindowHandle iHandle) const -> WindowHandle {
        if (!impl_) {
            return {};
        }
        auto cur = iHandle;
        while (true) {
            auto* n = impl_->Find(cur);
            if (!n || !n->parent.IsValid()) {
                return cur;
            }
            cur = n->parent;
        }
    }

    auto WindowManager::GetDepth(WindowHandle iHandle) const -> uint32_t {
        if (!impl_) {
            return 0;
        }
        return impl_->ComputeDepth(iHandle);
    }

    auto WindowManager::GetDescendantsPostOrder(WindowHandle iHandle) const -> std::vector<WindowHandle> {
        std::vector<WindowHandle> result;
        if (!impl_) {
            return result;
        }
        impl_->CollectPostOrder(iHandle, result);
        return result;
    }

    // ===========================================================================
    // State queries
    // ===========================================================================

    auto WindowManager::GetWindowInfo(WindowHandle iHandle) const -> WindowInfo {
        if (!impl_) {
            return {};
        }
        auto* n = impl_->Find(iHandle);
        if (!n) {
            return {};
        }
        auto ext
            = (impl_->backend && n->nativeToken) ? impl_->backend->GetFramebufferSize(n->nativeToken) : rhi::Extent2D{};
        return WindowInfo{
            .handle = n->handle,
            .extent = ext,
            .nativeWindow = n->nativeWindow,
            .flags = n->flags,
            .alive = n->alive,
            .minimized = n->minimized,
        };
    }

    auto WindowManager::GetNativeHandle(WindowHandle iHandle) const -> rhi::NativeWindowHandle {
        if (!impl_) {
            return rhi::Win32Window{};
        }
        auto* n = impl_->Find(iHandle);
        return n ? n->nativeWindow : rhi::NativeWindowHandle{rhi::Win32Window{}};
    }

    auto WindowManager::GetNativeToken(WindowHandle iHandle) const -> void* {
        if (!impl_) {
            return nullptr;
        }
        auto* n = impl_->Find(iHandle);
        return n ? n->nativeToken : nullptr;
    }

    // ===========================================================================
    // Enumeration
    // ===========================================================================

    auto WindowManager::GetWindowCount() const noexcept -> uint32_t {
        return impl_ ? impl_->nodes.Size() : 0;
    }

    auto WindowManager::GetAllWindows() const -> std::vector<WindowHandle> {
        std::vector<WindowHandle> result;
        if (!impl_) {
            return result;
        }
        auto collect = [&](core::SlotHandle, const WindowNode& node) {
            if (node.alive) {
                result.push_back(node.handle);
            }
        };
        impl_->nodes.ForEach(collect);
        return result;
    }

    auto WindowManager::GetRootWindows() const -> std::vector<WindowHandle> {
        std::vector<WindowHandle> result;
        if (!impl_) {
            return result;
        }
        auto collect = [&](core::SlotHandle, const WindowNode& node) {
            if (node.alive && !node.parent.IsValid()) {
                result.push_back(node.handle);
            }
        };
        impl_->nodes.ForEach(collect);
        return result;
    }

    auto WindowManager::GetActiveWindows() -> std::vector<WindowHandle> {
        std::vector<WindowHandle> result;
        if (!impl_) {
            return result;
        }
        auto collect = [&](core::SlotHandle, WindowNode& node) {
            if (!node.alive) {
                return;
            }
            if (impl_->backend && node.nativeToken) {
                node.minimized = impl_->backend->IsMinimized(node.nativeToken);
            }
            if (!node.minimized) {
                result.push_back(node.handle);
            }
        };
        impl_->nodes.ForEach(collect);
        return result;
    }

    // ===========================================================================
    // Event polling
    // ===========================================================================

    auto WindowManager::PollEvents() -> std::span<const WindowEvent> {
        if (!impl_ || !impl_->backend) {
            return {};
        }

        impl_->eventBuffer.clear();
        impl_->backend->PollEvents(impl_->eventBuffer);

        // Filter out events for stale handles; update minimized state.
        // CloseRequested is forwarded as-is — caller decides policy.
        auto updater = [&](core::SlotHandle, WindowNode& node) {
            if (!node.alive || !node.nativeToken) {
                return;
            }
            node.minimized = impl_->backend->IsMinimized(node.nativeToken);
            node.extent = impl_->backend->GetFramebufferSize(node.nativeToken);
        };
        impl_->nodes.ForEach(updater);

        // Remove events targeting dead windows
        std::erase_if(impl_->eventBuffer, [&](const WindowEvent& e) { return !impl_->Find(e.window); });

        return impl_->eventBuffer;
    }

    auto WindowManager::ShouldClose() const -> bool {
        if (!impl_) {
            return true;
        }
        return impl_->nodes.Size() == 0;
    }

    auto WindowManager::SetHasSurfaceCallback(HasSurfaceFn iCallback) -> void {
        if (impl_) {
            impl_->hasSurfaceCallback = iCallback;
        }
    }

    auto WindowManager::GetBackend() noexcept -> IWindowBackend* {
        return impl_ ? impl_->backend.get() : nullptr;
    }

}  // namespace miki::platform
