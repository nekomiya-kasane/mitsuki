/** @brief PipelineBatchCompiler — startup PSO batch compilation (Phase 3b §12.5 Step 2).
 *
 *  Submits all registered ManagedPipelines to AsyncPipelineCompiler in priority order.
 *  Critical PSOs are submitted first and can be waited on for blocking startup.
 *  Results are delivered via DrainCompleted() on the main thread.
 */

#include "miki/shader/PipelineBatchCompiler.h"

#include "miki/debug/StructuredLogger.h"
#include "miki/frame/DeferredDestructor.h"
#include "miki/shader/AsyncPipelineCompiler.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

namespace miki::shader {

    struct PipelineBatchCompiler::Impl {
        AsyncPipelineCompiler* compiler = nullptr;
        frame::DeferredDestructor* destructor = nullptr;

        struct Entry {
            ManagedPipeline* pipeline = nullptr;
            uint64_t asyncHandleId = 0;
        };

        std::vector<Entry> critical;
        std::vector<Entry> normal;
        std::vector<Entry> low;

        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point criticalDoneTime;
        bool criticalDone = false;
        bool allDone = false;
        uint32_t failedCount = 0;
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    PipelineBatchCompiler::~PipelineBatchCompiler() = default;
    PipelineBatchCompiler::PipelineBatchCompiler(PipelineBatchCompiler&&) noexcept = default;
    auto PipelineBatchCompiler::operator=(PipelineBatchCompiler&&) noexcept -> PipelineBatchCompiler& = default;
    PipelineBatchCompiler::PipelineBatchCompiler(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto PipelineBatchCompiler::Create(AsyncPipelineCompiler& iCompiler, frame::DeferredDestructor* iDestructor)
        -> PipelineBatchCompiler {
        auto impl = std::make_unique<Impl>();
        impl->compiler = &iCompiler;
        impl->destructor = iDestructor;
        return PipelineBatchCompiler(std::move(impl));
    }

    // =========================================================================
    // Add
    // =========================================================================

    auto PipelineBatchCompiler::Add(ManagedPipeline* iPipeline) -> void {
        if (!iPipeline) {
            return;
        }

        Impl::Entry entry{iPipeline, 0};
        switch (iPipeline->GetPriority()) {
            case PipelinePriority::Critical: impl_->critical.push_back(entry); break;
            case PipelinePriority::Normal: impl_->normal.push_back(entry); break;
            case PipelinePriority::Low: impl_->low.push_back(entry); break;
        }
    }

    // =========================================================================
    // SubmitAll
    // =========================================================================

    auto PipelineBatchCompiler::SubmitAll() -> void {
        impl_->startTime = std::chrono::steady_clock::now();
        impl_->criticalDone = false;
        impl_->allDone = false;

        auto submit = [&](std::vector<Impl::Entry>& entries) {
            for (auto& e : entries) {
                if (e.pipeline->CompileAsync(*impl_->compiler)) {
                    e.asyncHandleId = e.pipeline->GetAsyncHandle();
                }
            }
        };

        // Priority order: Critical first
        submit(impl_->critical);
        submit(impl_->normal);
        submit(impl_->low);

        auto total = impl_->critical.size() + impl_->normal.size() + impl_->low.size();
        MIKI_LOG_INFO(
            debug::LogCategory::Shader, "[PipelineBatch] Submitted {} pipelines ({} critical, {} normal, {} low)",
            total, impl_->critical.size(), impl_->normal.size(), impl_->low.size()
        );
    }

    // =========================================================================
    // WaitCritical
    // =========================================================================

    auto PipelineBatchCompiler::WaitCritical(std::chrono::milliseconds iTimeout) -> bool {
        if (impl_->critical.empty()) {
            impl_->criticalDone = true;
            return true;
        }

        auto deadline = std::chrono::steady_clock::now() + iTimeout;

        while (std::chrono::steady_clock::now() < deadline) {
            // Drain completed results first
            DrainCompleted();

            // Check if all critical pipelines are done
            bool allCriticalDone = true;
            for (auto& e : impl_->critical) {
                auto state = e.pipeline->GetState();
                if (state == PipelineState::Compiling || state == PipelineState::Pending) {
                    allCriticalDone = false;
                    break;
                }
            }

            if (allCriticalDone) {
                impl_->criticalDone = true;
                impl_->criticalDoneTime = std::chrono::steady_clock::now();
                auto elapsed
                    = std::chrono::duration_cast<std::chrono::milliseconds>(impl_->criticalDoneTime - impl_->startTime);
                MIKI_LOG_INFO(
                    debug::LogCategory::Shader, "[PipelineBatch] All {} critical PSOs ready in {}ms",
                    impl_->critical.size(), elapsed.count()
                );
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        MIKI_LOG_WARN(
            debug::LogCategory::Shader, "[PipelineBatch] Timeout waiting for critical PSOs ({}ms)", iTimeout.count()
        );
        return false;
    }

    // =========================================================================
    // DrainCompleted
    // =========================================================================

    auto PipelineBatchCompiler::DrainCompleted() -> uint32_t {
        auto results = impl_->compiler->DrainCompleted();
        if (results.empty()) {
            return 0;
        }

        uint32_t delivered = 0;

        // Build a lookup from handle id -> result
        auto findAndDeliver = [&](std::vector<Impl::Entry>& entries) {
            for (auto& e : entries) {
                for (auto& r : results) {
                    if (r.handle.id == e.asyncHandleId) {
                        rhi::PipelineHandle psoHandle;
                        // In a full integration, the blob would be passed to
                        // IDevice::CreateGraphicsPipeline(). Here we track the
                        // compile success and let the caller create the RHI pipeline.
                        e.pipeline->OnCompileComplete(psoHandle, r.success, impl_->destructor);
                        if (!r.success) {
                            impl_->failedCount++;
                        }
                        delivered++;
                        break;
                    }
                }
            }
        };

        findAndDeliver(impl_->critical);
        findAndDeliver(impl_->normal);
        findAndDeliver(impl_->low);

        // Check if all are complete
        if (!impl_->allDone) {
            bool allDone = true;
            auto checkGroup = [&](std::vector<Impl::Entry>& entries) {
                for (auto& e : entries) {
                    auto state = e.pipeline->GetState();
                    if (state == PipelineState::Compiling || state == PipelineState::Pending) {
                        allDone = false;
                    }
                }
            };
            checkGroup(impl_->critical);
            checkGroup(impl_->normal);
            checkGroup(impl_->low);

            if (allDone) {
                impl_->allDone = true;
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - impl_->startTime
                );
                MIKI_LOG_INFO(
                    debug::LogCategory::Shader, "[PipelineBatch] All pipelines complete in {}ms ({} failed)",
                    elapsed.count(), impl_->failedCount
                );
            }
        }

        return delivered;
    }

    // =========================================================================
    // Stats / Status
    // =========================================================================

    auto PipelineBatchCompiler::GetStats() const noexcept -> BatchCompileStats {
        BatchCompileStats s;

        auto count = [](std::vector<Impl::Entry> const& entries, uint32_t& ready, uint32_t& total) {
            total = static_cast<uint32_t>(entries.size());
            ready = 0;
            for (auto& e : entries) {
                if (e.pipeline->GetState() == PipelineState::Ready) {
                    ready++;
                }
            }
        };

        count(impl_->critical, s.criticalReady, s.criticalTotal);
        count(impl_->normal, s.normalReady, s.normalTotal);
        count(impl_->low, s.lowReady, s.lowTotal);

        s.totalPipelines = s.criticalTotal + s.normalTotal + s.lowTotal;
        s.failed = impl_->failedCount;
        s.criticalComplete = impl_->criticalDone;
        s.allComplete = impl_->allDone;

        if (impl_->criticalDone) {
            s.criticalTime
                = std::chrono::duration_cast<std::chrono::milliseconds>(impl_->criticalDoneTime - impl_->startTime);
        }
        if (impl_->allDone) {
            s.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - impl_->startTime
            );
        }

        return s;
    }

    auto PipelineBatchCompiler::IsAllComplete() const noexcept -> bool {
        return impl_->allDone;
    }

    auto PipelineBatchCompiler::FormatStatus() const -> std::string {
        auto s = GetStats();
        auto totalReady = s.criticalReady + s.normalReady + s.lowReady;
        return std::format(
            "PSO Batch: {}/{} ready ({}/{} critical, {}/{} normal, {}/{} low, {} failed)", totalReady, s.totalPipelines,
            s.criticalReady, s.criticalTotal, s.normalReady, s.normalTotal, s.lowReady, s.lowTotal, s.failed
        );
    }

}  // namespace miki::shader
