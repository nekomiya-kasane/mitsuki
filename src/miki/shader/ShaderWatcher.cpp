/** @brief ShaderWatcher implementation.
 *
 * Monitors a directory for .slang file changes, builds a transitive
 * include dependency graph, recompiles affected shaders, and queues results.
 *
 * Architecture improvements over reference:
 *   - std::jthread with std::stop_token for clean cancellation
 *   - Transitive dependency closure via BFS (not 1-level)
 *   - Platform: Windows ReadDirectoryChangesW, POSIX polling fallback
 */

#include "miki/shader/ShaderWatcher.h"
#include "miki/shader/SlangCompiler.h"

#include <atomic>
#include <chrono>
#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <Windows.h>
#endif

namespace miki::shader {

    namespace fs = std::filesystem;

    // ===========================================================================
    // IncludeDepGraph -- transitive dependency tracking (BFS closure)
    // ===========================================================================

    class IncludeDepGraph {
       public:
        /** @brief Scan a .slang file for #include / import directives. */
        void ScanFile(fs::path const& iFile) {
            auto canonical = fs::canonical(iFile).string();
            auto& deps = graph_[canonical];
            deps.clear();

            std::ifstream in(iFile);
            if (!in.is_open()) {
                return;
            }

            static std::regex const includeRe(R"(^\s*#\s*include\s*\"([^\"]+)\")");
            static std::regex const importRe(R"(^\s*import\s+([a-zA-Z0-9_./\\]+)\s*;)");
            auto parentDir = iFile.parent_path();

            std::string line;
            while (std::getline(in, line)) {
                std::smatch match;
                if (std::regex_search(line, match, includeRe)) {
                    auto incPath = parentDir / match[1].str();
                    if (fs::exists(incPath)) {
                        deps.insert(fs::canonical(incPath).string());
                    }
                } else if (std::regex_search(line, match, importRe)) {
                    auto modName = match[1].str();
                    std::replace(modName.begin(), modName.end(), '.', '/');
                    auto modPath = parentDir / (modName + ".slang");
                    if (fs::exists(modPath)) {
                        deps.insert(fs::canonical(modPath).string());
                    }
                }
            }
        }

        /** @brief Scan all .slang files in a directory. */
        void ScanDirectory(fs::path const& iDir) {
            std::error_code ec;
            for (auto const& entry : fs::recursive_directory_iterator(iDir, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".slang") {
                    ScanFile(entry.path());
                }
            }
        }

        /** @brief Get all files that transitively depend on the changed file (BFS). */
        [[nodiscard]] auto GetAffected(std::string const& iChangedFile) const -> std::unordered_set<std::string> {
            // Build reverse graph (dependee -> dependents)
            std::unordered_map<std::string, std::vector<std::string>> reverseGraph;
            for (auto const& [file, deps] : graph_) {
                for (auto const& dep : deps) {
                    reverseGraph[dep].push_back(file);
                }
            }

            // BFS from changed file
            std::unordered_set<std::string> affected;
            std::deque<std::string> queue;

            // The changed file itself is affected
            affected.insert(iChangedFile);
            queue.push_back(iChangedFile);

            while (!queue.empty()) {
                auto current = std::move(queue.front());
                queue.pop_front();

                auto it = reverseGraph.find(current);
                if (it == reverseGraph.end()) {
                    continue;
                }
                for (auto const& dependent : it->second) {
                    if (affected.insert(dependent).second) {
                        queue.push_back(dependent);
                    }
                }
            }
            return affected;
        }

       private:
        std::unordered_map<std::string, std::unordered_set<std::string>> graph_;
    };

    // ===========================================================================
    // Impl
    // ===========================================================================

    struct ShaderWatcher::Impl {
        SlangCompiler* compiler = nullptr;
        ShaderWatcherConfig config;
        fs::path watchDir;
        IncludeDepGraph depGraph;

        std::jthread watchThread;
        std::atomic<uint64_t> generation{0};
        std::atomic<bool> running{false};

        mutable std::mutex changeMutex;
        std::vector<ShaderChange> pendingChanges;

        mutable std::mutex errorMutex;
        std::vector<ShaderError> lastErrors;

        // File modification time tracking
        std::unordered_map<std::string, fs::file_time_type> lastWriteTimes;

        void RecompileFile(fs::path const& iFile) {
            for (auto target : config.targets) {
                ShaderCompileDesc desc;
                desc.sourcePath = iFile;
                desc.entryPoint = "main";
                desc.stage = ShaderStage::Compute;  // Default; real usage needs stage detection
                desc.target = target;

                auto result = compiler->Compile(desc);
                if (result.has_value() && !result->data.empty()) {
                    auto gen = generation.fetch_add(1, std::memory_order_relaxed) + 1;
                    ShaderChange change;
                    change.path = iFile;
                    change.target = target;
                    change.blob = std::move(*result);
                    change.generation = gen;

                    std::lock_guard lock(changeMutex);
                    pendingChanges.push_back(std::move(change));
                } else {
                    ShaderError err;
                    err.path = iFile;
                    err.message = "Compilation failed for target " + std::to_string(static_cast<int>(target));
                    auto diags = compiler->GetLastDiagnostics();
                    if (!diags.empty()) {
                        err.message = diags[0].message;
                    }

                    std::lock_guard lock(errorMutex);
                    lastErrors.push_back(std::move(err));
                }
            }
        }

        void ProcessChanges(std::unordered_set<std::string> const& iChangedFiles) {
            {
                std::lock_guard lock(errorMutex);
                lastErrors.clear();
            }

            std::unordered_set<std::string> allAffected;
            for (auto const& changed : iChangedFiles) {
                depGraph.ScanFile(changed);
                auto affected = depGraph.GetAffected(changed);
                allAffected.merge(std::move(affected));
            }

            for (auto const& file : allAffected) {
                if (fs::path(file).extension() == ".slang") {
                    RecompileFile(file);
                }
            }
        }

#ifdef _WIN32
        void WatchThreadFunc(std::stop_token iStopToken) {
            running.store(true, std::memory_order_release);

            HANDLE hDir = CreateFileW(
                watchDir.wstring().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr
            );
            if (hDir == INVALID_HANDLE_VALUE) {
                running.store(false, std::memory_order_release);
                return;
            }

            OVERLAPPED overlapped = {};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent) {
                CloseHandle(hDir);
                running.store(false, std::memory_order_release);
                return;
            }

            alignas(DWORD) uint8_t buffer[4096];

            while (!iStopToken.stop_requested()) {
                BOOL ok = ReadDirectoryChangesW(
                    hDir, buffer, sizeof(buffer), TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                    nullptr, &overlapped, nullptr
                );
                if (!ok) {
                    break;
                }

                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, config.debounceMs + 100);
                if (iStopToken.stop_requested()) {
                    break;
                }

                if (waitResult == WAIT_OBJECT_0) {
                    DWORD bytesReturned = 0;
                    GetOverlappedResult(hDir, &overlapped, &bytesReturned, FALSE);
                    ResetEvent(overlapped.hEvent);

                    // Debounce
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.debounceMs));

                    std::unordered_set<std::string> changedFiles;
                    auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
                    while (true) {
                        std::wstring wname(info->FileName, info->FileNameLength / sizeof(wchar_t));
                        auto fullPath = watchDir / wname;
                        if (fullPath.extension() == ".slang" && fs::exists(fullPath)) {
                            changedFiles.insert(fs::canonical(fullPath).string());
                        }
                        if (info->NextEntryOffset == 0) {
                            break;
                        }
                        info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                            reinterpret_cast<uint8_t*>(info) + info->NextEntryOffset
                        );
                    }

                    if (!changedFiles.empty()) {
                        ProcessChanges(changedFiles);
                    }
                }
            }

            CloseHandle(overlapped.hEvent);
            CloseHandle(hDir);
            running.store(false, std::memory_order_release);
        }
#else
        void WatchThreadFunc(std::stop_token iStopToken) {
            running.store(true, std::memory_order_release);

            // Initial scan
            std::error_code ec;
            for (auto const& entry : fs::recursive_directory_iterator(watchDir, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".slang") {
                    auto canonical = fs::canonical(entry.path()).string();
                    lastWriteTimes[canonical] = fs::last_write_time(entry.path());
                }
            }

            while (!iStopToken.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (iStopToken.stop_requested()) {
                    break;
                }

                std::unordered_set<std::string> changedFiles;
                for (auto const& entry : fs::recursive_directory_iterator(watchDir, ec)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".slang") {
                        continue;
                    }
                    auto canonical = fs::canonical(entry.path()).string();
                    auto modTime = fs::last_write_time(entry.path());
                    auto it = lastWriteTimes.find(canonical);
                    if (it == lastWriteTimes.end() || it->second != modTime) {
                        lastWriteTimes[canonical] = modTime;
                        changedFiles.insert(canonical);
                    }
                }

                if (!changedFiles.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.debounceMs));
                    ProcessChanges(changedFiles);
                }
            }

            running.store(false, std::memory_order_release);
        }
#endif
    };

    // ===========================================================================
    // Public API
    // ===========================================================================

    ShaderWatcher::ShaderWatcher(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}
    ShaderWatcher::~ShaderWatcher() {
        Stop();
    }
    ShaderWatcher::ShaderWatcher(ShaderWatcher&&) noexcept = default;
    auto ShaderWatcher::operator=(ShaderWatcher&&) noexcept -> ShaderWatcher& = default;

    auto ShaderWatcher::Create(SlangCompiler& iCompiler, ShaderWatcherConfig iConfig) -> core::Result<ShaderWatcher> {
        auto impl = std::make_unique<Impl>();
        impl->compiler = &iCompiler;
        impl->config = std::move(iConfig);
        return ShaderWatcher(std::move(impl));
    }

    auto ShaderWatcher::Start(fs::path const& iWatchDir) -> core::Result<void> {
        if (impl_->running.load(std::memory_order_acquire)) {
            return {};  // Already running
        }
        if (!fs::is_directory(iWatchDir)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        impl_->watchDir = fs::canonical(iWatchDir);
        impl_->depGraph.ScanDirectory(impl_->watchDir);
        impl_->watchThread = std::jthread([this](std::stop_token st) { impl_->WatchThreadFunc(std::move(st)); });
        return {};
    }

    auto ShaderWatcher::Stop() -> void {
        if (impl_->watchThread.joinable()) {
            impl_->watchThread.request_stop();
            impl_->watchThread.join();
        }
    }

    auto ShaderWatcher::Poll() -> std::vector<ShaderChange> {
        std::lock_guard lock(impl_->changeMutex);
        auto changes = std::move(impl_->pendingChanges);
        impl_->pendingChanges.clear();
        return changes;
    }

    auto ShaderWatcher::GetGeneration() const noexcept -> uint64_t {
        return impl_->generation.load(std::memory_order_acquire);
    }

    auto ShaderWatcher::GetLastErrors() const -> std::span<const ShaderError> {
        std::lock_guard lock(impl_->errorMutex);
        return impl_->lastErrors;
    }

    auto ShaderWatcher::IsRunning() const noexcept -> bool {
        return impl_->running.load(std::memory_order_acquire);
    }

}  // namespace miki::shader
