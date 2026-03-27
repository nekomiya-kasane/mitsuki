/** @brief StructuredLogger implementation.
 *
 * SPSC ring buffer per thread, background drain thread,
 * sink dispatch via std::visit, Windows SEH crash handler.
 */

#include "miki/debug/StructuredLogger.h"
#include "miki/debug/CrashHandler.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <print>

#include <thread>

#include <tapioca/console.h>
#include <tapioca/style.h>

namespace miki::debug {

    // ================================================================
    // SpscRingBuffer
    // ================================================================

    SpscRingBuffer::SpscRingBuffer() {
        buffer_.fill(std::byte{0});
    }

    auto SpscRingBuffer::TryWrite(const void* data, uint32_t size) -> bool {
        // Header: 4 bytes length prefix
        uint32_t totalSize = size + sizeof(uint32_t);

        uint32_t wp = writePos_.load(std::memory_order_relaxed);
        uint32_t rp = readPos_.load(std::memory_order_acquire);

        // Available space (circular)
        uint32_t used = wp - rp;  // unsigned wrap is intentional
        uint32_t avail = kCapacity - used;

        if (totalSize > avail) {
            return false;
        }

        // Write length header
        uint32_t mask = kCapacity - 1;
        uint32_t pos = wp & mask;

        auto writeBytes = [&](const void* src, uint32_t len) {
            auto srcBytes = static_cast<const std::byte*>(src);
            uint32_t firstChunk = std::min(len, kCapacity - pos);
            std::memcpy(&buffer_[pos], srcBytes, firstChunk);
            if (firstChunk < len) {
                std::memcpy(&buffer_[0], srcBytes + firstChunk, len - firstChunk);
            }
            pos = (pos + len) & mask;
        };

        writeBytes(&size, sizeof(uint32_t));
        writeBytes(data, size);

        writePos_.store(wp + totalSize, std::memory_order_release);
        return true;
    }

    auto SpscRingBuffer::ReadAll(ReadCallback cb, void* userCtx) -> uint32_t {
        uint32_t wp = writePos_.load(std::memory_order_acquire);
        uint32_t rp = readPos_.load(std::memory_order_relaxed);
        uint32_t count = 0;
        uint32_t mask = kCapacity - 1;

        while (rp != wp) {
            // Read length header
            uint32_t pos = rp & mask;
            uint32_t entrySize = 0;
            auto readBytes = [&](void* dst, uint32_t len) {
                auto dstBytes = static_cast<std::byte*>(dst);
                uint32_t firstChunk = std::min(len, kCapacity - pos);
                std::memcpy(dstBytes, &buffer_[pos], firstChunk);
                if (firstChunk < len) {
                    std::memcpy(dstBytes + firstChunk, &buffer_[0], len - firstChunk);
                }
                pos = (pos + len) & mask;
            };

            readBytes(&entrySize, sizeof(uint32_t));

            if (entrySize == 0 || entrySize > kCapacity / 2) {
                // Corrupted — reset ring
                readPos_.store(wp, std::memory_order_release);
                break;
            }

            // Read entry payload into staging buffer
            thread_local std::vector<std::byte> staging;
            staging.resize(entrySize);
            readBytes(staging.data(), entrySize);

            cb(staging.data(), entrySize, userCtx);

            rp += sizeof(uint32_t) + entrySize;
            ++count;
        }

        readPos_.store(rp, std::memory_order_release);
        return count;
    }

    auto SpscRingBuffer::HasData() const -> bool {
        return writePos_.load(std::memory_order_acquire) != readPos_.load(std::memory_order_acquire);
    }

    auto SpscRingBuffer::RawData() const -> std::span<const std::byte> {
        return std::span<const std::byte>{buffer_.data(), buffer_.size()};
    }

    auto SpscRingBuffer::WritePos() const -> uint32_t {
        return writePos_.load(std::memory_order_acquire);
    }

    auto SpscRingBuffer::ReadPos() const -> uint32_t {
        return readPos_.load(std::memory_order_acquire);
    }

    // ================================================================
    // Serialized entry format in ring buffer
    // ================================================================

    // Layout: [level:1][cat:2][line:4][timestampNs:8][threadId:4][fileLen:2][msgLen:2][file...][msg...]

    struct SerializedHeader {
        uint8_t level;
        uint16_t category;
        uint32_t line;
        uint64_t timestampNs;
        uint32_t threadId;
        uint16_t fileLen;
        uint16_t msgLen;
    };
    static_assert(sizeof(SerializedHeader) == 24);

    // ================================================================
    // ConsoleSink (tapioca-based)
    // ================================================================

    namespace {

        auto GetLevelStyle(LogLevel level) -> tapioca::style {
            using namespace tapioca;
            switch (level) {
                case LogLevel::Trace:
                    return {.fg = colors::bright_black, .bg = color::default_color(), .attrs = attr::dim};
                case LogLevel::Debug: return {.fg = colors::cyan, .bg = color::default_color(), .attrs = attr::none};
                case LogLevel::Info: return {.fg = colors::green, .bg = color::default_color(), .attrs = attr::none};
                case LogLevel::Warn: return {.fg = colors::yellow, .bg = color::default_color(), .attrs = attr::bold};
                case LogLevel::Error: return {.fg = colors::red, .bg = color::default_color(), .attrs = attr::bold};
                case LogLevel::Fatal: return {.fg = colors::white, .bg = colors::red, .attrs = attr::bold};
                default: return {};
            }
        }

        auto FormatTimestamp(uint64_t ns) -> std::string {
            using namespace std::chrono;
            // Convert ns to time_point and format with milliseconds
            auto tp = sys_time<milliseconds>{duration_cast<milliseconds>(nanoseconds{ns})};
            auto dp = floor<days>(tp);
            auto tod = hh_mm_ss{tp - dp};
            return std::format(
                "{:02}:{:02}:{:02}.{:03}", tod.hours().count(), tod.minutes().count(), tod.seconds().count(),
                tod.subseconds().count()
            );
        }

        // Thread-local tapioca console for stderr
        auto GetStderrConsole() -> tapioca::basic_console& {
            thread_local tapioca::basic_console console{tapioca::console_config{tapioca::pal::stderr_sink()}};
            return console;
        }

    }  // anonymous namespace

    ConsoleSink::ConsoleSink() {
        colorEnabled_ = GetStderrConsole().color_enabled();
    }

    auto ConsoleSink::Write(const LogEntry& entry) -> void {
        auto& console = GetStderrConsole();
        auto ts = FormatTimestamp(entry.timestampNs);
        auto levelStr = ToString(entry.level);
        auto catStr = ToString(entry.category);

        // Build formatted line
        std::string line;
        line.reserve(128);
        line += '[';
        line += ts;
        line += "] [";
        line.append(levelStr.data(), levelStr.size());
        line += "] [";
        line.append(catStr.data(), catStr.size());
        line += "] ";
        line.append(entry.message.data(), entry.message.size());

        // Write with style
        auto style = GetLevelStyle(entry.level);
        console.styled_write(style, line);
        console.newline();
    }

    auto ConsoleSink::Flush() -> void {
        std::fflush(stderr);
    }

    // ================================================================
    // FileSink
    // ================================================================

    FileSink::FileSink(std::filesystem::path path, bool dailyRotation)
        : basePath_(std::move(path)), rotate_(dailyRotation) {
        file_ = std::fopen(basePath_.string().c_str(), "a");
    }

    FileSink::~FileSink() {
        if (file_) {
            std::fclose(file_);
        }
    }

    FileSink::FileSink(FileSink&& other) noexcept
        : file_(other.file_), basePath_(std::move(other.basePath_)), rotate_(other.rotate_) {
        other.file_ = nullptr;
    }

    auto FileSink::operator=(FileSink&& other) noexcept -> FileSink& {
        if (this != &other) {
            if (file_) {
                std::fclose(file_);
            }
            file_ = other.file_;
            basePath_ = std::move(other.basePath_);
            rotate_ = other.rotate_;
            other.file_ = nullptr;
        }
        return *this;
    }

    auto FileSink::Write(const LogEntry& entry) -> void {
        if (!file_) {
            return;
        }

        auto ts = FormatTimestamp(entry.timestampNs);
        auto levelStr = ToString(entry.level);
        auto catStr = ToString(entry.category);

        std::println(file_, "[{}] [{}] [{}] {}", ts, levelStr, catStr, entry.message);
    }

    auto FileSink::Flush() -> void {
        if (file_) {
            std::fflush(file_);
        }
    }

    auto FileSink::GetPath() const -> const std::filesystem::path& {
        return basePath_;
    }

    // ================================================================
    // JsonSink
    // ================================================================

    JsonSink::JsonSink(std::filesystem::path path) : path_(std::move(path)) {
        file_ = std::fopen(path_.string().c_str(), "a");
    }

    JsonSink::~JsonSink() {
        if (file_) {
            std::fclose(file_);
        }
    }

    JsonSink::JsonSink(JsonSink&& other) noexcept : file_(other.file_), path_(std::move(other.path_)) {
        other.file_ = nullptr;
    }

    auto JsonSink::operator=(JsonSink&& other) noexcept -> JsonSink& {
        if (this != &other) {
            if (file_) {
                std::fclose(file_);
            }
            file_ = other.file_;
            path_ = std::move(other.path_);
            other.file_ = nullptr;
        }
        return *this;
    }

    namespace {

        auto EscapeJson(std::string_view sv) -> std::string {
            std::string out;
            out.reserve(sv.size() + 8);
            for (char c : sv) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                        } else {
                            out += c;
                        }
                        break;
                }
            }
            return out;
        }

    }  // anonymous namespace

    auto JsonSink::Write(const LogEntry& entry) -> void {
        if (!file_) {
            return;
        }

        auto levelStr = ToString(entry.level);
        auto catStr = ToString(entry.category);
        auto msgEscaped = EscapeJson(entry.message);
        auto fileEscaped = EscapeJson(entry.file);

        // Trim trailing spaces from padded level/cat strings
        auto trim = [](std::string_view sv) {
            auto end = sv.find_last_not_of(' ');
            return end != std::string_view::npos ? sv.substr(0, end + 1) : sv;
        };

        auto lvl = trim(levelStr);
        auto cat = trim(catStr);

        std::println(
            file_, R"({{"ts":{},"level":"{}","cat":"{}","msg":"{}","file":"{}","line":{},"tid":{}}})",
            entry.timestampNs, lvl, cat, msgEscaped, fileEscaped, entry.line, entry.threadId
        );
    }

    auto JsonSink::Flush() -> void {
        if (file_) {
            std::fflush(file_);
        }
    }

    // ================================================================
    // CallbackSink
    // ================================================================

    CallbackSink::CallbackSink(Callback cb) : cb_(std::move(cb)) {}

    auto CallbackSink::Write(const LogEntry& entry) -> void {
        if (cb_) {
            cb_(entry);
        }
    }

    auto CallbackSink::Flush() -> void {
        // No-op
    }

    // ================================================================
    // StructuredLogger — thread-local ring management
    // ================================================================

    namespace {

        auto GetCurrentThreadIdHash() -> uint32_t {
            return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        }

    }  // anonymous namespace

    auto StructuredLogger::GetThreadRing() -> SpscRingBuffer& {
        thread_local SpscRingBuffer ring;
        thread_local bool registered = false;

        if (!registered) {
            registered = true;
            auto& logger = Instance();
            std::lock_guard lock(logger.ringsMutex_);
            logger.rings_.push_back({&ring, GetCurrentThreadIdHash()});
        }

        return ring;
    }

    // ================================================================
    // StructuredLogger — core implementation
    // ================================================================

    StructuredLogger::StructuredLogger() {
        // Default all categories to Trace (show everything)
        for (auto& lvl : categoryLevels_) {
            lvl.store(static_cast<uint8_t>(LogLevel::Trace), std::memory_order_relaxed);
        }
    }

    StructuredLogger::~StructuredLogger() {
        Shutdown();
        UninstallCrashHandlers();
    }

    auto StructuredLogger::Instance() -> StructuredLogger& {
        static StructuredLogger instance;
        return instance;
    }

    auto StructuredLogger::AddSink(LogSink sink) -> void {
        std::lock_guard lock(sinksMutex_);
        sinks_.push_back(std::move(sink));
    }

    auto StructuredLogger::ClearSinks() -> void {
        std::lock_guard lock(sinksMutex_);
        sinks_.clear();
    }

    auto StructuredLogger::SetCategoryLevel(LogCategory cat, LogLevel level) -> void {
        auto idx = static_cast<size_t>(cat);
        if (idx < categoryLevels_.size()) {
            categoryLevels_[idx].store(static_cast<uint8_t>(level), std::memory_order_relaxed);
        }
    }

    auto StructuredLogger::GetCategoryLevel(LogCategory cat) const -> LogLevel {
        auto idx = static_cast<size_t>(cat);
        if (idx < categoryLevels_.size()) {
            return static_cast<LogLevel>(categoryLevels_[idx].load(std::memory_order_relaxed));
        }
        return LogLevel::Trace;
    }

    auto StructuredLogger::SetBackpressurePolicy(BackpressurePolicy policy) -> void {
        policy_.store(policy, std::memory_order_relaxed);
    }

    auto StructuredLogger::IsRunning() const -> bool {
        return running_.load(std::memory_order_acquire);
    }

    auto StructuredLogger::GetDroppedCount() const -> uint64_t {
        return droppedCount_.load(std::memory_order_relaxed);
    }

    auto StructuredLogger::ResetDroppedCount() -> void {
        droppedCount_.store(0, std::memory_order_relaxed);
    }

    auto StructuredLogger::WriteToRing(
        LogLevel level, LogCategory cat, std::string_view file, uint32_t line, uint64_t timestampNs,
        std::string_view message
    ) -> void {
        auto& ring = GetThreadRing();

        // Serialize entry
        SerializedHeader hdr{};
        hdr.level = static_cast<uint8_t>(level);
        hdr.category = static_cast<uint16_t>(cat);
        hdr.line = line;
        hdr.timestampNs = timestampNs;
        hdr.threadId = GetCurrentThreadIdHash();
        hdr.fileLen = static_cast<uint16_t>(std::min<size_t>(file.size(), 0xFFFF));
        hdr.msgLen = static_cast<uint16_t>(std::min<size_t>(message.size(), 0xFFFF));

        uint32_t totalPayload = sizeof(SerializedHeader) + hdr.fileLen + hdr.msgLen;

        // Build payload buffer
        thread_local std::vector<std::byte> payload;
        payload.resize(totalPayload);

        auto* dst = payload.data();
        std::memcpy(dst, &hdr, sizeof(hdr));
        dst += sizeof(hdr);
        std::memcpy(dst, file.data(), hdr.fileLen);
        dst += hdr.fileLen;
        std::memcpy(dst, message.data(), hdr.msgLen);

        bool ok = ring.TryWrite(payload.data(), totalPayload);

        if (!ok) {
            auto pol = policy_.load(std::memory_order_relaxed);
            if (pol == BackpressurePolicy::Block) {
                // Spin until space is available
                while (!ring.TryWrite(payload.data(), totalPayload)) {
                    std::this_thread::yield();
                }
            } else {
                droppedCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Notify drain thread
        drainCv_.notify_one();
    }

    auto StructuredLogger::StartDrainThread() -> void {
        if (running_.load(std::memory_order_acquire)) {
            return;
        }
        running_.store(true, std::memory_order_release);
        drainThread_ = std::thread([this] { DrainLoop(); });
    }

    auto StructuredLogger::DrainLoop() -> void {
        while (running_.load(std::memory_order_acquire)) {
            {
                std::unique_lock lock(drainMutex_);
                drainCv_.wait_for(lock, std::chrono::milliseconds(1));
            }

            DrainOnce();

            if (flushRequested_.load(std::memory_order_acquire)) {
                // Drain remaining entries
                DrainOnce();
                // Flush all sinks
                {
                    std::lock_guard lock(sinksMutex_);
                    for (auto& sink : sinks_) {
                        std::visit([](auto& s) { s.Flush(); }, sink);
                    }
                }
                flushRequested_.store(false, std::memory_order_release);
                flushDone_.store(true, std::memory_order_release);
                flushCv_.notify_all();
            }
        }

        // Final drain on shutdown
        DrainOnce();
        {
            std::lock_guard lock(sinksMutex_);
            for (auto& sink : sinks_) {
                std::visit([](auto& s) { s.Flush(); }, sink);
            }
        }
    }

    auto StructuredLogger::DrainOnce() -> uint32_t {
        uint32_t total = 0;

        std::lock_guard lock(ringsMutex_);
        for (auto& reg : rings_) {
            if (!reg.ring) {
                continue;
            }

            struct Ctx {
                StructuredLogger* self;
            };
            Ctx ctx{this};

            total += reg.ring->ReadAll(
                [](const void* data, uint32_t size, void* userCtx) {
                    auto* c = static_cast<Ctx*>(userCtx);
                    if (size < sizeof(SerializedHeader)) {
                        return;
                    }

                    SerializedHeader hdr{};
                    std::memcpy(&hdr, data, sizeof(hdr));

                    auto* bytes = static_cast<const char*>(data);
                    auto* filePtr = bytes + sizeof(SerializedHeader);
                    auto* msgPtr = filePtr + hdr.fileLen;

                    LogEntry entry{};
                    entry.level = static_cast<LogLevel>(hdr.level);
                    entry.category = static_cast<LogCategory>(hdr.category);
                    entry.file = std::string_view{filePtr, hdr.fileLen};
                    entry.message = std::string_view{msgPtr, hdr.msgLen};
                    entry.line = hdr.line;
                    entry.timestampNs = hdr.timestampNs;
                    entry.threadId = hdr.threadId;

                    c->self->DispatchToSinks(entry);
                },
                &ctx
            );
        }

        return total;
    }

    auto StructuredLogger::DispatchToSinks(const LogEntry& entry) -> void {
        std::lock_guard lock(sinksMutex_);
        for (auto& sink : sinks_) {
            std::visit([&entry](auto& s) { s.Write(entry); }, sink);
        }
    }

    auto StructuredLogger::Flush() -> void {
        if (!running_.load(std::memory_order_acquire)) {
            // Drain thread not running — drain synchronously
            DrainOnce();
            std::lock_guard lock(sinksMutex_);
            for (auto& sink : sinks_) {
                std::visit([](auto& s) { s.Flush(); }, sink);
            }
            return;
        }

        flushDone_.store(false, std::memory_order_release);
        flushRequested_.store(true, std::memory_order_release);
        drainCv_.notify_one();

        std::unique_lock lock(flushMutex_);
        flushCv_.wait(lock, [this] { return flushDone_.load(std::memory_order_acquire); });
    }

    auto StructuredLogger::Shutdown() -> void {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }

        running_.store(false, std::memory_order_release);
        drainCv_.notify_all();

        if (drainThread_.joinable()) {
            drainThread_.join();
        }
    }

    // ================================================================
    // Crash handler integration
    // ================================================================

    // Async-signal-safe callback for crash dump (friend of StructuredLogger)
    void LoggerCrashCallback(const CrashContext& ctx, intptr_t fd) {
        (void)ctx;  // Context already written by CrashHandler

        SafeWriteLiteral(fd, "--- Logger Ring Buffer Dump ---\n");

        // Access global logger instance
        // Note: In signal handler, we cannot use locks safely.
        // We read rings_ via friend access — this is acceptable for crash dump.
        auto& logger = StructuredLogger::Instance();
        auto& rings = logger.rings_;

        // Dump each registered ring buffer
        SafeWriteLiteral(fd, "Registered rings: ");
        SafeWriteUint64(fd, static_cast<uint64_t>(rings.size()));
        SafeWriteLiteral(fd, "\n");

        for (size_t i = 0; i < rings.size(); ++i) {
            auto* ring = rings[i].ring;
            auto tid = rings[i].threadId;

            SafeWriteLiteral(fd, "\nRing ");
            SafeWriteUint64(fd, i);
            SafeWriteLiteral(fd, " (thread ");
            SafeWriteUint64(fd, tid);
            SafeWriteLiteral(fd, "):\n");

            // Get ring buffer state via public API
            auto wp = ring->WritePos();
            auto rp = ring->ReadPos();
            auto used = wp - rp;

            SafeWriteLiteral(fd, "  writePos=");
            SafeWriteUint64(fd, wp);
            SafeWriteLiteral(fd, " readPos=");
            SafeWriteUint64(fd, rp);
            SafeWriteLiteral(fd, " used=");
            SafeWriteUint64(fd, used);
            SafeWriteLiteral(fd, "\n");

            // Dump first 256 bytes of pending data as hex
            if (used > 0) {
                auto rawData = ring->RawData();
                auto dumpLen = std::min<size_t>(used, 256);
                auto startIdx = rp & (SpscRingBuffer::kCapacity - 1);
                SafeWriteLiteral(fd, "  Data (first 256 bytes): ");
                SafeWriteHex(fd, &rawData[startIdx], dumpLen);
                SafeWriteLiteral(fd, "\n");
            }
        }

        SafeWriteLiteral(fd, "\n--- End Ring Buffer Dump ---\n");
    }

    auto StructuredLogger::InstallCrashHandlers() -> void {
        if (emergencyPath_.empty()) {
            return;
        }
        miki::debug::InstallCrashHandlers(emergencyPath_, LoggerCrashCallback);
    }

    auto StructuredLogger::SetEmergencyOutputPath(std::filesystem::path path) -> void {
        emergencyPath_ = std::move(path);
    }

}  // namespace miki::debug
