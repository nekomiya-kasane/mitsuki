// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <variant>

namespace miki::rhi {

    struct Extent2D {
        uint32_t width = 0;
        uint32_t height = 0;
        constexpr auto operator==(const Extent2D&) const noexcept -> bool = default;
    };

    struct Win32Window {
        void* hwnd = nullptr;
    };

    struct XlibWindow {
        void* display = nullptr;
        unsigned long window = 0;
    };

    struct WaylandWindow {
        void* display = nullptr;
        void* surface = nullptr;
    };

    struct CocoaWindow {
        void* nsWindow = nullptr;
    };

    struct AndroidWindow {
        void* aNativeWindow = nullptr;
    };

    struct WebWindow {
        const char* canvasSelector = nullptr;
    };

    using NativeWindowHandle
        = std::variant<Win32Window, XlibWindow, WaylandWindow, CocoaWindow, AndroidWindow, WebWindow>;

    static_assert(std::is_trivially_destructible_v<NativeWindowHandle>);
    static_assert(std::is_trivially_copyable_v<Win32Window>);
    static_assert(std::is_trivially_copyable_v<XlibWindow>);
    static_assert(std::is_trivially_copyable_v<WaylandWindow>);

}  // namespace miki::rhi
