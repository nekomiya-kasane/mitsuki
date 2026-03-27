// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT

#include <miki/debug/CpuProfiler.h>
#include <miki/core/ErrorCode.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#    define NOMINMAX
#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>
#else
#    include <pthread.h>
#    include <time.h>
#endif

namespace miki::debug {

    namespace {

        // Thread-local depth counter for nested scopes
        thread_local uint32_t tl_depth = 0;

        // Default ring buffer capacity (64K events)
        constexpr uint32_t kDefaultCapacity = 65536;

    }  // namespace

    struct CpuProfiler::Impl {
        std::atomic<bool> enabled{false};
        std::mutex mutex;
        std::vector<CpuProfileEvent> events;
        uint32_t capacity = kDefaultCapacity;

        Impl() { events.reserve(capacity); }
    };

    CpuProfiler::CpuProfiler() : impl_(new Impl()) {}

    CpuProfiler::~CpuProfiler() {
        delete impl_;
    }

    auto CpuProfiler::Instance() -> CpuProfiler& {
        static CpuProfiler instance;
        return instance;
    }

    auto CpuProfiler::Enable(bool enabled) -> void {
        impl_->enabled.store(enabled, std::memory_order_release);
    }

    auto CpuProfiler::IsEnabled() const -> bool {
        return impl_->enabled.load(std::memory_order_acquire);
    }

    auto CpuProfiler::RecordEvent(
        std::string_view name, LogCategory cat, uint64_t startNs, uint64_t endNs, uint32_t threadId, uint32_t depth
    ) -> void {
        if (!impl_->enabled.load(std::memory_order_acquire)) {
            return;
        }

        std::lock_guard lock(impl_->mutex);
        if (impl_->events.size() >= impl_->capacity) {
            // Ring buffer: overwrite oldest
            impl_->events.erase(impl_->events.begin());
        }
        impl_->events.push_back(
            CpuProfileEvent{
                .name = name,
                .category = cat,
                .startNs = startNs,
                .endNs = endNs,
                .threadId = threadId,
                .depth = depth,
            }
        );
    }

    auto CpuProfiler::ExportTrace(const std::filesystem::path& outputPath, TraceFormat format) -> core::Result<void> {
        std::lock_guard lock(impl_->mutex);

        FILE* file = nullptr;
#ifdef _WIN32
        _wfopen_s(&file, outputPath.c_str(), L"w");
#else
        file = std::fopen(outputPath.c_str(), "w");
#endif
        if (!file) {
            return std::unexpected(core::ErrorCode::TraceExportFailed);
        }

        if (format == TraceFormat::ChromeJson) {
            // Chrome Tracing JSON format
            std::fputs("{\"traceEvents\":[\n", file);

            bool first = true;
            for (const auto& event : impl_->events) {
                if (!first) {
                    std::fputs(",\n", file);
                }
                first = false;

                // Duration event (ph: "X")
                std::fprintf(
                    file, R"({"name":"%.*s","cat":"%s","ph":"X","ts":%llu,"dur":%llu,"pid":1,"tid":%u})",
                    static_cast<int>(event.name.size()), event.name.data(), ToString(event.category).data(),
                    static_cast<unsigned long long>(event.startNs / 1000),  // Convert ns to us
                    static_cast<unsigned long long>((event.endNs - event.startNs) / 1000), event.threadId
                );
            }

            std::fputs("\n],\"displayTimeUnit\":\"ns\"}\n", file);
        } else {
            // Perfetto protobuf format - simplified text representation for now
            // Full protobuf implementation would require protobuf library
            std::fputs("# Perfetto trace (text format)\n", file);
            std::fputs("# Import into ui.perfetto.dev for visualization\n\n", file);

            for (const auto& event : impl_->events) {
                std::fprintf(
                    file, "TRACE_EVENT: name=%.*s cat=%s start=%llu end=%llu tid=%u depth=%u\n",
                    static_cast<int>(event.name.size()), event.name.data(), ToString(event.category).data(),
                    static_cast<unsigned long long>(event.startNs), static_cast<unsigned long long>(event.endNs),
                    event.threadId, event.depth
                );
            }
        }

        std::fclose(file);
        return {};
    }

    auto CpuProfiler::Clear() -> void {
        std::lock_guard lock(impl_->mutex);
        impl_->events.clear();
    }

    auto CpuProfiler::GetRecentEvents(uint32_t maxCount) const -> std::span<const CpuProfileEvent> {
        std::lock_guard lock(impl_->mutex);
        auto count = std::min(static_cast<size_t>(maxCount), impl_->events.size());
        if (count == 0) {
            return {};
        }
        return std::span<const CpuProfileEvent>(impl_->events.data() + impl_->events.size() - count, count);
    }

    auto CpuProfiler::GetEventCount() const -> uint32_t {
        std::lock_guard lock(impl_->mutex);
        return static_cast<uint32_t>(impl_->events.size());
    }

    auto CpuProfiler::GetCurrentDepth() -> uint32_t {
        return tl_depth;
    }

    auto CpuProfiler::PushDepth() -> uint32_t {
        return tl_depth++;
    }

    auto CpuProfiler::PopDepth() -> void {
        if (tl_depth > 0) {
            --tl_depth;
        }
    }

    auto CpuProfiler::GetTimestampNs() -> uint64_t {
#ifdef _WIN32
        static const auto frequency = []() {
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            return freq.QuadPart;
        }();

        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        // Convert to nanoseconds: counter * 1e9 / frequency
        return static_cast<uint64_t>(counter.QuadPart * 1000000000ULL / frequency);
#else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
#endif
    }

    auto CpuProfiler::GetCurrentThreadId() -> uint32_t {
#ifdef _WIN32
        return static_cast<uint32_t>(::GetCurrentThreadId());
#else
        return static_cast<uint32_t>(pthread_self());
#endif
    }

}  // namespace miki::debug
