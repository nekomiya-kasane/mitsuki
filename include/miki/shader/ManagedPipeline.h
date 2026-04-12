/** @file ManagedPipeline.h
 *  @brief Production-grade pipeline lifecycle management (Phase 3b §12.5 Step 1).
 *
 *  ManagedPipeline wraps an RHI PipelineHandle with a 5-state FSM:
 *    Pending → Compiling → Ready → Stale → Compiling → Ready ...
 *                       ↘ Failed → Compiling (hot-reload retry)
 *
 *  Key properties:
 *    - Zero-stall pipeline swap at frame boundary.
 *    - Old pipeline kept alive until DeferredDestructor confirms GPU completion.
 *    - Failed pipelines fall back to previous Ready pipeline (if any).
 *    - Thread-safe state queries (atomic state + mutex for handle swap).
 *
 *  Namespace: miki::shader
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/rhi/Handle.h"
#include "miki/shader/ShaderTypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace miki::frame {
    class DeferredDestructor;
}

namespace miki::shader {

    class AsyncPipelineCompiler;

    // =========================================================================
    // PipelineState — 5-state FSM (§16.5)
    // =========================================================================

    /** @brief Pipeline lifecycle state. */
    enum class PipelineState : uint8_t {
        Pending,    ///< Created, compilation not yet submitted.
        Compiling,  ///< Background compilation in progress.
        Ready,      ///< Compiled and usable for rendering.
        Stale,      ///< Source changed; recompilation needed.
        Failed,     ///< Compilation failed. Previous Ready pipeline (if any) still usable.
    };

    /** @brief Priority class for pipeline creation batching (§16.4). */
    enum class PipelinePriority : uint8_t {
        Critical,  ///< Depth, geometry, resolve, present — blocks first frame.
        Normal,    ///< Standard passes — complete before interactive frame.
        Low,       ///< CAD, CAE, debug, XR — complete asynchronously.
    };

    /** @brief Callback invoked when pipeline state changes. */
    using PipelineStateCallback = std::function<void(PipelineState newState)>;

    // =========================================================================
    // ManagedPipeline
    // =========================================================================

    /** @brief Lifecycle-managed pipeline with async compilation and deferred destruction.
     *
     *  Usage:
     *  1. Create with CompileDesc + priority + optional callback.
     *  2. Call CompileAsync() to submit to AsyncPipelineCompiler.
     *  3. Poll GetState() / GetActivePipeline() per frame.
     *  4. On source change, call MarkStale() → triggers recompile.
     *  5. On failure, call RetryCompile() for hot-reload.
     *
     *  Active pipeline = most recent Ready pipeline. During Compiling/Stale,
     *  the previous Ready pipeline remains active (zero frame stall).
     */
    class ManagedPipeline {
       public:
        ~ManagedPipeline();

        ManagedPipeline(ManagedPipeline const&) = delete;
        auto operator=(ManagedPipeline const&) -> ManagedPipeline& = delete;
        ManagedPipeline(ManagedPipeline&&) noexcept;
        auto operator=(ManagedPipeline&&) noexcept -> ManagedPipeline&;

        /** @brief Create a managed pipeline in Pending state.
         *  @param iDesc     Shader compilation descriptor.
         *  @param iPriority Pipeline priority class for batching.
         *  @param iName     Debug name for logging.
         */
        [[nodiscard]] static auto Create(
            ShaderCompileDesc iDesc, PipelinePriority iPriority = PipelinePriority::Normal, std::string iName = {}
        ) -> ManagedPipeline;

        /** @brief Submit for async compilation. Transitions Pending/Stale/Failed → Compiling.
         *  @param iCompiler The async pipeline compiler (thread pool).
         *  @return true if compilation was submitted, false if already compiling or ready.
         */
        auto CompileAsync(AsyncPipelineCompiler& iCompiler) -> bool;

        /** @brief Mark the pipeline as stale (source changed). Triggers recompile on next CompileAsync().
         *  Does NOT destroy the current Ready pipeline — it remains active.
         */
        auto MarkStale() -> void;

        /** @brief Update the compile descriptor (e.g. for permutation change) and mark stale. */
        auto UpdateDesc(ShaderCompileDesc iNewDesc) -> void;

        /** @brief Called by the pipeline manager's per-frame drain to deliver compile results.
         *  @param iPipeline  Newly compiled pipeline handle (valid on success).
         *  @param iSuccess   Whether compilation succeeded.
         *  @param iDestructor Deferred destructor for retiring old pipelines.
         */
        auto OnCompileComplete(rhi::PipelineHandle iPipeline, bool iSuccess, frame::DeferredDestructor* iDestructor)
            -> void;

        // ── Queries ──────────────────────────────────────────────────

        /** @brief Get the current pipeline FSM state (lock-free). */
        [[nodiscard]] auto GetState() const noexcept -> PipelineState;

        /** @brief Get the currently active pipeline (Ready or previous Ready during recompile).
         *  Returns invalid handle if no pipeline has ever been Ready.
         */
        [[nodiscard]] auto GetActivePipeline() const noexcept -> rhi::PipelineHandle;

        /** @brief Check if a usable pipeline is available (even if Stale/Compiling). */
        [[nodiscard]] auto HasUsablePipeline() const noexcept -> bool;

        /** @brief Get the pipeline priority class. */
        [[nodiscard]] auto GetPriority() const noexcept -> PipelinePriority;

        /** @brief Get the debug name. */
        [[nodiscard]] auto GetName() const noexcept -> std::string_view;

        /** @brief Get the compile descriptor. */
        [[nodiscard]] auto GetDesc() const noexcept -> ShaderCompileDesc const&;

        /** @brief Get the async compilation handle (for polling). */
        [[nodiscard]] auto GetAsyncHandle() const noexcept -> uint64_t;

        /** @brief Set state change callback. */
        auto SetStateCallback(PipelineStateCallback iCallback) -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit ManagedPipeline(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::shader
