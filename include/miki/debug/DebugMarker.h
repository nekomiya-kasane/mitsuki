// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace miki::debug {

    /// @brief Default debug region color (white).
    inline constexpr float kDefaultDebugColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    /// @brief Predefined debug colors for common pass types.
    namespace DebugColors {
        inline constexpr float kGeometry[4] = {0.2f, 0.6f, 1.0f, 1.0f};     // Blue - geometry passes
        inline constexpr float kLighting[4] = {1.0f, 0.8f, 0.2f, 1.0f};     // Yellow - lighting passes
        inline constexpr float kShadow[4] = {0.4f, 0.4f, 0.4f, 1.0f};       // Gray - shadow passes
        inline constexpr float kPostProcess[4] = {0.8f, 0.2f, 0.8f, 1.0f};  // Magenta - post-process
        inline constexpr float kCompute[4] = {0.2f, 0.8f, 0.2f, 1.0f};      // Green - compute passes
        inline constexpr float kTransfer[4] = {0.2f, 0.8f, 0.8f, 1.0f};     // Cyan - transfer/copy
        inline constexpr float kRayTracing[4] = {1.0f, 0.4f, 0.2f, 1.0f};   // Orange - ray tracing
        inline constexpr float kUI[4] = {1.0f, 1.0f, 1.0f, 1.0f};           // White - UI rendering
    }  // namespace DebugColors

    /// @brief Scoped debug region. Calls CmdBeginDebugLabel on construction, CmdEndDebugLabel on destruction. Works
    /// with any RHI CommandBuffer type (CRTP concrete or type-erased CommandListHandle).
    ///
    /// @tparam CmdBuf Command buffer type with CmdBeginDebugLabel/CmdEndDebugLabel methods.
    ///
    /// @example
    ///   {
    ///       ScopedDebugRegion region(cmd, "Shadow Pass", DebugColors::kShadow);
    ///       // ... shadow rendering commands ...
    ///   } // CmdEndDebugLabel called automatically
    template <typename CmdBuf>
    class ScopedDebugRegion {
       public:
        /// @brief Begin a debug region with name and optional color.
        /// @param cmd Command buffer reference.
        /// @param name Debug region name (visible in RenderDoc/PIX/NSight).
        /// @param color RGBA color for the region (default: white).
        ScopedDebugRegion(CmdBuf& cmd, const char* name, const float color[4] = nullptr) : cmd_(cmd) {
            cmd_.CmdBeginDebugLabel(name, color ? color : kDefaultDebugColor);
        }

        ~ScopedDebugRegion() { cmd_.CmdEndDebugLabel(); }

        ScopedDebugRegion(const ScopedDebugRegion&) = delete;
        ScopedDebugRegion& operator=(const ScopedDebugRegion&) = delete;
        ScopedDebugRegion(ScopedDebugRegion&&) = delete;
        ScopedDebugRegion& operator=(ScopedDebugRegion&&) = delete;

       private:
        CmdBuf& cmd_;
    };

    /// @brief Insert a point marker (non-scoped) into the command buffer.
    /// @tparam CmdBuf Command buffer type with CmdInsertDebugLabel method.
    /// @param cmd Command buffer reference.
    /// @param name Marker name.
    /// @param color RGBA color for the marker.
    template <typename CmdBuf>
    inline void InsertDebugMarker(CmdBuf& cmd, const char* name, const float color[4] = nullptr) {
        cmd.CmdInsertDebugLabel(name, color ? color : kDefaultDebugColor);
    }

}  // namespace miki::debug

// Macro helpers
#ifndef MIKI_CONCAT_IMPL
#    define MIKI_CONCAT_IMPL(a, b) a##b
#endif
#ifndef MIKI_CONCAT
#    define MIKI_CONCAT(a, b) MIKI_CONCAT_IMPL(a, b)
#endif

/// @brief Create a scoped GPU debug region with the given name.
/// @param cmd Command buffer variable.
/// @param name Debug region name string literal.
#define MIKI_GPU_DEBUG_REGION(cmd, name)                                        \
    ::miki::debug::ScopedDebugRegion MIKI_CONCAT(_miki_gpu_region_, __LINE__) { \
        cmd, name                                                               \
    }

/// @brief Create a scoped GPU debug region with name and color.
/// @param cmd Command buffer variable.
/// @param name Debug region name string literal.
/// @param color Float[4] RGBA color array.
#define MIKI_GPU_DEBUG_REGION_COLOR(cmd, name, color)                           \
    ::miki::debug::ScopedDebugRegion MIKI_CONCAT(_miki_gpu_region_, __LINE__) { \
        cmd, name, color                                                        \
    }

/// @brief Insert a point marker into the command buffer.
/// @param cmd Command buffer variable.
/// @param name Marker name string literal.
#define MIKI_GPU_DEBUG_MARKER(cmd, name) ::miki::debug::InsertDebugMarker(cmd, name)
