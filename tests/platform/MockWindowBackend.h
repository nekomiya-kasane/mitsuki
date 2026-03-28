// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include "miki/platform/WindowManager.h"
#include "miki/platform/EventSimulator.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace miki::test {

    /** @brief Headless mock backend for WindowManager unit tests.
     *  No OS windows, no GPU. Tracks create/destroy calls and simulates events.
     */
    class MockWindowBackend : public miki::platform::IWindowBackend {
       public:
        struct WindowState {
            miki::platform::WindowHandle handle;
            void* parentToken = nullptr;
            uint32_t width = 0;
            uint32_t height = 0;
            int32_t posX = 0;
            int32_t posY = 0;
            miki::platform::WindowDesc desc;
            std::string title;
            bool closed = false;
            bool minimized = false;
            bool maximized = false;
            bool visible = true;
            bool focused = false;
        };

        // Injected events to be returned by PollEvents
        std::vector<miki::platform::WindowEvent> injectedEvents;
        // Track all create/destroy calls
        std::unordered_map<void*, WindowState> windows;
        uint32_t createCount = 0;
        uint32_t destroyCount = 0;

        [[nodiscard]] auto CreateNativeWindow(
            const miki::platform::WindowDesc& iDesc, void* iParentToken, void*& oNativeToken
        ) -> miki::core::Result<miki::rhi::NativeWindowHandle> override {
            ++createCount;
            // Use a monotonic fake pointer as token
            auto* token = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000 + createCount));
            oNativeToken = token;
            bool hidden = iDesc.flags.Has(miki::platform::WindowFlags::Hidden);
            windows[token] = WindowState{
                .parentToken = iParentToken,
                .width = iDesc.width,
                .height = iDesc.height,
                .desc = iDesc,
                .visible = !hidden,
            };
            return miki::rhi::NativeWindowHandle{miki::rhi::Win32Window{.hwnd = token}};
        }

        auto DestroyNativeWindow(void* iNativeToken) -> void override {
            ++destroyCount;
            windows.erase(iNativeToken);
        }

        auto PollEvents(std::vector<miki::platform::WindowEvent>& ioEvents) -> void override {
            ioEvents.insert(ioEvents.end(), injectedEvents.begin(), injectedEvents.end());
            injectedEvents.clear();
        }

        [[nodiscard]] auto ShouldClose(void* iNativeToken) -> bool override {
            auto it = windows.find(iNativeToken);
            return it == windows.end() || it->second.closed;
        }

        [[nodiscard]] auto GetFramebufferSize(void* iNativeToken) -> miki::rhi::Extent2D override {
            auto it = windows.find(iNativeToken);
            if (it == windows.end()) {
                return {};
            }
            if (it->second.minimized) {
                return {0, 0};
            }
            return {it->second.width, it->second.height};
        }

        [[nodiscard]] auto IsMinimized(void* iNativeToken) -> bool override {
            auto it = windows.find(iNativeToken);
            return it == windows.end() || it->second.minimized;
        }

        auto ShowWindow(void* iNativeToken) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.visible = true;
            }
        }

        auto HideWindow(void* iNativeToken) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.visible = false;
            }
        }

        // Window state operations
        auto ResizeWindow(void* iNativeToken, uint32_t iWidth, uint32_t iHeight) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.width = iWidth;
                it->second.height = iHeight;
            }
        }

        auto SetWindowPosition(void* iNativeToken, int32_t iX, int32_t iY) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.posX = iX;
                it->second.posY = iY;
            }
        }

        [[nodiscard]] auto GetWindowPosition(void* iNativeToken) const -> std::pair<int32_t, int32_t> override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                return {it->second.posX, it->second.posY};
            }
            return {0, 0};
        }

        auto MinimizeWindow(void* iNativeToken) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.minimized = true;
            }
        }

        auto MaximizeWindow(void* iNativeToken) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.maximized = true;
                it->second.minimized = false;
            }
        }

        auto RestoreWindow(void* iNativeToken) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.minimized = false;
                it->second.maximized = false;
            }
        }

        auto FocusWindow(void* iNativeToken) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.focused = true;
            }
        }

        auto SetWindowTitle(void* iNativeToken, std::string_view iTitle) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.title = std::string(iTitle);
            }
        }

        auto SetWindowHandle(void* iNativeToken, miki::platform::WindowHandle iHandle) -> void override {
            auto it = windows.find(iNativeToken);
            if (it != windows.end()) {
                it->second.handle = iHandle;
            }
        }

        // Test helpers
        void SimulateMinimize(void* token) {
            auto it = windows.find(token);
            if (it != windows.end()) {
                it->second.minimized = true;
            }
        }

        void SimulateRestore(void* token) {
            auto it = windows.find(token);
            if (it != windows.end()) {
                it->second.minimized = false;
            }
        }

        void SimulateClose(void* token) {
            auto it = windows.find(token);
            if (it != windows.end()) {
                it->second.closed = true;
            }
        }

        /** @brief Get mutable reference to injected events for EventSimulator. */
        [[nodiscard]] auto GetPendingEvents() noexcept -> std::vector<miki::platform::WindowEvent>& {
            return injectedEvents;
        }
    };

    /** @brief Mock EventSimulator for headless unit tests.
     *  Injects events directly into MockWindowBackend's event queue.
     */
    class MockEventSimulator : public miki::platform::IEventSimulator {
       public:
        explicit MockEventSimulator(MockWindowBackend* iBackend) : backend_(iBackend) {}

        auto InjectKeyDown(
            miki::platform::WindowHandle iWindow, neko::platform::Key iKey, neko::platform::Modifiers iMods
        ) -> void override {
            if (backend_) {
                backend_->injectedEvents.push_back(
                    {iWindow, neko::platform::KeyDown{.key = iKey, .scancode = 0, .mods = iMods}}
                );
            }
        }

        auto InjectKeyUp(
            miki::platform::WindowHandle iWindow, neko::platform::Key iKey, neko::platform::Modifiers iMods
        ) -> void override {
            if (backend_) {
                backend_->injectedEvents.push_back(
                    {iWindow, neko::platform::KeyUp{.key = iKey, .scancode = 0, .mods = iMods}}
                );
            }
        }

        auto InjectTextInput(miki::platform::WindowHandle iWindow, char32_t iCodepoint) -> void override {
            if (backend_) {
                backend_->injectedEvents.push_back({iWindow, neko::platform::TextInput{.codepoint = iCodepoint}});
            }
        }

        auto InjectMouseMove(miki::platform::WindowHandle iWindow, double iX, double iY) -> void override {
            if (backend_) {
                double dx = iX - lastMouseX_;
                double dy = iY - lastMouseY_;
                backend_->injectedEvents.push_back(
                    {iWindow, neko::platform::MouseMove{.x = iX, .y = iY, .dx = dx, .dy = dy}}
                );
                lastMouseX_ = iX;
                lastMouseY_ = iY;
            }
        }

        auto InjectMouseDown(
            miki::platform::WindowHandle iWindow, neko::platform::MouseBtn iButton, double iX, double iY,
            neko::platform::Modifiers iMods
        ) -> void override {
            if (backend_) {
                InjectMouseMove(iWindow, iX, iY);
                backend_->injectedEvents.push_back(
                    {iWindow, neko::platform::MouseButton{
                                  .button = iButton, .action = neko::platform::Action::Press, .mods = iMods
                              }}
                );
            }
        }

        auto InjectMouseUp(
            miki::platform::WindowHandle iWindow, neko::platform::MouseBtn iButton, double iX, double iY,
            neko::platform::Modifiers iMods
        ) -> void override {
            if (backend_) {
                InjectMouseMove(iWindow, iX, iY);
                backend_->injectedEvents.push_back(
                    {iWindow, neko::platform::MouseButton{
                                  .button = iButton, .action = neko::platform::Action::Release, .mods = iMods
                              }}
                );
            }
        }

        auto InjectScroll(miki::platform::WindowHandle iWindow, double iDeltaX, double iDeltaY) -> void override {
            if (backend_) {
                backend_->injectedEvents.push_back({iWindow, neko::platform::Scroll{.dx = iDeltaX, .dy = iDeltaY}});
            }
        }

        auto InjectFocus(miki::platform::WindowHandle iWindow, bool iFocused) -> void override {
            if (backend_) {
                backend_->injectedEvents.push_back({iWindow, neko::platform::Focus{.focused = iFocused}});
            }
        }

        auto InjectResize(miki::platform::WindowHandle iWindow, uint32_t iWidth, uint32_t iHeight) -> void override {
            if (backend_) {
                backend_->injectedEvents.push_back(
                    {iWindow, neko::platform::Resize{.width = iWidth, .height = iHeight}}
                );
            }
        }

        auto InjectCloseRequested(miki::platform::WindowHandle iWindow) -> void override {
            if (backend_) {
                backend_->injectedEvents.push_back({iWindow, neko::platform::CloseRequested{}});
            }
        }

        auto InjectEvent(miki::platform::WindowHandle iWindow, const neko::platform::Event& iEvent) -> void override {
            if (backend_) {
                backend_->injectedEvents.push_back({iWindow, iEvent});
            }
        }

       private:
        MockWindowBackend* backend_ = nullptr;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
    };

}  // namespace miki::test
