/** @brief Async PSO compilation (§16.1).
 *
 *  Thread pool dispatches SlangCompiler::Compile() on worker threads.
 *  Completed results are collected in a lock-free queue for main-thread drain.
 */

#include "miki/shader/AsyncPipelineCompiler.h"

#include "miki/debug/StructuredLogger.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace miki::shader {

    struct AsyncPipelineCompiler::Impl {
        SlangCompiler& compiler;
        std::vector<std::jthread> workers;

        // Work queue
        struct WorkItem {
            AsyncPSOHandle handle;
            AsyncPSORequest request;
            AsyncPSOCallback callback;
        };
        std::mutex queueMutex;
        std::condition_variable queueCV;
        std::deque<WorkItem> workQueue;
        bool shutdownRequested = false;

        // Completed results
        std::mutex resultMutex;
        std::vector<AsyncPSOResult> completedResults;

        // Tracking
        std::atomic<uint64_t> nextId{1};
        std::atomic<uint32_t> pendingCount{0};

        // Per-handle completion tracking
        std::mutex completionMutex;
        std::unordered_set<uint64_t> completedIds;

        explicit Impl(SlangCompiler& iCompiler) : compiler(iCompiler) {}

        void WorkerLoop(std::stop_token iStop) {
            while (!iStop.stop_requested()) {
                WorkItem item;
                {
                    std::unique_lock lock(queueMutex);
                    queueCV.wait(lock, [&] { return shutdownRequested || !workQueue.empty(); });
                    if (shutdownRequested && workQueue.empty()) {
                        return;
                    }
                    if (workQueue.empty()) {
                        continue;
                    }
                    item = std::move(workQueue.front());
                    workQueue.pop_front();
                }

                auto t0 = std::chrono::steady_clock::now();

                AsyncPSOResult result;
                result.handle = item.handle;
                result.userData = item.request.userData;

                auto compileResult = compiler.Compile(item.request.desc);
                if (compileResult) {
                    result.blob = std::move(*compileResult);
                    result.success = true;
                } else {
                    result.success = false;
                }

                auto t1 = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

                MIKI_LOG_DEBUG(
                    debug::LogCategory::Shader, "[AsyncPSO] Compilation {} {} ({:.1f}ms)", item.handle.id,
                    result.success ? "succeeded" : "failed", ms
                );

                // Mark completed
                {
                    std::lock_guard lock(completionMutex);
                    completedIds.insert(item.handle.id);
                }

                // Store for drain, then invoke callback
                // Callback receives a separate result with success/userData but no blob
                // (blob ownership stays with DrainCompleted results)
                {
                    std::lock_guard lock(resultMutex);
                    completedResults.push_back(std::move(result));
                }

                if (item.callback) {
                    AsyncPSOResult cbResult;
                    cbResult.handle = item.handle;
                    cbResult.userData = item.request.userData;
                    cbResult.success = compileResult.has_value();
                    item.callback(std::move(cbResult));
                }

                pendingCount.fetch_sub(1, std::memory_order_release);
            }
        }
    };

    AsyncPipelineCompiler::~AsyncPipelineCompiler() {
        if (impl_) {
            Shutdown();
        }
    }

    AsyncPipelineCompiler::AsyncPipelineCompiler(AsyncPipelineCompiler&&) noexcept = default;
    auto AsyncPipelineCompiler::operator=(AsyncPipelineCompiler&&) noexcept -> AsyncPipelineCompiler& = default;

    AsyncPipelineCompiler::AsyncPipelineCompiler(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto AsyncPipelineCompiler::Create(SlangCompiler& iCompiler, uint32_t iThreadCount)
        -> core::Result<AsyncPipelineCompiler> {
        auto threadCount = iThreadCount > 0 ? iThreadCount : std::max(1u, std::thread::hardware_concurrency() / 2);

        auto impl = std::make_unique<Impl>(iCompiler);

        for (uint32_t i = 0; i < threadCount; ++i) {
            impl->workers.emplace_back([raw = impl.get()](std::stop_token st) { raw->WorkerLoop(st); });
        }

        MIKI_LOG_INFO(debug::LogCategory::Shader, "[AsyncPSO] Created with {} worker threads", threadCount);
        return AsyncPipelineCompiler(std::move(impl));
    }

    auto AsyncPipelineCompiler::Submit(AsyncPSORequest iRequest, AsyncPSOCallback iCallback) -> AsyncPSOHandle {
        AsyncPSOHandle handle{impl_->nextId.fetch_add(1, std::memory_order_relaxed)};

        impl_->pendingCount.fetch_add(1, std::memory_order_release);
        {
            std::lock_guard lock(impl_->queueMutex);
            impl_->workQueue.push_back({handle, std::move(iRequest), std::move(iCallback)});
        }
        impl_->queueCV.notify_one();
        return handle;
    }

    auto AsyncPipelineCompiler::SubmitBatch(std::span<const AsyncPSORequest> iRequests, AsyncPSOCallback iCallback)
        -> std::vector<AsyncPSOHandle> {
        std::vector<AsyncPSOHandle> handles;
        handles.reserve(iRequests.size());

        {
            std::lock_guard lock(impl_->queueMutex);
            for (auto const& req : iRequests) {
                AsyncPSOHandle handle{impl_->nextId.fetch_add(1, std::memory_order_relaxed)};
                handles.push_back(handle);
                impl_->pendingCount.fetch_add(1, std::memory_order_release);
                impl_->workQueue.push_back({handle, req, iCallback});
            }
        }
        impl_->queueCV.notify_all();
        return handles;
    }

    auto AsyncPipelineCompiler::IsComplete(AsyncPSOHandle iHandle) const noexcept -> bool {
        std::lock_guard lock(impl_->completionMutex);
        return impl_->completedIds.contains(iHandle.id);
    }

    auto AsyncPipelineCompiler::DrainCompleted() -> std::vector<AsyncPSOResult> {
        std::lock_guard lock(impl_->resultMutex);
        auto results = std::move(impl_->completedResults);
        impl_->completedResults.clear();
        return results;
    }

    auto AsyncPipelineCompiler::PendingCount() const noexcept -> uint32_t {
        return impl_->pendingCount.load(std::memory_order_acquire);
    }

    auto AsyncPipelineCompiler::WaitAll() -> void {
        while (PendingCount() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    auto AsyncPipelineCompiler::Shutdown() -> void {
        if (!impl_) {
            return;
        }
        {
            std::lock_guard lock(impl_->queueMutex);
            impl_->shutdownRequested = true;
        }
        impl_->queueCV.notify_all();
        for (auto& w : impl_->workers) {
            w.request_stop();
            if (w.joinable()) {
                w.join();
            }
        }
        impl_->workers.clear();
        MIKI_LOG_INFO(debug::LogCategory::Shader, "[AsyncPSO] Shutdown complete");
    }

}  // namespace miki::shader
