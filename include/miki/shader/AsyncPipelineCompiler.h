/** @file AsyncPipelineCompiler.h
 *  @brief Async PSO compilation using thread pool (§16.1).
 *
 *  Offloads pipeline state object creation to background threads.
 *  Uses std::jthread + std::latch for completion signaling.
 *  Main thread polls IsComplete() and swaps pipeline at frame boundary.
 *
 *  Namespace: miki::shader
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/shader/ShaderTypes.h"
#include "miki/shader/SlangCompiler.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace miki::shader {

    /** @brief Handle to a pending async PSO compilation. */
    struct AsyncPSOHandle {
        uint64_t id = 0;
        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return id != 0; }
    };

    /** @brief Compilation request for async PSO. */
    struct AsyncPSORequest {
        ShaderCompileDesc desc;
        uint64_t userData = 0;  ///< Caller-defined tag (e.g. pipeline slot index)
    };

    /** @brief Result of a completed async PSO compilation. */
    struct AsyncPSOResult {
        AsyncPSOHandle handle;
        uint64_t userData = 0;
        ShaderBlob blob;  ///< Compiled shader blob (moved in on completion)
        bool success = false;
    };

    /** @brief Callback invoked when an async PSO compilation completes (on worker thread). */
    using AsyncPSOCallback = std::function<void(AsyncPSOResult&&)>;

    /** @brief Async PSO compiler backed by a thread pool.
     *
     *  Architecture:
     *  - Submit() enqueues compilation to the thread pool (non-blocking).
     *  - Worker threads call SlangCompiler::Compile() independently.
     *  - Main thread polls IsComplete() per frame, collects results.
     *  - Pipeline swap happens at BeginFrame boundary.
     */
    class AsyncPipelineCompiler {
       public:
        ~AsyncPipelineCompiler();

        AsyncPipelineCompiler(AsyncPipelineCompiler const&) = delete;
        auto operator=(AsyncPipelineCompiler const&) -> AsyncPipelineCompiler& = delete;
        AsyncPipelineCompiler(AsyncPipelineCompiler&&) noexcept;
        auto operator=(AsyncPipelineCompiler&&) noexcept -> AsyncPipelineCompiler&;

        /** @brief Create the async pipeline compiler.
         *  @param iCompiler    Shared SlangCompiler instance (thread-safe via internal mutex).
         *  @param iThreadCount Number of worker threads (0 = hardware_concurrency).
         */
        [[nodiscard]] static auto Create(SlangCompiler& iCompiler, uint32_t iThreadCount = 0)
            -> core::Result<AsyncPipelineCompiler>;

        /** @brief Submit a shader compilation request (non-blocking).
         *  @param iRequest  Compilation descriptor.
         *  @param iCallback Optional callback on completion (called on worker thread).
         *  @return Handle for polling.
         */
        [[nodiscard]] auto Submit(AsyncPSORequest iRequest, AsyncPSOCallback iCallback = {}) -> AsyncPSOHandle;

        /** @brief Submit multiple compilation requests as a batch.
         *  @return Handles in same order as requests.
         */
        [[nodiscard]] auto SubmitBatch(std::span<const AsyncPSORequest> iRequests, AsyncPSOCallback iCallback = {})
            -> std::vector<AsyncPSOHandle>;

        /** @brief Non-blocking poll: is this compilation done? */
        [[nodiscard]] auto IsComplete(AsyncPSOHandle iHandle) const noexcept -> bool;

        /** @brief Drain all completed results since last call.
         *  Call once per frame from main thread.
         *  @return Completed results (moved out).
         */
        [[nodiscard]] auto DrainCompleted() -> std::vector<AsyncPSOResult>;

        /** @brief Number of compilations currently in-flight. */
        [[nodiscard]] auto PendingCount() const noexcept -> uint32_t;

        /** @brief Block until all pending compilations complete. Use at shutdown. */
        auto WaitAll() -> void;

        /** @brief Shutdown the thread pool. Blocks until all workers finish. */
        auto Shutdown() -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit AsyncPipelineCompiler(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::shader
