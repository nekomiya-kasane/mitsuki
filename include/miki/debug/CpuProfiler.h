// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <miki/core/Result.h>
#include <miki/debug/LogCategory.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace miki::debug {

    /// @brief CPU profile event data.
    struct CpuProfileEvent {
        std::string_view name;  // MUST have static storage duration (string literal or __func__)
        LogCategory category;
        uint64_t startNs;
        uint64_t endNs;
        uint32_t threadId;
        uint32_t depth;  // Nesting level (computed from thread-local stack)
    };

    /// @brief Trace export format.
    enum class TraceFormat : uint8_t {
        Perfetto,    // Perfetto protobuf (primary, compact, native Perfetto UI support)
        ChromeJson,  // Chrome Tracing JSON (legacy, chrome://tracing compatible)
    };

    /// @brief Global CPU profiler. Collects scoped events, exports trace.
    ///
    /// Architecture:
    ///   - Thread-local event ring buffers (lock-free write)
    ///   - Global merge on export
    ///   - Zero overhead when disabled
    ///
    /// Performance target: <20ns overhead per scope enter/exit (rdtsc + ring write).
    class CpuProfiler {
       public:
        static auto Instance() -> CpuProfiler&;

        auto Enable(bool enabled) -> void;
        [[nodiscard]] auto IsEnabled() const -> bool;

        /// @brief Record a completed event (called by CpuProfileScope destructor).
        auto RecordEvent(
            std::string_view name, LogCategory cat, uint64_t startNs, uint64_t endNs, uint32_t threadId, uint32_t depth
        ) -> void;

        /// @brief Export trace to file via streaming write (no in-memory string assembly).
        /// @param outputPath Output file path (.perfetto-trace or .json).
        /// @param format Export format.
        /// @return Ok on success, TraceExportFailed on error.
        auto ExportTrace(const std::filesystem::path& outputPath, TraceFormat format = TraceFormat::ChromeJson)
            -> core::Result<void>;

        /// @brief Clear all recorded events.
        auto Clear() -> void;

        /// @brief Get last N events for ImGui display.
        [[nodiscard]] auto GetRecentEvents(uint32_t maxCount) const -> std::span<const CpuProfileEvent>;

        /// @brief Get total recorded event count.
        [[nodiscard]] auto GetEventCount() const -> uint32_t;

        /// @brief Get current thread's nesting depth.
        [[nodiscard]] static auto GetCurrentDepth() -> uint32_t;

        /// @brief Push depth for current thread.
        static auto PushDepth() -> uint32_t;

        /// @brief Pop depth for current thread.
        static auto PopDepth() -> void;

        /// @brief Get current timestamp in nanoseconds (high-resolution).
        [[nodiscard]] static auto GetTimestampNs() -> uint64_t;

        /// @brief Get current thread ID.
        [[nodiscard]] static auto GetCurrentThreadId() -> uint32_t;

       private:
        CpuProfiler();
        ~CpuProfiler();
        CpuProfiler(const CpuProfiler&) = delete;
        CpuProfiler& operator=(const CpuProfiler&) = delete;

        struct Impl;
        Impl* impl_;
    };

    /// @brief Scoped CPU timer. Writes begin/end events to thread-local ring.
    /// RAII: records event on destruction.
    class CpuProfileScope {
       public:
        CpuProfileScope(std::string_view name, LogCategory cat = LogCategory::Core)
            : name_(name), cat_(cat), depth_(0), startNs_(0) {
            if (CpuProfiler::Instance().IsEnabled()) {
                depth_ = CpuProfiler::PushDepth();
                startNs_ = CpuProfiler::GetTimestampNs();
            }
        }

        ~CpuProfileScope() {
            if (startNs_ != 0) {
                auto endNs = CpuProfiler::GetTimestampNs();
                CpuProfiler::PopDepth();
                CpuProfiler::Instance().RecordEvent(
                    name_, cat_, startNs_, endNs, CpuProfiler::GetCurrentThreadId(), depth_
                );
            }
        }

        CpuProfileScope(const CpuProfileScope&) = delete;
        CpuProfileScope& operator=(const CpuProfileScope&) = delete;
        CpuProfileScope(CpuProfileScope&&) = delete;
        CpuProfileScope& operator=(CpuProfileScope&&) = delete;

       private:
        std::string_view name_;
        LogCategory cat_;
        uint32_t depth_;
        uint64_t startNs_;
    };

}  // namespace miki::debug

// Macro helpers for unique variable names
#define MIKI_CONCAT_IMPL(a, b) a##b
#define MIKI_CONCAT(a, b) MIKI_CONCAT_IMPL(a, b)

/// @brief Create a scoped CPU profile region with the given name.
#define MIKI_CPU_PROFILE_SCOPE(name)                                         \
    ::miki::debug::CpuProfileScope MIKI_CONCAT(_miki_cpu_scope_, __LINE__) { \
        name                                                                 \
    }

/// @brief Create a scoped CPU profile region using the current function name.
#define MIKI_CPU_PROFILE_FUNCTION() MIKI_CPU_PROFILE_SCOPE(__func__)

/// @brief Create a scoped CPU profile region with name and category.
#define MIKI_CPU_PROFILE_SCOPE_CAT(name, cat)                                \
    ::miki::debug::CpuProfileScope MIKI_CONCAT(_miki_cpu_scope_, __LINE__) { \
        name, cat                                                            \
    }
