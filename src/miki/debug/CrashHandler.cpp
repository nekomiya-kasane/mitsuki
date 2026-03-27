/** @brief Cross-platform crash handler implementation.
 *
 * Windows: SetUnhandledExceptionFilter
 * POSIX: sigaction for SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL
 *
 * All crash path operations are async-signal-safe.
 */

#include "miki/debug/CrashHandler.h"

#include <atomic>
#include <cstring>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <signal.h>
#    include <unistd.h>
#endif

namespace miki::debug {

    namespace {

        // Global state (accessible from signal handler)
        std::atomic<bool> g_installed{false};
        CrashDumpCallback g_callback{nullptr};

#ifdef _WIN32
        HANDLE g_emergencyHandle{INVALID_HANDLE_VALUE};
        LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter{nullptr};
#else
        int g_emergencyFd{-1};
        struct sigaction g_previousSigsegv{};
        struct sigaction g_previousSigabrt{};
        struct sigaction g_previousSigfpe{};
        struct sigaction g_previousSigbus{};
        struct sigaction g_previousSigill{};
#endif

        // Hex digits for SafeWriteHex
        constexpr char kHexDigits[] = "0123456789abcdef";

#ifdef _WIN32

        auto ExceptionCodeToName(DWORD code) -> const char* {
            switch (code) {
                case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
                case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
                case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
                case EXCEPTION_FLT_DENORMAL_OPERAND: return "FLT_DENORMAL_OPERAND";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
                case EXCEPTION_FLT_INEXACT_RESULT: return "FLT_INEXACT_RESULT";
                case EXCEPTION_FLT_INVALID_OPERATION: return "FLT_INVALID_OPERATION";
                case EXCEPTION_FLT_OVERFLOW: return "FLT_OVERFLOW";
                case EXCEPTION_FLT_STACK_CHECK: return "FLT_STACK_CHECK";
                case EXCEPTION_FLT_UNDERFLOW: return "FLT_UNDERFLOW";
                case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
                case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
                case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
                case EXCEPTION_INT_OVERFLOW: return "INT_OVERFLOW";
                case EXCEPTION_INVALID_DISPOSITION: return "INVALID_DISPOSITION";
                case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
                case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
                case EXCEPTION_SINGLE_STEP: return "SINGLE_STEP";
                case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
                default: return "UNKNOWN_EXCEPTION";
            }
        }

        LONG WINAPI CrashHandlerWindows(EXCEPTION_POINTERS* exInfo) {
            if (!g_installed.load(std::memory_order_relaxed)) {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            auto fd = reinterpret_cast<intptr_t>(g_emergencyHandle);
            if (g_emergencyHandle == INVALID_HANDLE_VALUE) {
                if (g_previousFilter) {
                    return g_previousFilter(exInfo);
                }
                return EXCEPTION_CONTINUE_SEARCH;
            }

            // Build crash context
            CrashContext ctx{};
            ctx.signalNumber = static_cast<int>(exInfo->ExceptionRecord->ExceptionCode);
            ctx.signalName = ExceptionCodeToName(exInfo->ExceptionRecord->ExceptionCode);
            ctx.faultAddress = exInfo->ExceptionRecord->ExceptionAddress;
#ifdef _M_X64
            ctx.instructionPtr = reinterpret_cast<void*>(exInfo->ContextRecord->Rip);
#elif defined(_M_IX86)
            ctx.instructionPtr = reinterpret_cast<void*>(exInfo->ContextRecord->Eip);
#elif defined(_M_ARM64)
            ctx.instructionPtr = reinterpret_cast<void*>(exInfo->ContextRecord->Pc);
#else
            ctx.instructionPtr = nullptr;
#endif

            // Write crash header
            SafeWriteLiteral(fd, "\n=== MIKI CRASH DUMP ===\n");
            SafeWriteLiteral(fd, "Exception: ");
            SafeWrite(fd, ctx.signalName, std::strlen(ctx.signalName));
            SafeWriteLiteral(fd, " (0x");
            SafeWriteHex(fd, &ctx.signalNumber, sizeof(ctx.signalNumber));
            SafeWriteLiteral(fd, ")\n");
            SafeWriteLiteral(fd, "Fault address: ");
            SafeWritePtr(fd, ctx.faultAddress);
            SafeWriteLiteral(fd, "\nInstruction pointer: ");
            SafeWritePtr(fd, ctx.instructionPtr);
            SafeWriteLiteral(fd, "\n\n");

            // Call user callback
            if (g_callback) {
                g_callback(ctx, fd);
            }

            // Write footer
            SafeWriteLiteral(fd, "\n=== END CRASH DUMP ===\n");

            // Flush and close
            FlushFileBuffers(g_emergencyHandle);

            // Chain to previous handler
            if (g_previousFilter) {
                return g_previousFilter(exInfo);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }

#else  // POSIX

        auto SignalToName(int sig) -> const char* {
            switch (sig) {
                case SIGSEGV: return "SIGSEGV";
                case SIGABRT: return "SIGABRT";
                case SIGFPE: return "SIGFPE";
                case SIGBUS: return "SIGBUS";
                case SIGILL: return "SIGILL";
                default: return "UNKNOWN_SIGNAL";
            }
        }

        void CrashHandlerPosix(int sig, siginfo_t* info, void* ucontext) {
            if (!g_installed.load(std::memory_order_relaxed)) {
                return;
            }

            auto fd = static_cast<intptr_t>(g_emergencyFd);
            if (g_emergencyFd < 0) {
                // Re-raise with default handler
                signal(sig, SIG_DFL);
                raise(sig);
                return;
            }

            // Build crash context
            CrashContext ctx{};
            ctx.signalNumber = sig;
            ctx.signalName = SignalToName(sig);
            ctx.faultAddress = info ? info->si_addr : nullptr;

            // Get instruction pointer from ucontext (platform-specific)
#if defined(__x86_64__)
            auto* uc = static_cast<ucontext_t*>(ucontext);
            ctx.instructionPtr = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__i386__)
            auto* uc = static_cast<ucontext_t*>(ucontext);
            ctx.instructionPtr = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_EIP]);
#elif defined(__aarch64__)
            auto* uc = static_cast<ucontext_t*>(ucontext);
            ctx.instructionPtr = reinterpret_cast<void*>(uc->uc_mcontext.pc);
#else
            (void)ucontext;
            ctx.instructionPtr = nullptr;
#endif

            // Write crash header
            SafeWriteLiteral(fd, "\n=== MIKI CRASH DUMP ===\n");
            SafeWriteLiteral(fd, "Signal: ");
            SafeWrite(fd, ctx.signalName, std::strlen(ctx.signalName));
            SafeWriteLiteral(fd, " (");
            SafeWriteUint64(fd, static_cast<uint64_t>(sig));
            SafeWriteLiteral(fd, ")\n");
            SafeWriteLiteral(fd, "Fault address: ");
            SafeWritePtr(fd, ctx.faultAddress);
            SafeWriteLiteral(fd, "\nInstruction pointer: ");
            SafeWritePtr(fd, ctx.instructionPtr);
            SafeWriteLiteral(fd, "\n\n");

            // Call user callback
            if (g_callback) {
                g_callback(ctx, fd);
            }

            // Write footer
            SafeWriteLiteral(fd, "\n=== END CRASH DUMP ===\n");

            // Sync to disk
            fsync(g_emergencyFd);

            // Restore default handler and re-raise
            signal(sig, SIG_DFL);
            raise(sig);
        }

#endif  // _WIN32

    }  // anonymous namespace

    auto InstallCrashHandlers(const std::filesystem::path& emergencyPath, CrashDumpCallback callback) -> bool {
        if (g_installed.load(std::memory_order_relaxed)) {
            return false;  // Already installed
        }

        g_callback = callback;

#ifdef _WIN32
        // Open emergency file
        g_emergencyHandle = CreateFileW(
            emergencyPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
        );
        if (g_emergencyHandle == INVALID_HANDLE_VALUE) {
            return false;
        }

        // Install SEH handler
        g_previousFilter = SetUnhandledExceptionFilter(CrashHandlerWindows);
#else
        // Open emergency file
        g_emergencyFd = open(emergencyPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (g_emergencyFd < 0) {
            return false;
        }

        // Install signal handlers
        struct sigaction sa{};
        sa.sa_sigaction = CrashHandlerPosix;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, &g_previousSigsegv);
        sigaction(SIGABRT, &sa, &g_previousSigabrt);
        sigaction(SIGFPE, &sa, &g_previousSigfpe);
        sigaction(SIGBUS, &sa, &g_previousSigbus);
        sigaction(SIGILL, &sa, &g_previousSigill);
#endif

        g_installed.store(true, std::memory_order_release);
        return true;
    }

    auto UninstallCrashHandlers() -> void {
        if (!g_installed.load(std::memory_order_relaxed)) {
            return;
        }

#ifdef _WIN32
        SetUnhandledExceptionFilter(g_previousFilter);
        g_previousFilter = nullptr;

        if (g_emergencyHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_emergencyHandle);
            g_emergencyHandle = INVALID_HANDLE_VALUE;
        }
#else
        sigaction(SIGSEGV, &g_previousSigsegv, nullptr);
        sigaction(SIGABRT, &g_previousSigabrt, nullptr);
        sigaction(SIGFPE, &g_previousSigfpe, nullptr);
        sigaction(SIGBUS, &g_previousSigbus, nullptr);
        sigaction(SIGILL, &g_previousSigill, nullptr);

        if (g_emergencyFd >= 0) {
            close(g_emergencyFd);
            g_emergencyFd = -1;
        }
#endif

        g_callback = nullptr;
        g_installed.store(false, std::memory_order_release);
    }

    auto AreCrashHandlersInstalled() -> bool {
        return g_installed.load(std::memory_order_relaxed);
    }

    auto GetEmergencyFd() -> intptr_t {
#ifdef _WIN32
        return reinterpret_cast<intptr_t>(g_emergencyHandle);
#else
        return static_cast<intptr_t>(g_emergencyFd);
#endif
    }

    auto SafeWrite(intptr_t fd, const void* buf, size_t len) -> bool {
        if (len == 0) {
            return true;
        }

#ifdef _WIN32
        auto handle = reinterpret_cast<HANDLE>(fd);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD written = 0;
        return WriteFile(handle, buf, static_cast<DWORD>(len), &written, nullptr) != 0;
#else
        if (fd < 0) {
            return false;
        }
        auto result = write(static_cast<int>(fd), buf, len);
        return result >= 0;
#endif
    }

    auto SafeWriteHex(intptr_t fd, const void* data, size_t len) -> void {
        auto* bytes = static_cast<const uint8_t*>(data);
        char buf[2];
        for (size_t i = 0; i < len; ++i) {
            buf[0] = kHexDigits[(bytes[i] >> 4) & 0xF];
            buf[1] = kHexDigits[bytes[i] & 0xF];
            SafeWrite(fd, buf, 2);
        }
    }

    auto SafeWriteUint64(intptr_t fd, uint64_t value) -> void {
        if (value == 0) {
            SafeWrite(fd, "0", 1);
            return;
        }

        char buf[20];  // Max uint64 is 20 digits
        int pos = 19;
        while (value > 0) {
            buf[pos--] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        SafeWrite(fd, buf + pos + 1, static_cast<size_t>(19 - pos));
    }

    auto SafeWritePtr(intptr_t fd, const void* ptr) -> void {
        SafeWrite(fd, "0x", 2);
        auto value = reinterpret_cast<uintptr_t>(ptr);
        char buf[sizeof(uintptr_t) * 2];
        for (int i = static_cast<int>(sizeof(buf)) - 1; i >= 0; --i) {
            buf[i] = kHexDigits[value & 0xF];
            value >>= 4;
        }
        SafeWrite(fd, buf, sizeof(buf));
    }

}  // namespace miki::debug
