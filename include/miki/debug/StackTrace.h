/** @brief Cross-platform stack trace capture and pretty-print utility.
 *
 * Replaces <stacktrace> STL with a hand-rolled implementation:
 *   - Windows: CaptureStackBackTrace + DbgHelp (SymFromAddr, SymGetLineFromAddr64)
 *   - POSIX:   backtrace + dladdr + abi::__cxa_demangle
 *
 * Provides two output modes:
 *   - PrintColored(): rich tapioca-styled box-drawing output to stderr
 *   - ToString():     plain-text representation for file/JSON sinks
 *
 * Thread-safe: DbgHelp symbol resolution is guarded by an internal mutex.
 *
 * @see specs/00-infra.md
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace miki::debug {

    /** @brief One resolved frame from a captured stack trace. */
    struct StackFrame {
        void* address = nullptr;      ///< Raw instruction pointer
        std::string symbol;           ///< Demangled function name (or "<unknown>")
        std::string module;           ///< Module / shared library basename
        std::string sourceFile;       ///< Source file path (empty if unavailable)
        uint32_t sourceLine = 0;      ///< 1-based line number (0 = unknown)
        uint64_t offset = 0;          ///< Byte displacement from function start
    };

    /** @brief Captured and symbol-resolved stack trace. */
    class StackTrace {
    public:
        /** @brief Capture the current thread's call stack.
         *  @param skipFrames Number of innermost frames to skip (Capture itself is always skipped).
         *  @param maxFrames  Upper bound on captured frames.
         */
        static auto Capture(uint32_t skipFrames = 0, uint32_t maxFrames = 64) -> StackTrace;

        /** @brief Access the resolved frames (outermost → innermost after Capture). */
        [[nodiscard]] auto GetFrames() const -> const std::vector<StackFrame>&;

        [[nodiscard]] auto Size() const -> uint32_t;
        [[nodiscard]] auto Empty() const -> bool;

        /** @brief Plain-text multi-line representation (no ANSI escapes). */
        [[nodiscard]] auto ToString() const -> std::string;

        /** @brief Pretty-print to stderr with tapioca box-drawing and colors.
         *  @param title Optional header label (default: "Stack Trace").
         */
        void PrintColored(std::string_view title = "Stack Trace") const;

        /** @brief Compact single-line format suitable for MIKI_LOG (≤ ~200 chars). */
        [[nodiscard]] static auto FormatFrameCompact(const StackFrame& frame, uint32_t index) -> std::string;

    private:
        std::vector<StackFrame> frames_;
    };

}  // namespace miki::debug
