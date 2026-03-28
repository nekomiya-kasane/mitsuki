/** @brief Cross-platform crash handler for emergency log dump.
 *
 * Provides async-signal-safe crash handling for POSIX signals and Windows SEH.
 * All operations in the crash path use only raw write() / WriteFile() — no heap
 * allocation, no std::visit, no fwrite.
 */
#pragma once

#include <cstdint>
#include <filesystem>

namespace miki::debug {

    /// @brief Crash context passed to dump callback.
    struct CrashContext {
        const char* signalName;  ///< "SIGSEGV", "SIGABRT", "ACCESS_VIOLATION", etc.
        int signalNumber;        ///< POSIX signal number or Windows exception code
        void* faultAddress;      ///< Address that caused the fault (if applicable)
        void* instructionPtr;    ///< Instruction pointer at crash
    };

    /// @brief Callback type for emergency dump. Must be async-signal-safe: no heap allocation, no exceptions, no locks.
    /// @param ctx Crash context with signal/exception info
    /// @param fd Raw file descriptor (int on POSIX, HANDLE cast to intptr_t on Windows)
    using CrashDumpCallback = void (*)(const CrashContext& ctx, intptr_t fd);

    /// @brief Install platform-specific crash handlers.
    /// Registers handlers for:
    ///   - POSIX: SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL
    ///   - Windows: SetUnhandledExceptionFilter
    /// @param emergencyPath Path to emergency dump file (opened eagerly)
    /// @param callback Async-signal-safe callback to dump application state
    /// @return true if handlers installed successfully
    auto InstallCrashHandlers(const std::filesystem::path& emergencyPath, CrashDumpCallback callback) -> bool;

    /// @brief Uninstall crash handlers and restore previous handlers.
    auto UninstallCrashHandlers() -> void;

    /// @brief Check if crash handlers are currently installed.
    [[nodiscard]] auto AreCrashHandlersInstalled() -> bool;

    /// @brief Get the emergency file descriptor (for use in callback).
    /// Returns -1 (POSIX) or 0 (Windows INVALID_HANDLE_VALUE) if not set.
    [[nodiscard]] auto GetEmergencyFd() -> intptr_t;

    /// @brief Async-signal-safe write to file descriptor. Wrapper around POSIX write() / Windows WriteFile().
    auto SafeWrite(intptr_t fd, const void* buf, size_t len) -> bool;

    /// @brief Async-signal-safe write string literal.
    template <size_t N>
    auto SafeWriteLiteral(intptr_t fd, const char (&str)[N]) -> bool {
        return SafeWrite(fd, str, N - 1);
    }

    /// @brief Async-signal-safe write hex dump of memory region.
    auto SafeWriteHex(intptr_t fd, const void* data, size_t len) -> void;

    /// @brief Async-signal-safe write uint64 as decimal string.
    auto SafeWriteUint64(intptr_t fd, uint64_t value) -> void;

    /// @brief Async-signal-safe write pointer as hex string (0x prefix).
    auto SafeWritePtr(intptr_t fd, const void* ptr) -> void;

}  // namespace miki::debug
