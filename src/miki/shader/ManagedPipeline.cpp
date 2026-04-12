/** @brief ManagedPipeline — 5-state pipeline lifecycle FSM (Phase 3b §12.5 Step 1).
 *
 *  State machine:
 *    Pending ─CompileAsync()→ Compiling ─success→ Ready
 *                                        ─failure→ Failed
 *    Ready   ─MarkStale()→ Stale ─CompileAsync()→ Compiling
 *    Failed  ─CompileAsync()→ Compiling (hot-reload retry)
 *
 *  Thread safety:
 *    - state_ is std::atomic — lock-free reads from any thread.
 *    - activePipeline_ and pendingPipeline_ guarded by pipelineMutex_.
 *    - OnCompileComplete() called from drain thread (main thread).
 */

#include "miki/shader/ManagedPipeline.h"

#include "miki/debug/StructuredLogger.h"
#include "miki/frame/DeferredDestructor.h"
#include "miki/shader/AsyncPipelineCompiler.h"

namespace miki::shader {

    struct ManagedPipeline::Impl {
        ShaderCompileDesc desc;
        PipelinePriority priority = PipelinePriority::Normal;
        std::string name;

        std::atomic<PipelineState> state{PipelineState::Pending};

        mutable std::mutex pipelineMutex;
        rhi::PipelineHandle activePipeline;   // most recent Ready pipeline
        rhi::PipelineHandle pendingPipeline;  // pipeline being compiled (not yet swapped)

        uint64_t asyncHandleId = 0;  // AsyncPSOHandle.id for tracking

        PipelineStateCallback stateCallback;

        void TransitionTo(PipelineState iNewState) {
            auto old = state.exchange(iNewState, std::memory_order_release);
            if (old != iNewState) {
                MIKI_LOG_DEBUG(
                    debug::LogCategory::Shader, "[ManagedPipeline] '{}' {} → {}", name, static_cast<int>(old),
                    static_cast<int>(iNewState)
                );
                if (stateCallback) {
                    stateCallback(iNewState);
                }
            }
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    ManagedPipeline::~ManagedPipeline() = default;
    ManagedPipeline::ManagedPipeline(ManagedPipeline&&) noexcept = default;
    auto ManagedPipeline::operator=(ManagedPipeline&&) noexcept -> ManagedPipeline& = default;
    ManagedPipeline::ManagedPipeline(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto ManagedPipeline::Create(ShaderCompileDesc iDesc, PipelinePriority iPriority, std::string iName)
        -> ManagedPipeline {
        auto impl = std::make_unique<Impl>();
        impl->desc = std::move(iDesc);
        impl->priority = iPriority;
        impl->name = std::move(iName);
        return ManagedPipeline(std::move(impl));
    }

    // =========================================================================
    // CompileAsync — submit to thread pool
    // =========================================================================

    auto ManagedPipeline::CompileAsync(AsyncPipelineCompiler& iCompiler) -> bool {
        auto currentState = impl_->state.load(std::memory_order_acquire);

        // Only submit from Pending, Stale, or Failed states
        if (currentState != PipelineState::Pending && currentState != PipelineState::Stale
            && currentState != PipelineState::Failed) {
            return false;
        }

        AsyncPSORequest req;
        req.desc = impl_->desc;
        req.userData = 0;

        auto handle = iCompiler.Submit(std::move(req));
        impl_->asyncHandleId = handle.id;
        impl_->TransitionTo(PipelineState::Compiling);
        return true;
    }

    // =========================================================================
    // MarkStale / UpdateDesc
    // =========================================================================

    auto ManagedPipeline::MarkStale() -> void {
        auto currentState = impl_->state.load(std::memory_order_acquire);
        if (currentState == PipelineState::Ready) {
            impl_->TransitionTo(PipelineState::Stale);
        }
        // If already Compiling, the result will be discarded and re-queued on next CompileAsync.
    }

    auto ManagedPipeline::UpdateDesc(ShaderCompileDesc iNewDesc) -> void {
        impl_->desc = std::move(iNewDesc);
        MarkStale();
    }

    // =========================================================================
    // OnCompileComplete — called from main thread drain
    // =========================================================================

    auto ManagedPipeline::OnCompileComplete(
        rhi::PipelineHandle iPipeline, bool iSuccess, frame::DeferredDestructor* iDestructor
    ) -> void {
        if (iSuccess && iPipeline.IsValid()) {
            std::lock_guard lock(impl_->pipelineMutex);

            // Retire old pipeline via deferred destruction (GPU may still reference it)
            if (impl_->activePipeline.IsValid() && iDestructor) {
                iDestructor->Destroy(impl_->activePipeline);
            }

            impl_->activePipeline = iPipeline;
            impl_->TransitionTo(PipelineState::Ready);

            MIKI_LOG_INFO(debug::LogCategory::Shader, "[ManagedPipeline] '{}' compilation succeeded", impl_->name);
        } else {
            // Keep the previous Ready pipeline alive (if any) — graceful degradation.
            impl_->TransitionTo(PipelineState::Failed);

            MIKI_LOG_WARN(debug::LogCategory::Shader, "[ManagedPipeline] '{}' compilation failed", impl_->name);
        }
    }

    // =========================================================================
    // Queries (lock-free or minimal lock)
    // =========================================================================

    auto ManagedPipeline::GetState() const noexcept -> PipelineState {
        return impl_->state.load(std::memory_order_acquire);
    }

    auto ManagedPipeline::GetActivePipeline() const noexcept -> rhi::PipelineHandle {
        std::lock_guard lock(impl_->pipelineMutex);
        return impl_->activePipeline;
    }

    auto ManagedPipeline::HasUsablePipeline() const noexcept -> bool {
        std::lock_guard lock(impl_->pipelineMutex);
        return impl_->activePipeline.IsValid();
    }

    auto ManagedPipeline::GetPriority() const noexcept -> PipelinePriority {
        return impl_->priority;
    }

    auto ManagedPipeline::GetName() const noexcept -> std::string_view {
        return impl_->name;
    }

    auto ManagedPipeline::GetDesc() const noexcept -> ShaderCompileDesc const& {
        return impl_->desc;
    }

    auto ManagedPipeline::GetAsyncHandle() const noexcept -> uint64_t {
        return impl_->asyncHandleId;
    }

    auto ManagedPipeline::SetStateCallback(PipelineStateCallback iCallback) -> void {
        impl_->stateCallback = std::move(iCallback);
    }

}  // namespace miki::shader
