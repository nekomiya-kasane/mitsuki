/** @brief WindowManager Demo — Showcases multi-window, events, parent-child hierarchy.
 *
 * Features demonstrated:
 * - Creating/destroying windows dynamically
 * - Parent-child window relationships (window tree)
 * - Event polling and dispatch (keyboard, mouse, resize, focus, close)
 * - Window show/hide/minimize
 * - Framebuffer size queries
 * - Graceful shutdown with cascade destruction
 */

#include "miki/platform/glfw/GlfwWindowBackend.h"
#include "miki/platform/WindowManager.h"
#include "miki/debug/StructuredLogger.h"

#include <vector>

#ifdef __EMSCRIPTEN__
#    include <emscripten/emscripten.h>
#endif

namespace {

    using namespace miki::debug;
    constexpr auto kCat = LogCategory::Demo;

    struct WindowNode {
        miki::platform::WindowHandle handle;
        std::string name;
        std::vector<WindowNode> children;
    };

    struct DemoState {
        miki::platform::WindowManager* wm = nullptr;
        WindowNode root;
        miki::platform::WindowHandle focusedWindow = {};
        bool running = true;
        int frameCount = 0;
        int windowCounter = 0;
        bool toolWindowVisible = true;
    };

    DemoState gState;

    auto FindNode(WindowNode& node, miki::platform::WindowHandle handle) -> WindowNode* {
        if (node.handle == handle) {
            return &node;
        }
        for (auto& child : node.children) {
            if (auto* found = FindNode(child, handle)) {
                return found;
            }
        }
        return nullptr;
    }

    auto RemoveNode(WindowNode& parent, miki::platform::WindowHandle handle) -> bool {
        for (auto it = parent.children.begin(); it != parent.children.end(); ++it) {
            if (it->handle == handle) {
                parent.children.erase(it);
                return true;
            }
            if (RemoveNode(*it, handle)) {
                return true;
            }
        }
        return false;
    }

    void PrintWindowTree(const WindowNode& node, int depth = 0) {
        std::string indent(depth * 2, ' ');
        auto info = gState.wm->GetWindowInfo(node.handle);
        MIKI_LOG_INFO(
            kCat, "{}[{}] {} ({}x{})", indent, node.handle.IsValid() ? node.handle.id : 0u, node.name,
            info.extent.width, info.extent.height
        );
        for (const auto& child : node.children) {
            PrintWindowTree(child, depth + 1);
        }
    }

    auto CreateChildWindow(miki::platform::WindowHandle parent, const std::string& name)
        -> miki::platform::WindowHandle {
        miki::platform::WindowDesc desc{
            .title = name,
            .width = static_cast<uint32_t>(300 + (gState.windowCounter % 3) * 50),
            .height = static_cast<uint32_t>(200 + (gState.windowCounter % 3) * 30),
            .parent = parent,
            .flags = miki::platform::WindowFlags::None,
#ifdef __EMSCRIPTEN__
            .canvasSelector = "#dynamic",
#endif
        };
        auto result = gState.wm->CreateWindow(desc);
        if (!result) {
            MIKI_LOG_ERROR(kCat, "Failed to create window: {}", name);
            return {};
        }
        gState.windowCounter++;
        MIKI_LOG_INFO(kCat, "Created window: {} (handle={})", name, result->id);
        return *result;
    }

    void LogEvent(const miki::platform::WindowEvent& evt) {
        std::visit(
            [&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                auto winId = evt.window.IsValid() ? evt.window.id : 0u;
                if constexpr (std::is_same_v<T, neko::platform::KeyDown>) {
                    MIKI_LOG_DEBUG(
                        kCat, "[Win{}] KeyDown: key={} mods={}", winId, static_cast<int>(e.key),
                        static_cast<int>(e.mods)
                    );
                } else if constexpr (std::is_same_v<T, neko::platform::KeyUp>) {
                    MIKI_LOG_DEBUG(kCat, "[Win{}] KeyUp: key={}", winId, static_cast<int>(e.key));
                } else if constexpr (std::is_same_v<T, neko::platform::MouseButton>) {
                    MIKI_LOG_DEBUG(
                        kCat, "[Win{}] MouseButton: btn={} action={}", winId, static_cast<int>(e.button),
                        static_cast<int>(e.action)
                    );
                } else if constexpr (std::is_same_v<T, neko::platform::Scroll>) {
                    MIKI_LOG_DEBUG(kCat, "[Win{}] Scroll: dx={:.2f} dy={:.2f}", winId, e.dx, e.dy);
                } else if constexpr (std::is_same_v<T, neko::platform::Resize>) {
                    MIKI_LOG_INFO(kCat, "[Win{}] Resize: {}x{}", winId, e.width, e.height);
                } else if constexpr (std::is_same_v<T, neko::platform::Focus>) {
                    MIKI_LOG_DEBUG(kCat, "[Win{}] Focus: {}", winId, e.focused ? "gained" : "lost");
                    if (e.focused) {
                        gState.focusedWindow = evt.window;
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::CloseRequested>) {
                    MIKI_LOG_INFO(kCat, "[Win{}] CloseRequested", winId);
                } else if constexpr (std::is_same_v<T, neko::platform::TextInput>) {
                    MIKI_LOG_DEBUG(kCat, "[Win{}] TextInput: U+{:04X}", winId, static_cast<unsigned>(e.codepoint));
                }
            },
            evt.event
        );
    }

    void PrintHelp() {
        MIKI_LOG_INFO(kCat, "=== Controls ===");
        MIKI_LOG_INFO(kCat, "  N     — Create new child window under focused window");
        MIKI_LOG_INFO(kCat, "  D     — Destroy focused window (and its children)");
        MIKI_LOG_INFO(kCat, "  H     — Hide focused window");
        MIKI_LOG_INFO(kCat, "  S     — Show focused window");
        MIKI_LOG_INFO(kCat, "  M     — Minimize focused window");
        MIKI_LOG_INFO(kCat, "  X     — Maximize focused window");
        MIKI_LOG_INFO(kCat, "  R     — Restore focused window");
        MIKI_LOG_INFO(kCat, "  F     — Focus/bring to front focused window");
        MIKI_LOG_INFO(kCat, "  +/-   — Resize focused window (+50/-50 pixels)");
        MIKI_LOG_INFO(kCat, "  Arrows— Move focused window (with Shift held)");
        MIKI_LOG_INFO(kCat, "  T     — Print window tree");
        MIKI_LOG_INFO(kCat, "  F1    — Print this help");
        MIKI_LOG_INFO(kCat, "  ESC   — Quit");
    }

    void MainLoop() {
        if (!gState.running) {
#ifdef __EMSCRIPTEN__
            emscripten_cancel_main_loop();
#endif
            return;
        }

        auto events = gState.wm->PollEvents();
        for (const auto& evt : events) {
            LogEvent(evt);

            if (std::holds_alternative<neko::platform::CloseRequested>(evt.event)) {
                if (evt.window == gState.root.handle) {
                    MIKI_LOG_INFO(kCat, "Main window close requested — shutting down");
                    gState.running = false;
                } else {
                    MIKI_LOG_INFO(kCat, "Window {} close requested — destroying", evt.window.id);
                    (void)gState.wm->DestroyWindow(evt.window);
                    RemoveNode(gState.root, evt.window);
                }
            }

            if (std::holds_alternative<neko::platform::KeyDown>(evt.event)) {
                auto& key = std::get<neko::platform::KeyDown>(evt.event);
                switch (key.key) {
                    case neko::platform::Key::N: {
                        auto parent = gState.focusedWindow.IsValid() ? gState.focusedWindow : gState.root.handle;
                        auto* parentNode = FindNode(gState.root, parent);
                        if (parentNode) {
                            auto name = std::format("Child-{}", gState.windowCounter);
                            auto handle = CreateChildWindow(parent, name);
                            if (handle.IsValid()) {
                                parentNode->children.push_back({handle, name, {}});
                            }
                        }
                        break;
                    }
                    case neko::platform::Key::D: {
                        if (gState.focusedWindow.IsValid() && gState.focusedWindow != gState.root.handle) {
                            MIKI_LOG_INFO(kCat, "Destroying window {}", gState.focusedWindow.id);
                            (void)gState.wm->DestroyWindow(gState.focusedWindow);
                            RemoveNode(gState.root, gState.focusedWindow);
                            gState.focusedWindow = gState.root.handle;
                        } else {
                            MIKI_LOG_WARN(kCat, "Cannot destroy main window with D key");
                        }
                        break;
                    }
                    case neko::platform::Key::H: {
                        if (gState.focusedWindow.IsValid()) {
                            MIKI_LOG_INFO(kCat, "Hiding window {}", gState.focusedWindow.id);
                            gState.wm->HideWindow(gState.focusedWindow);
                        }
                        break;
                    }
                    case neko::platform::Key::S: {
                        if (gState.focusedWindow.IsValid()) {
                            MIKI_LOG_INFO(kCat, "Showing window {}", gState.focusedWindow.id);
                            gState.wm->ShowWindow(gState.focusedWindow);
                        }
                        break;
                    }
                    case neko::platform::Key::M: {
                        if (gState.focusedWindow.IsValid()) {
                            MIKI_LOG_INFO(kCat, "Minimizing window {}", gState.focusedWindow.id);
                            gState.wm->MinimizeWindow(gState.focusedWindow);
                        }
                        break;
                    }
                    case neko::platform::Key::X: {
                        if (gState.focusedWindow.IsValid()) {
                            MIKI_LOG_INFO(kCat, "Maximizing window {}", gState.focusedWindow.id);
                            gState.wm->MaximizeWindow(gState.focusedWindow);
                        }
                        break;
                    }
                    case neko::platform::Key::R: {
                        if (gState.focusedWindow.IsValid()) {
                            MIKI_LOG_INFO(kCat, "Restoring window {}", gState.focusedWindow.id);
                            gState.wm->RestoreWindow(gState.focusedWindow);
                        }
                        break;
                    }
                    case neko::platform::Key::F: {
                        if (gState.focusedWindow.IsValid()) {
                            MIKI_LOG_INFO(kCat, "Focusing window {}", gState.focusedWindow.id);
                            gState.wm->FocusWindow(gState.focusedWindow);
                        }
                        break;
                    }
                    case neko::platform::Key::Equal: {  // + key (resize larger)
                        if (gState.focusedWindow.IsValid()) {
                            auto info = gState.wm->GetWindowInfo(gState.focusedWindow);
                            uint32_t newW = info.extent.width + 50;
                            uint32_t newH = info.extent.height + 50;
                            MIKI_LOG_INFO(kCat, "Resizing window {} to {}x{}", gState.focusedWindow.id, newW, newH);
                            gState.wm->ResizeWindow(gState.focusedWindow, newW, newH);
                        }
                        break;
                    }
                    case neko::platform::Key::Minus: {  // - key (resize smaller)
                        if (gState.focusedWindow.IsValid()) {
                            auto info = gState.wm->GetWindowInfo(gState.focusedWindow);
                            uint32_t newW = info.extent.width > 100 ? info.extent.width - 50 : 100;
                            uint32_t newH = info.extent.height > 100 ? info.extent.height - 50 : 100;
                            MIKI_LOG_INFO(kCat, "Resizing window {} to {}x{}", gState.focusedWindow.id, newW, newH);
                            gState.wm->ResizeWindow(gState.focusedWindow, newW, newH);
                        }
                        break;
                    }
                    case neko::platform::Key::Left: {
                        if (gState.focusedWindow.IsValid()
                            && (key.mods & neko::platform::Modifiers::Shift) != neko::platform::Modifiers::None) {
                            auto [x, y] = gState.wm->GetWindowPosition(gState.focusedWindow);
                            gState.wm->SetWindowPosition(gState.focusedWindow, x - 50, y);
                            MIKI_LOG_INFO(kCat, "Moved window {} to ({}, {})", gState.focusedWindow.id, x - 50, y);
                        }
                        break;
                    }
                    case neko::platform::Key::Right: {
                        if (gState.focusedWindow.IsValid()
                            && (key.mods & neko::platform::Modifiers::Shift) != neko::platform::Modifiers::None) {
                            auto [x, y] = gState.wm->GetWindowPosition(gState.focusedWindow);
                            gState.wm->SetWindowPosition(gState.focusedWindow, x + 50, y);
                            MIKI_LOG_INFO(kCat, "Moved window {} to ({}, {})", gState.focusedWindow.id, x + 50, y);
                        }
                        break;
                    }
                    case neko::platform::Key::Up: {
                        if (gState.focusedWindow.IsValid()
                            && (key.mods & neko::platform::Modifiers::Shift) != neko::platform::Modifiers::None) {
                            auto [x, y] = gState.wm->GetWindowPosition(gState.focusedWindow);
                            gState.wm->SetWindowPosition(gState.focusedWindow, x, y - 50);
                            MIKI_LOG_INFO(kCat, "Moved window {} to ({}, {})", gState.focusedWindow.id, x, y - 50);
                        }
                        break;
                    }
                    case neko::platform::Key::Down: {
                        if (gState.focusedWindow.IsValid()
                            && (key.mods & neko::platform::Modifiers::Shift) != neko::platform::Modifiers::None) {
                            auto [x, y] = gState.wm->GetWindowPosition(gState.focusedWindow);
                            gState.wm->SetWindowPosition(gState.focusedWindow, x, y + 50);
                            MIKI_LOG_INFO(kCat, "Moved window {} to ({}, {})", gState.focusedWindow.id, x, y + 50);
                        }
                        break;
                    }
                    case neko::platform::Key::T: {
                        MIKI_LOG_INFO(kCat, "=== Window Tree ===");
                        PrintWindowTree(gState.root);
                        break;
                    }
                    case neko::platform::Key::F1: {
                        PrintHelp();
                        break;
                    }
                    case neko::platform::Key::Escape: {
                        MIKI_LOG_INFO(kCat, "Escape pressed — shutting down");
                        gState.running = false;
                        break;
                    }
                    default: break;
                }
            }
        }

        if (gState.wm->ShouldClose()) {
            gState.running = false;
        }

        constexpr int kStatsInterval = 300;
        gState.frameCount++;
        if (gState.frameCount % kStatsInterval == 0) {
            auto info = gState.wm->GetWindowInfo(gState.root.handle);
            MIKI_LOG_TRACE(kCat, "Frame {} — Main: {}x{}", gState.frameCount, info.extent.width, info.extent.height);
        }
    }

}  // namespace

int main() {
    auto& logger = StructuredLogger::Instance();
    logger.AddSink(ConsoleSink{});
    logger.StartDrainThread();

    MIKI_LOG_INFO(kCat, "=== miki WindowManager Demo ===");
    MIKI_LOG_INFO(kCat, "Features: create/destroy windows, hide/show/minimize, window tree");
    PrintHelp();

    auto backend = std::make_unique<miki::platform::GlfwWindowBackend>(miki::rhi::BackendType::WebGPU);
    auto wmResult = miki::platform::WindowManager::Create(std::move(backend));
    if (!wmResult) {
        MIKI_LOG_FATAL(kCat, "Failed to create WindowManager");
        return 1;
    }
    auto wm = std::move(*wmResult);
    gState.wm = &wm;

    miki::platform::WindowDesc mainDesc{
        .title = "miki Demo — Main Window (press F1 for help)",
        .width = 1280,
        .height = 720,
        .flags = miki::platform::WindowFlags::None,
#ifdef __EMSCRIPTEN__
        .canvasSelector = "#main",
#endif
    };
    auto mainResult = wm.CreateWindow(mainDesc);
    if (!mainResult) {
        MIKI_LOG_FATAL(kCat, "Failed to create main window");
        return 1;
    }
    gState.root = {*mainResult, "Main", {}};
    gState.focusedWindow = *mainResult;
    gState.windowCounter = 1;
    MIKI_LOG_INFO(kCat, "Created main window (handle={})", mainResult->id);

    auto child1 = CreateChildWindow(gState.root.handle, "Child-1");
    if (child1.IsValid()) {
        gState.root.children.push_back({child1, "Child-1", {}});
        auto grandchild = CreateChildWindow(child1, "Grandchild-1");
        if (grandchild.IsValid()) {
            gState.root.children[0].children.push_back({grandchild, "Grandchild-1", {}});
        }
    }

    auto child2 = CreateChildWindow(gState.root.handle, "Child-2");
    if (child2.IsValid()) {
        gState.root.children.push_back({child2, "Child-2", {}});
    }

    MIKI_LOG_INFO(kCat, "Initial window tree:");
    PrintWindowTree(gState.root);
    MIKI_LOG_INFO(kCat, "Starting main loop...");

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(MainLoop, 0, 1);
#else
    while (gState.running) {
        MainLoop();
    }
#endif

    MIKI_LOG_INFO(kCat, "Shutting down...");
    logger.Flush();
    logger.Shutdown();
    return 0;
}
