/** @brief Structured logging system for the miki renderer.
 *
 * Architecture:
 *   Producer threads -> thread-local SPSC ring buffer (64KB)
 *   Background drain thread -> merges rings -> dispatches to sinks via std::visit (no virtual calls).
 *
 * Hot path (Log()) touches only thread-local memory. No mutex, no atomic CAS, no virtual call, no heap allocation.
 *
 * @see specs/00-infra.md section 3.1 for full design rationale.
 */
#pragma once

#include "miki/debug/LogCategory.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

/// @brief Compile-time minimum log level. Messages below this level are entirely eliminated by the compiler.
/// Default: Info in Release, Trace in Debug.
#ifndef MIKI_MIN_LOG_LEVEL
#    ifdef NDEBUG
#        define MIKI_MIN_LOG_LEVEL 2  // Info
#    else
#        define MIKI_MIN_LOG_LEVEL 0  // Trace
#    endif
#endif

namespace miki::debug {

    // ---- Log entry (value type, used in sink dispatch) ----

    struct LogEntry {
        LogLevel level;
        LogCategory category;
        std::string_view message;
        std::string_view file;
        uint32_t line;
        uint64_t timestampNs;
        uint32_t threadId;
    };

    // ---- Backpressure policy ----

    enum class BackpressurePolicy : uint8_t {
        Drop,   // Oldest entries silently overwritten (default)
        Block,  // Producer spins until drain frees space
    };

    // ---- Sink types (value types, no virtual dispatch) ----

    /// @brief Console sink. Writes ANSI-colored output to stderr.
    /// Uses native Win32 Console API / ANSI escapes. When tapioca is available, will delegate to
    /// tapioca::basic_console.
    class ConsoleSink {
       public:
        ConsoleSink();

        auto Write(const LogEntry& entry) -> void;
        auto Flush() -> void;

       private:
        bool colorEnabled_ = false;
    };

    /// @brief File sink. Writes plain text (no ANSI) to a file.
    class FileSink {
       public:
        explicit FileSink(std::filesystem::path path, bool dailyRotation = false);
        ~FileSink();

        FileSink(FileSink&& other) noexcept;
        auto operator=(FileSink&& other) noexcept -> FileSink&;

        FileSink(const FileSink&) = delete;
        auto operator=(const FileSink&) -> FileSink& = delete;

        auto Write(const LogEntry& entry) -> void;
        auto Flush() -> void;

        [[nodiscard]] auto GetPath() const -> const std::filesystem::path&;

       private:
        FILE* file_ = nullptr;
        std::filesystem::path basePath_;
        bool rotate_;
    };

    /// @brief JSON sink. One JSON object per line (NDJSON).
    class JsonSink {
       public:
        explicit JsonSink(std::filesystem::path path);
        ~JsonSink();

        JsonSink(JsonSink&& other) noexcept;
        auto operator=(JsonSink&& other) noexcept -> JsonSink&;

        JsonSink(const JsonSink&) = delete;
        auto operator=(const JsonSink&) -> JsonSink& = delete;

        auto Write(const LogEntry& entry) -> void;
        auto Flush() -> void;

       private:
        FILE* file_ = nullptr;
        std::filesystem::path path_;
    };

    /// @brief Callback sink. Bridges to user-defined handler. The ONLY path where a user-controlled indirect call
    /// occurs.
    class CallbackSink {
       public:
        using Callback = std::function<void(const LogEntry&)>;

        explicit CallbackSink(Callback cb);

        auto Write(const LogEntry& entry) -> void;
        auto Flush() -> void;

       private:
        Callback cb_;
    };

    /// @brief Type-erased sink via std::variant. No virtual dispatch.
    using LogSink = std::variant<ConsoleSink, FileSink, JsonSink, CallbackSink>;

    // ---- SPSC Ring Buffer ----

    /// @brief Lock-free single-producer single-consumer ring buffer. One instance per producer thread. 64KB,
    /// power-of-2, cache-line aligned. Stores serialized log entry payloads.
    class SpscRingBuffer {
       public:
        static constexpr uint32_t kCapacity = 64 * 1024;

        SpscRingBuffer();

        /// @brief Try to write data into the ring.
        /// @return true if written, false if full (Drop policy).
        [[nodiscard]] auto TryWrite(const void* data, uint32_t size) -> bool;

        /// @brief Read all available data, invoking callback per entry. Called only by the drain thread.
        using ReadCallback = void (*)(const void* data, uint32_t size, void* userCtx);
        auto ReadAll(ReadCallback cb, void* userCtx) -> uint32_t;

        /// @brief Discard all pending data (reset read pos to write pos).
        auto Reset() -> void;

        /// @brief Check if the ring has pending data.
        [[nodiscard]] auto HasData() const -> bool;

        /// @brief Raw access for emergency flush (crash handler).
        [[nodiscard]] auto RawData() const -> std::span<const std::byte>;
        [[nodiscard]] auto WritePos() const -> uint32_t;
        [[nodiscard]] auto ReadPos() const -> uint32_t;

       private:
        alignas(64) std::array<std::byte, kCapacity> buffer_;
        alignas(64) std::atomic<uint32_t> writePos_{0};
        alignas(64) std::atomic<uint32_t> readPos_{0};
    };

    // ---- Core Logger ----

    /// @brief Structured logger. One global instance. Zero-contention hot path via thread-local SPSC rings.
    class StructuredLogger {
       public:
        static auto Instance() -> StructuredLogger&;

        /// @brief Add a sink. Call at startup before logging begins.
        auto AddSink(LogSink sink) -> void;

        /// @brief Remove all sinks.
        auto ClearSinks() -> void;

        /// @brief Runtime per-category level filter.
        auto SetCategoryLevel(LogCategory cat, LogLevel level) -> void;

        /// @brief Get current level for a category.
        [[nodiscard]] auto GetCategoryLevel(LogCategory cat) const -> LogLevel;

        /// @brief Set backpressure policy for all ring buffers.
        auto SetBackpressurePolicy(BackpressurePolicy policy) -> void;

        /// @brief Core log function. Formats message, writes to thread-local ring. Hot path: format_to -> memcpy ->
        /// notify.
        template <typename... Args>
        auto Log(
            LogLevel level, LogCategory cat, std::string_view file, uint32_t line, std::format_string<Args...> fmt,
            Args&&... args
        ) -> void {
            auto levelInt = static_cast<uint8_t>(level);
            auto catLevel = static_cast<uint8_t>(GetCategoryLevel(cat));
            if (levelInt < catLevel) {
                return;
            }

            auto now = std::chrono::steady_clock::now();
            auto ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()
            );

            char buf[512];
            auto result = std::format_to_n(buf, sizeof(buf) - 1, fmt, std::forward<Args>(args)...);
            *result.out = '\0';
            auto msgLen = static_cast<uint32_t>(result.out - buf);

            WriteToRing(level, cat, file, line, ns, std::string_view{buf, msgLen});
        }

        /// @brief Flush all sinks. Blocks until drain processes all pending entries.
        auto Flush() -> void;

        /// @brief Shutdown: flush + join drain thread.
        auto Shutdown() -> void;

        /// @brief Start the background drain thread.
        auto StartDrainThread() -> void;

        /// @brief Check if logger is running (drain thread active).
        [[nodiscard]] auto IsRunning() const -> bool;

        /// @brief Get dropped message count (since last reset).
        [[nodiscard]] auto GetDroppedCount() const -> uint64_t;

        /// @brief Reset dropped message counter.
        auto ResetDroppedCount() -> void;

        /// @brief Discard all pending data in registered ring buffers.
        /// Call only when drain thread is stopped.
        auto ResetRings() -> void;

        StructuredLogger();
        ~StructuredLogger();

        StructuredLogger(const StructuredLogger&) = delete;
        auto operator=(const StructuredLogger&) -> StructuredLogger& = delete;

       private:
        auto WriteToRing(
            LogLevel level, LogCategory cat, std::string_view file, uint32_t line, uint64_t timestampNs,
            std::string_view message
        ) -> void;

        auto DrainLoop() -> void;
        auto DrainOnce() -> uint32_t;

        auto DispatchToSinks(const LogEntry& entry) -> void;

        static auto GetThreadRing() -> SpscRingBuffer&;

        struct RegisteredRing {
            SpscRingBuffer* ring = nullptr;
            uint32_t threadId = 0;
        };

        std::mutex sinksMutex_;
        std::vector<LogSink> sinks_;

        std::mutex ringsMutex_;
        std::vector<RegisteredRing> rings_;

        alignas(64) std::array<std::atomic<uint8_t>, static_cast<size_t>(LogCategory::Count_)> categoryLevels_;

        std::atomic<BackpressurePolicy> policy_{BackpressurePolicy::Drop};
        std::atomic<uint64_t> droppedCount_{0};

        std::thread drainThread_;
        std::atomic<bool> running_{false};
        std::atomic<bool> flushRequested_{false};
        std::mutex drainMutex_;
        std::condition_variable drainCv_;
        std::mutex flushMutex_;
        std::condition_variable flushCv_;
        std::atomic<bool> flushDone_{false};
    };

}  // namespace miki::debug

// ---- Macros ----

// Two-level concat for __LINE__ expansion
#define MIKI_CONCAT_IMPL_(a, b) a##b
#define MIKI_CONCAT_(a, b) MIKI_CONCAT_IMPL_(a, b)

#define MIKI_LOG(level, cat, ...)                                                                                 \
    do {                                                                                                          \
        constexpr auto lvl_ = static_cast<uint8_t>(level);                                                        \
        if constexpr (lvl_ >= MIKI_MIN_LOG_LEVEL) {                                                               \
            auto& logger_ = ::miki::debug::StructuredLogger::Instance();                                          \
            if (logger_.GetCategoryLevel(cat) <= level) logger_.Log(level, cat, __FILE__, __LINE__, __VA_ARGS__); \
        }                                                                                                         \
    } while (0)

#define MIKI_LOG_TRACE(cat, ...) MIKI_LOG(::miki::debug::LogLevel::Trace, cat, __VA_ARGS__)
#define MIKI_LOG_DEBUG(cat, ...) MIKI_LOG(::miki::debug::LogLevel::Debug, cat, __VA_ARGS__)
#define MIKI_LOG_INFO(cat, ...) MIKI_LOG(::miki::debug::LogLevel::Info, cat, __VA_ARGS__)
#define MIKI_LOG_WARN(cat, ...) MIKI_LOG(::miki::debug::LogLevel::Warn, cat, __VA_ARGS__)
#define MIKI_LOG_ERROR(cat, ...) MIKI_LOG(::miki::debug::LogLevel::Error, cat, __VA_ARGS__)
#define MIKI_LOG_FATAL(cat, ...) MIKI_LOG(::miki::debug::LogLevel::Fatal, cat, __VA_ARGS__)

#define MIKI_LOG_FLUSH() ::miki::debug::StructuredLogger::Instance().Flush()
