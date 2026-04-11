/** @file StackTrace.cpp
 *  @brief Cross-platform stack trace capture + tapioca-styled pretty-print.
 *
 *  Windows: CaptureStackBackTrace → DbgHelp symbol resolution
 *  POSIX:   backtrace → dladdr + __cxa_demangle
 */

#include "miki/debug/StackTrace.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>

#include <tapioca/console.h>
#include <tapioca/style.h>

// =============================================================================
// Platform includes
// =============================================================================

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#    include <dbghelp.h>
#else
#    include <cxxabi.h>
#    include <dlfcn.h>
#    include <execinfo.h>
#endif

namespace miki::debug {

    // =========================================================================
    // Symbol resolver (thread-safe singleton init)
    // =========================================================================

#ifdef _WIN32

    namespace {
        std::once_flag g_symInitFlag;
        std::mutex g_dbgHelpMutex;  // DbgHelp is single-threaded

        void EnsureSymbolsInitialized() {
            std::call_once(g_symInitFlag, [] {
                SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
                SymInitialize(GetCurrentProcess(), nullptr, TRUE);
            });
        }

        /** @brief Extract basename from a full path. */
        auto ExtractModuleName(const char* path) -> std::string {
            if (!path || !*path) return {};
            std::string_view sv{path};
            auto pos = sv.find_last_of("\\/");
            return std::string{(pos != std::string_view::npos) ? sv.substr(pos + 1) : sv};
        }
    }  // namespace

    auto StackTrace::Capture(uint32_t skipFrames, uint32_t maxFrames) -> StackTrace {
        EnsureSymbolsInitialized();

        // +1 to skip Capture() itself
        uint32_t totalSkip = skipFrames + 1;
        uint32_t totalCapture = totalSkip + maxFrames;
        if (totalCapture > 128) totalCapture = 128;

        std::vector<void*> rawFrames(totalCapture);
        USHORT captured = CaptureStackBackTrace(
            static_cast<DWORD>(totalSkip), static_cast<DWORD>(maxFrames),
            rawFrames.data(), nullptr
        );

        StackTrace trace;
        trace.frames_.reserve(captured);

        // Lock DbgHelp for the entire resolution pass
        std::lock_guard lock(g_dbgHelpMutex);
        HANDLE proc = GetCurrentProcess();

        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)];
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = MAX_SYM_NAME;

        IMAGEHLP_LINE64 lineInfo{};
        lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        IMAGEHLP_MODULE64 modInfo{};
        modInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);

        for (USHORT i = 0; i < captured; ++i) {
            StackFrame frame;
            frame.address = rawFrames[i];
            auto addr = reinterpret_cast<DWORD64>(rawFrames[i]);

            // Symbol name + displacement
            DWORD64 symDisplacement = 0;
            if (SymFromAddr(proc, addr, &symDisplacement, sym)) {
                frame.symbol = std::string(sym->Name, sym->NameLen);
                frame.offset = static_cast<uint64_t>(symDisplacement);
            }

            // Source file + line
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(proc, addr, &lineDisplacement, &lineInfo)) {
                frame.sourceFile = lineInfo.FileName;  // keep full path for ToString()
                frame.sourceLine = lineInfo.LineNumber;
            }

            // Module name
            if (SymGetModuleInfo64(proc, addr, &modInfo)) {
                frame.module = ExtractModuleName(modInfo.ImageName);
            }

            trace.frames_.push_back(std::move(frame));
        }

        return trace;
    }

#else  // POSIX

    namespace {
        auto DemangleSymbol(const char* mangled) -> std::string {
            if (!mangled || !*mangled) return {};
            int status = 0;
            char* demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
            std::string result = (status == 0 && demangled) ? demangled : mangled;
            free(demangled);
            return result;
        }

        auto ExtractModuleName(const char* path) -> std::string {
            if (!path || !*path) return {};
            std::string_view sv{path};
            auto pos = sv.find_last_of('/');
            return std::string{(pos != std::string_view::npos) ? sv.substr(pos + 1) : sv};
        }
    }  // namespace

    auto StackTrace::Capture(uint32_t skipFrames, uint32_t maxFrames) -> StackTrace {
        uint32_t totalSkip = skipFrames + 1;  // +1 to skip Capture() itself
        uint32_t totalCapture = totalSkip + maxFrames;
        if (totalCapture > 128) totalCapture = 128;

        std::vector<void*> rawFrames(totalCapture);
        int captured = backtrace(rawFrames.data(), static_cast<int>(totalCapture));

        StackTrace trace;
        int start = static_cast<int>(totalSkip);
        if (start > captured) start = captured;
        trace.frames_.reserve(static_cast<size_t>(captured - start));

        for (int i = start; i < captured; ++i) {
            StackFrame frame;
            frame.address = rawFrames[i];

            Dl_info info{};
            if (dladdr(rawFrames[i], &info)) {
                if (info.dli_sname) {
                    frame.symbol = DemangleSymbol(info.dli_sname);
                    frame.offset = static_cast<uint64_t>(
                        reinterpret_cast<const char*>(rawFrames[i])
                        - reinterpret_cast<const char*>(info.dli_saddr)
                    );
                }
                if (info.dli_fname) {
                    frame.module = ExtractModuleName(info.dli_fname);
                }
            }
            // Note: dladdr doesn't provide source file/line. addr2line or libdw would be needed.
            trace.frames_.push_back(std::move(frame));
        }

        return trace;
    }

#endif  // _WIN32

    // =========================================================================
    // Accessors
    // =========================================================================

    auto StackTrace::GetFrames() const -> const std::vector<StackFrame>& { return frames_; }
    auto StackTrace::Size() const -> uint32_t { return static_cast<uint32_t>(frames_.size()); }
    auto StackTrace::Empty() const -> bool { return frames_.empty(); }

    // =========================================================================
    // FormatFrameCompact — single line, ≤ ~200 chars, no ANSI
    // =========================================================================

    namespace {
        /** @brief Repeat a UTF-8 string N times. */
        auto RepeatStr(std::string_view s, uint32_t n) -> std::string {
            std::string result;
            result.reserve(s.size() * n);
            for (uint32_t i = 0; i < n; ++i) result += s;
            return result;
        }
    }  // namespace

    auto StackTrace::FormatFrameCompact(const StackFrame& frame, uint32_t index) -> std::string {
        std::string sym = frame.symbol.empty() ? "<unknown>" : frame.symbol;

        // Truncate long symbol names
        constexpr size_t kMaxSymLen = 80;
        if (sym.size() > kMaxSymLen) {
            sym.resize(kMaxSymLen - 1);
            sym += "\u2026";  // …
        }

        if (!frame.sourceFile.empty()) {
            // Extract just filename for compact format
            std::string_view file{frame.sourceFile};
            auto pos = file.find_last_of("\\/");
            if (pos != std::string_view::npos) file = file.substr(pos + 1);
            return std::format("#{:<2} {} ({}:{} +0x{:X})", index, sym, file, frame.sourceLine, frame.offset);
        }

        if (!frame.module.empty()) {
            return std::format("#{:<2} {} [{}+0x{:X}]", index, sym, frame.module, frame.offset);
        }

        return std::format("#{:<2} {} (0x{:X})", index, sym, reinterpret_cast<uintptr_t>(frame.address));
    }

    // =========================================================================
    // ToString — plain-text multi-line
    // =========================================================================

    auto StackTrace::ToString() const -> std::string {
        std::string result;
        result.reserve(frames_.size() * 120);
        result += std::format("Stack Trace ({} frames):\n", frames_.size());
        for (uint32_t i = 0; i < frames_.size(); ++i) {
            result += "  ";
            result += FormatFrameCompact(frames_[i], i);
            result += '\n';
        }
        return result;
    }

    // =========================================================================
    // PrintColored — rich tapioca-styled output to stderr
    // =========================================================================

    namespace {
        // Shared thread-local console for stderr
        auto GetStderrConsole() -> tapioca::basic_console& {
            thread_local tapioca::basic_console console{
                tapioca::console_config{tapioca::pal::stderr_sink()}
            };
            return console;
        }

        // Style constants
        using namespace tapioca;
        // Box drawing and structural elements
        constexpr style kBoxStyle{.fg = colors::bright_black, .bg = color::default_color(), .attrs = attr::dim};
        // Frame index (#0, #1, ...)
        constexpr style kIndexStyle{.fg = colors::white, .bg = color::default_color(), .attrs = attr::bold};
        // Function name
        constexpr style kSymbolStyle{.fg = colors::bright_cyan, .bg = color::default_color(), .attrs = attr::bold};
        // Unknown / unresolved symbol
        constexpr style kUnknownStyle{
            .fg = colors::bright_black, .bg = color::default_color(), .attrs = attr::dim | attr::italic
        };
        // Source file path
        constexpr style kFileStyle{.fg = colors::green, .bg = color::default_color(), .attrs = attr::none};
        // Line number
        constexpr style kLineStyle{.fg = colors::bright_green, .bg = color::default_color(), .attrs = attr::bold};
        // Offset / address
        constexpr style kOffsetStyle{.fg = colors::bright_black, .bg = color::default_color(), .attrs = attr::dim};
        // Module name
        constexpr style kModuleStyle{.fg = colors::yellow, .bg = color::default_color(), .attrs = attr::dim};
        // Title text
        constexpr style kTitleStyle{.fg = colors::bright_red, .bg = color::default_color(), .attrs = attr::bold};
        // Frame count badge
        constexpr style kBadgeStyle{
            .fg = colors::bright_white, .bg = color::default_color(), .attrs = attr::bold | attr::dim
        };
    }  // namespace

    void StackTrace::PrintColored(std::string_view title) const {
        if (frames_.empty()) return;

        auto& con = GetStderrConsole();
        uint32_t termWidth = con.term_width();
        if (termWidth < 40) termWidth = 80;
        if (termWidth > 160) termWidth = 160;

        // Compute content width (inside box borders)
        uint32_t innerWidth = termWidth - 4;  // "│ " ... " │"

        // Helper: pad to fill remaining inner width
        auto padRight = [&](std::string& buf, uint32_t usedCols) {
            if (usedCols < innerWidth) {
                buf.append(innerWidth - usedCols, ' ');
            }
        };

        std::string buf;
        buf.reserve(4096);

        // ── Top border ──────────────────────────────────────────────────────
        // "╭─ Title ──...── N frames ─╮"
        {
            std::string titleStr{title};
            std::string countStr = std::format("{} frames", frames_.size());

            // Calculate decorative fill
            // "╭─ " + title + " ─...─ " + count + " ─╮"
            uint32_t fixedChars = 6;  // "╭─ " + " ─╮"
            uint32_t contentLen = static_cast<uint32_t>(titleStr.size() + countStr.size()) + 3;  // " ─ " separator
            uint32_t fillLen = (termWidth > fixedChars + contentLen) ? (termWidth - fixedChars - contentLen) : 1;

            con.emit_styled(kBoxStyle, "\u256D\u2500 ", buf);  // ╭─
            con.emit_styled(kTitleStyle, titleStr, buf);
            con.emit_styled(kBoxStyle, " ", buf);
            std::string fill = RepeatStr("\u2500", fillLen);  // ─ repeated
            con.emit_styled(kBoxStyle, fill, buf);
            con.emit_styled(kBoxStyle, " ", buf);
            con.emit_styled(kBadgeStyle, countStr, buf);
            con.emit_styled(kBoxStyle, " \u2500\u256E", buf);  // ─╮
            con.emit_reset(buf);
            buf += '\n';
        }

        // ── Empty line inside box ───────────────────────────────────────────
        auto emitEmptyLine = [&]() {
            con.emit_styled(kBoxStyle, "\u2502", buf);  // │
            buf.append(termWidth - 2, ' ');
            con.emit_styled(kBoxStyle, "\u2502", buf);  // │
            con.emit_reset(buf);
            buf += '\n';
        };

        emitEmptyLine();

        // ── Frame entries ───────────────────────────────────────────────────
        for (uint32_t i = 0; i < static_cast<uint32_t>(frames_.size()); ++i) {
            const auto& f = frames_[i];

            // Line 1: "│  #N  symbolName                                    │"
            {
                uint32_t used = 0;
                con.emit_styled(kBoxStyle, "\u2502  ", buf);  // │
                used += 3;

                std::string indexStr = std::format("#{:<3}", i);
                con.emit_styled(kIndexStyle, indexStr, buf);
                used += static_cast<uint32_t>(indexStr.size());

                std::string sym = f.symbol.empty() ? "<unknown>" : f.symbol;
                uint32_t maxSymWidth = (innerWidth > used + 2) ? (innerWidth - used - 1) : 20;
                bool truncated = false;
                if (sym.size() > maxSymWidth) {
                    sym.resize(maxSymWidth - 1);
                    truncated = true;
                }

                if (f.symbol.empty()) {
                    con.emit_styled(kUnknownStyle, sym, buf);
                } else {
                    con.emit_styled(kSymbolStyle, sym, buf);
                    if (truncated) con.emit_styled(kBoxStyle, "\u2026", buf);  // …
                }
                used += static_cast<uint32_t>(sym.size()) + (truncated ? 1 : 0);

                padRight(buf, used);
                con.emit_styled(kBoxStyle, " \u2502", buf);  // │
                con.emit_reset(buf);
                buf += '\n';
            }

            // Line 2: "│       @ file:line                           +0xOFF │"
            {
                uint32_t used = 0;
                con.emit_styled(kBoxStyle, "\u2502      ", buf);  // │ + indent
                used += 7;

                if (!f.sourceFile.empty()) {
                    // Extract just filename for display
                    std::string_view filePath{f.sourceFile};
                    auto pos = filePath.find_last_of("\\/");
                    std::string_view fileName = (pos != std::string_view::npos) ? filePath.substr(pos + 1) : filePath;

                    con.emit_styled(kOffsetStyle, "@ ", buf);
                    used += 2;
                    con.emit_styled(kFileStyle, fileName, buf);
                    used += static_cast<uint32_t>(fileName.size());
                    con.emit_styled(kBoxStyle, ":", buf);
                    used += 1;
                    std::string lineStr = std::to_string(f.sourceLine);
                    con.emit_styled(kLineStyle, lineStr, buf);
                    used += static_cast<uint32_t>(lineStr.size());
                } else if (!f.module.empty()) {
                    con.emit_styled(kOffsetStyle, "@ ", buf);
                    used += 2;
                    con.emit_styled(kModuleStyle, f.module, buf);
                    used += static_cast<uint32_t>(f.module.size());
                } else {
                    std::string addrStr = std::format("@ 0x{:X}", reinterpret_cast<uintptr_t>(f.address));
                    con.emit_styled(kOffsetStyle, addrStr, buf);
                    used += static_cast<uint32_t>(addrStr.size());
                }

                // Right-align the offset
                std::string offsetStr = std::format("+0x{:X}", f.offset);
                uint32_t rightMargin = static_cast<uint32_t>(offsetStr.size()) + 1;  // +1 for trailing space
                if (used + rightMargin + 2 < innerWidth) {
                    uint32_t gap = innerWidth - used - rightMargin;
                    buf.append(gap, ' ');
                    con.emit_styled(kOffsetStyle, offsetStr, buf);
                } else {
                    buf += ' ';
                    con.emit_styled(kOffsetStyle, offsetStr, buf);
                    used += 1 + static_cast<uint32_t>(offsetStr.size());
                    padRight(buf, used);
                }
                con.emit_styled(kBoxStyle, " \u2502", buf);  // │
                con.emit_reset(buf);
                buf += '\n';
            }

            // Separator between frames (thin dotted line) — skip after last
            if (i + 1 < static_cast<uint32_t>(frames_.size())) {
                con.emit_styled(kBoxStyle, "\u2502  ", buf);  // │
                std::string dots = RepeatStr("\u00B7", innerWidth - 2);  // · repeated
                con.emit_styled(kBoxStyle, dots, buf);
                con.emit_styled(kBoxStyle, " \u2502", buf);   // │
                con.emit_reset(buf);
                buf += '\n';
            }
        }

        // ── Bottom border ───────────────────────────────────────────────────
        emitEmptyLine();
        {
            con.emit_styled(kBoxStyle, "\u2570", buf);  // ╰
            std::string fill = RepeatStr("\u2500", termWidth - 2);  // ─ repeated
            con.emit_styled(kBoxStyle, fill, buf);
            con.emit_styled(kBoxStyle, "\u256F", buf);  // ╯
            con.emit_reset(buf);
            buf += '\n';
        }

        // Flush the entire buffer in one write
        con.flush_to_sink(buf);
        std::fflush(stderr);
    }

}  // namespace miki::debug
