/** @file PipelineBatchCompiler.h
 *  @brief Startup pipeline batch compilation with priority classes (Phase 3b §12.5 Step 2).
 *
 *  Orchestrates parallel PSO compilation at startup:
 *    1. Critical (depth, geometry, resolve, present) — blocks first frame.
 *    2. Normal (standard rendering passes) — complete before interactive frame.
 *    3. Low (CAD, CAE, debug, XR) — complete asynchronously, no frame stall.
 *
 *  Performance targets:
 *    - Cold start: all Critical PSOs ready within 2s.
 *    - Warm start (PipelineCache): all Critical within 100ms.
 *    - Non-critical: complete within 5s. No frame stall after first frame.
 *
 *  Namespace: miki::shader
 */
#pragma once

#include "miki/shader/ManagedPipeline.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace miki::frame {
    class DeferredDestructor;
}

namespace miki::shader {

    class AsyncPipelineCompiler;

    // =========================================================================
    // Batch compilation statistics
    // =========================================================================

    struct BatchCompileStats {
        uint32_t totalPipelines = 0;
        uint32_t criticalReady = 0;
        uint32_t criticalTotal = 0;
        uint32_t normalReady = 0;
        uint32_t normalTotal = 0;
        uint32_t lowReady = 0;
        uint32_t lowTotal = 0;
        uint32_t failed = 0;
        std::chrono::milliseconds criticalTime{0};  ///< Wall time until all Critical ready.
        std::chrono::milliseconds totalTime{0};     ///< Wall time until all pipelines ready.
        bool criticalComplete = false;
        bool allComplete = false;
    };

    // =========================================================================
    // PipelineBatchCompiler
    // =========================================================================

    /** @brief Startup batch compiler for all registered managed pipelines.
     *
     *  Typical usage:
     *    1. Create ManagedPipeline for each render pass.
     *    2. Register them via Add().
     *    3. Call SubmitAll() at startup.
     *    4. Call WaitCritical() to block until critical PSOs ready (or timeout).
     *    5. Periodically call DrainCompleted() per frame to deliver results.
     */
    class PipelineBatchCompiler {
       public:
        ~PipelineBatchCompiler();

        PipelineBatchCompiler(PipelineBatchCompiler const&) = delete;
        auto operator=(PipelineBatchCompiler const&) -> PipelineBatchCompiler& = delete;
        PipelineBatchCompiler(PipelineBatchCompiler&&) noexcept;
        auto operator=(PipelineBatchCompiler&&) noexcept -> PipelineBatchCompiler&;

        /** @brief Create a batch compiler.
         *  @param iCompiler The async pipeline compiler backend.
         *  @param iDestructor Deferred destructor for pipeline retirement (may be null).
         */
        [[nodiscard]] static auto Create(
            AsyncPipelineCompiler& iCompiler, frame::DeferredDestructor* iDestructor = nullptr
        ) -> PipelineBatchCompiler;

        /** @brief Register a managed pipeline for batch compilation. */
        auto Add(ManagedPipeline* iPipeline) -> void;

        /** @brief Submit all registered pipelines to the async compiler.
         *  Critical → submitted first, Normal → next, Low → last.
         *  Idempotent: pipelines already Compiling or Ready are skipped.
         */
        auto SubmitAll() -> void;

        /** @brief Block until all Critical-priority pipelines are Ready or Failed, or timeout.
         *  @param iTimeout  Max wait time. Use 0ms for non-blocking poll.
         *  @return true if all Critical pipelines are ready before timeout.
         */
        [[nodiscard]] auto WaitCritical(std::chrono::milliseconds iTimeout = std::chrono::milliseconds{2000}) -> bool;

        /** @brief Deliver completed compilations to their ManagedPipeline owners.
         *  Call once per frame from main thread.
         *  @return Number of completions delivered this call.
         */
        auto DrainCompleted() -> uint32_t;

        /** @brief Get current batch compilation statistics. */
        [[nodiscard]] auto GetStats() const noexcept -> BatchCompileStats;

        /** @brief Check if all pipelines (all priorities) are complete. */
        [[nodiscard]] auto IsAllComplete() const noexcept -> bool;

        /** @brief Format human-readable status. Example: "PSO Batch: 85/88 ready (3 critical, 70 normal, 12 low)" */
        [[nodiscard]] auto FormatStatus() const -> std::string;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit PipelineBatchCompiler(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::shader
