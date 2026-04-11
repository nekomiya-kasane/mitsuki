/** @file RenderGraphCompute.h
 *  @brief GPGPU & Heterogeneous Compute support for the miki Render Graph.
 *
 *  Implements spec §8.1-§8.5 (Phase L):
 *    L-1: Compute-only subgraphs (ML inference, physics, mesh processing)
 *    L-2: Multi-frame compute tasks (AsyncTaskManager integration)
 *    L-3: Work graph integration (D3D12 SM 6.8 DispatchGraph + fallback)
 *    L-4: Cooperative matrix / tensor core pass hints
 *    L-5: Heterogeneous device support (RGDeviceAffinity)
 *
 *  Design principles:
 *  - Type-safe compute subgraph builder prevents accidental graphics pass injection
 *  - Multi-frame tasks bridge AsyncTaskManager timeline to graph dependency edges
 *  - Work graph passes are opaque to barrier/sync — standard resource access declarations apply
 *  - Cooperative matrix is a scheduling hint, not a barrier concern
 *  - Device affinity is forward-looking annotation; current impl targets single-GPU multi-queue
 *
 *  Thread model: single-threaded (render thread only).
 *  Namespace: miki::rg
 *
 *  See also:
 *  - specs/04-render-graph.md §8
 *  - specs/03-sync.md §5.6 (AsyncTaskManager)
 *  - D3D12 Work Graphs 1.0 spec (microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html)
 *  - VK_KHR_cooperative_matrix spec
 */
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/Handle.h"

namespace miki::frame {
    struct AsyncTaskHandle;
}

namespace miki::rg {

    class RenderGraphBuilder;
    class PassBuilder;

    // =========================================================================
    // §8.5  Device Affinity — heterogeneous GPU support
    // =========================================================================

    /// @brief Per-pass device affinity hint for multi-GPU / iGPU+dGPU systems.
    /// Current implementation: single-GPU only. Affinity is recorded but not
    /// enforced until multi-device support is implemented (Phase L-5 future).
    /// The compiler uses this for:
    ///   1. Automatic PCIe transfer pass injection for cross-device edges (future)
    ///   2. Queue family selection hints (integrated GPU may have different queue topology)
    ///   3. Memory placement decisions (device-local vs host-visible for iGPU shared memory)
    enum class RGDeviceAffinity : uint8_t {
        Any,            ///< Scheduler picks optimal device (default — respects existing queue logic)
        DiscreteGPU,    ///< Force discrete GPU (high performance, dedicated VRAM)
        IntegratedGPU,  ///< Force integrated GPU (lower power, shared memory with CPU)
        CopyEngine,     ///< DMA-only engine (PCIe copy, no shader execution)
    };

    /// @brief Configuration for a pass with device affinity override.
    struct RGDeviceAffinityConfig {
        RGDeviceAffinity affinity = RGDeviceAffinity::Any;
        uint8_t deviceIndex = 0;  ///< Physical device index (0 = primary, for explicit multi-GPU)
    };

    // =========================================================================
    // §8.4  Cooperative Matrix / Tensor Core — scheduling hints
    // =========================================================================

    /// @brief Per-pass cooperative matrix configuration hint.
    /// Influences the async compute scheduler to prefer the compute queue for
    /// large matrix workloads, since tensor core dispatches benefit from
    /// overlapping with graphics frontend work.
    ///
    /// From the render graph's perspective, cooperative matrix passes are
    /// standard compute passes — no special barrier handling needed.
    /// The shader internally uses VK_KHR_cooperative_matrix / SM 6.9 wave matrix.
    struct CooperativeMatrixConfig {
        bool useCooperativeMatrix = false;  ///< Shader uses cooperative matrix intrinsics
        uint32_t matrixM = 0;               ///< Matrix dimension M (rows of A, rows of C)
        uint32_t matrixN = 0;               ///< Matrix dimension N (cols of B, cols of C)
        uint32_t matrixK = 0;               ///< Matrix dimension K (cols of A, rows of B)
        uint32_t batchCount = 1;            ///< Number of independent matrix multiplies
        bool preferAsyncCompute = true;     ///< Hint: prefer async compute queue for overlap
    };

    /// @brief Estimate total FLOPs for a cooperative matrix workload.
    /// Used by the scheduler to gauge dispatch size for async vs pipelined decision.
    [[nodiscard]] constexpr auto EstimateCoopMatrixFlops(const CooperativeMatrixConfig& cfg) noexcept -> uint64_t {
        return static_cast<uint64_t>(cfg.matrixM) * cfg.matrixN * cfg.matrixK * 2 * cfg.batchCount;
    }

    // =========================================================================
    // §8.3  Work Graph Pass — D3D12 SM 6.8+ DispatchGraph integration
    // =========================================================================

    /// @brief Work graph launch mode.
    /// Determines how the command list initiates work graph execution.
    enum class WorkGraphLaunchMode : uint8_t {
        CPUInput,      ///< CPU-sourced input records (D3D12_DISPATCH_MODE_NODE_CPU_INPUT)
        GPUInput,      ///< GPU buffer-sourced input records (D3D12_DISPATCH_MODE_NODE_GPU_INPUT)
        MultiNodeCPU,  ///< Multiple entry points with CPU input
        MultiNodeGPU,  ///< Multiple entry points with GPU input
    };

    /// @brief Descriptor for a D3D12 work graph pass.
    /// The render graph treats this as an opaque compute pass — all resource
    /// access must be declared via PassBuilder as usual. The internal GPU-driven
    /// scheduling within the work graph is invisible to the render graph.
    ///
    /// On non-D3D12 backends (Vulkan/GL/WebGPU), the fallback function is called
    /// instead, allowing a traditional DispatchIndirect chain to replace the
    /// work graph dispatch.
    struct WorkGraphPassDesc {
        const char* programName = nullptr;  ///< D3D12 work graph program name (from state object)
        WorkGraphLaunchMode launchMode = WorkGraphLaunchMode::CPUInput;

        /// @brief Backing memory size (bytes) for the work graph's intermediate storage.
        /// Queried from ID3D12WorkGraphProperties::GetWorkGraphMemoryRequirements().
        /// 0 = use driver-reported minimum.
        uint64_t backingMemorySize = 0;

        /// @brief Initialization behavior for backing memory.
        /// true = initialize on first use (required for correct execution).
        /// false = skip initialization (subsequent dispatches can reuse state).
        bool initializeBackingMemory = true;

        /// @brief Maximum number of input records for DispatchGraph().
        /// Used for GPU input mode — the GPU buffer contains this many records.
        uint32_t maxInputRecords = 0;

        /// @brief D3D12 state object handle containing the work graph program.
        /// Set by the application; the render graph does not manage state object lifetime.
        rhi::PipelineHandle stateObject;

        /// @brief CPU input record data (for CPUInput/MultiNodeCPU modes).
        /// Points to application-managed memory valid for the duration of the dispatch.
        const void* cpuInputData = nullptr;
        uint32_t cpuInputRecordCount = 0;
        uint32_t cpuInputRecordStride = 0;

        /// @brief GPU input buffer (for GPUInput/MultiNodeGPU modes).
        rhi::BufferHandle gpuInputBuffer;
        uint64_t gpuInputBufferOffset = 0;
    };

    /// @brief Work graph fallback function type.
    /// Called on backends that don't support D3D12 work graphs.
    /// Receives the RenderPassContext for issuing traditional dispatch commands.
    using WorkGraphFallbackFn = std::function<void(struct RenderPassContext&)>;

    // =========================================================================
    // §8.2  Multi-Frame Task Integration
    // =========================================================================

    /// @brief Async task dependency for render graph passes.
    /// When a pass depends on a long-running async task (BLAS rebuild, GDeflate decode),
    /// the compiler translates this into a timeline semaphore wait injected at the
    /// pass's submission point.
    ///
    /// The task handle comes from AsyncTaskManager::Submit().
    /// If the task is already complete by execution time, the wait is a no-op.
    /// If not, only the waiting pass (and its dependents) stall — all independent
    /// passes continue executing.
    struct AsyncTaskDependency {
        uint64_t taskHandle = 0;     ///< From AsyncTaskManager::Submit(), 0 = invalid
        uint64_t timelineValue = 0;  ///< Timeline semaphore value to wait on (resolved at compile time)
        RGQueueType taskQueue = RGQueueType::AsyncCompute;  ///< Queue where the task runs
    };

    /// @brief Resolution status for an async task dependency.
    enum class AsyncTaskStatus : uint8_t {
        NotResolved,  ///< Dependency not yet checked
        Ready,        ///< Task is complete; wait is elided
        Pending,      ///< Task is in-flight; wait will be injected
        Failed,       ///< Task failed; pass should handle gracefully
    };

    /// @brief Resolved async task wait info — output of dependency resolution.
    struct ResolvedAsyncWait {
        uint32_t passIndex = 0;   ///< Pass that declared the wait
        uint64_t taskHandle = 0;  ///< Original task handle
        AsyncTaskStatus status = AsyncTaskStatus::NotResolved;
        uint64_t waitTimelineValue = 0;  ///< Timeline value to inject as semaphore wait
        RGQueueType waitOnQueue = RGQueueType::AsyncCompute;
    };

    // =========================================================================
    // §8.1  Compute-Only Subgraph Builder
    // =========================================================================

    /// @brief Compute subgraph classification.
    /// Affects scheduling strategy and debug visualization.
    enum class ComputeSubgraphType : uint8_t {
        Generic,            ///< Unclassified compute workload
        MLInference,        ///< Neural network inference (denoiser, super-resolution)
        PhysicsSimulation,  ///< Particle systems, cloth, fluid dynamics
        MeshProcessing,     ///< GPU QEM simplification, meshlet generation, LOD compute
        ScientificCompute,  ///< CAE solver preprocessing, matrix operations
        ImageProcessing,    ///< Post-process compute (bloom, blur, FFT)
    };

    /// @brief Configuration for a compute-only subgraph.
    struct ComputeSubgraphConfig {
        const char* name = nullptr;  ///< Debug name for the subgraph
        ComputeSubgraphType type = ComputeSubgraphType::Generic;

        /// @brief Preferred queue for all passes in this subgraph.
        /// AsyncCompute = try async compute queue (default for large workloads).
        /// Graphics = force graphics queue (for small dispatches, pipelined compute).
        RGQueueType preferredQueue = RGQueueType::AsyncCompute;

        /// @brief Device affinity for the entire subgraph.
        RGDeviceAffinityConfig deviceAffinity;

        /// @brief If true, the subgraph is eligible for cross-frame spanning
        /// (individual passes can be split across frames via AsyncTaskManager).
        bool allowMultiFrame = false;

        /// @brief If true, cooperative matrix hints are propagated to all compute
        /// passes within the subgraph.
        CooperativeMatrixConfig coopMatrixConfig;
    };

    /// @brief Type-safe compute-only subgraph builder.
    ///
    /// Wraps RenderGraphBuilder but restricts to compute and transfer passes only.
    /// Graphics passes (rasterization) are compile-time prevented.
    ///
    /// Usage:
    /// @code
    ///     ComputeSubgraphBuilder csb({"MLDenoise", ComputeSubgraphType::MLInference});
    ///     auto input = csb.ImportBuffer(ml.inputGpu, "MLInput");
    ///     auto output = csb.CreateBuffer({.size = ml.outputSize, .debugName = "MLOutput"});
    ///     csb.AddComputePass("Denoise", setupFn, executeFn);
    ///     builder.InsertComputeSubgraph(std::move(csb));
    /// @endcode
    class ComputeSubgraphBuilder {
       public:
        explicit ComputeSubgraphBuilder(const ComputeSubgraphConfig& config);
        ~ComputeSubgraphBuilder();

        ComputeSubgraphBuilder(const ComputeSubgraphBuilder&) = delete;
        auto operator=(const ComputeSubgraphBuilder&) -> ComputeSubgraphBuilder& = delete;
        ComputeSubgraphBuilder(ComputeSubgraphBuilder&&) noexcept;
        auto operator=(ComputeSubgraphBuilder&&) noexcept -> ComputeSubgraphBuilder&;

        // -- Compute pass declaration (async compute queue eligible) --

        [[nodiscard]] auto AddComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle;
        [[nodiscard]] auto AddAsyncComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute)
            -> RGPassHandle;

        // -- Transfer pass declaration --

        [[nodiscard]] auto AddTransferPass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle;

        // -- Resource declaration --

        [[nodiscard]] auto CreateBuffer(const RGBufferDesc& desc) -> RGResourceHandle;
        [[nodiscard]] auto CreateTexture(const RGTextureDesc& desc) -> RGResourceHandle;
        [[nodiscard]] auto ImportBuffer(rhi::BufferHandle buffer, const char* name = nullptr) -> RGResourceHandle;
        [[nodiscard]] auto ImportTexture(rhi::TextureHandle texture, const char* name = nullptr) -> RGResourceHandle;

        // -- Work graph pass (D3D12) --

        [[nodiscard]] auto AddWorkGraphPass(
            const char* name, const WorkGraphPassDesc& desc, PassSetupFn setup, PassExecuteFn execute,
            WorkGraphFallbackFn fallback = nullptr
        ) -> RGPassHandle;

        // -- Async task dependency --

        void WaitForAsyncTask(RGPassHandle pass, uint64_t taskHandle);

        // -- Configuration --

        [[nodiscard]] auto GetConfig() const noexcept -> const ComputeSubgraphConfig& { return config_; }
        [[nodiscard]] auto GetPassCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetResourceCount() const noexcept -> uint32_t;

        // -- Internal: transfer ownership to parent graph --

        [[nodiscard]] auto TakeBuilder() -> RenderGraphBuilder&;
        [[nodiscard]] auto GetBuilder() const noexcept -> const RenderGraphBuilder&;

       private:
        ComputeSubgraphConfig config_;
        std::unique_ptr<RenderGraphBuilder> builder_;
        std::vector<WorkGraphPassDesc> workGraphDescs_;
        std::vector<WorkGraphFallbackFn> workGraphFallbacks_;
        std::vector<AsyncTaskDependency> asyncDependencies_;
    };

    // =========================================================================
    // Multi-Frame Task Resolver
    // =========================================================================

    /// @brief Callback to query async task completion status.
    /// Provided by the AsyncTaskManager bridge at compile time.
    using AsyncTaskQueryFn = std::function<AsyncTaskStatus(uint64_t taskHandle)>;

    /// @brief Callback to resolve timeline value for an async task.
    using AsyncTaskTimelineFn = std::function<uint64_t(uint64_t taskHandle)>;

    /// @brief Resolves async task dependencies declared via WaitForAsyncTask()
    /// into concrete timeline semaphore waits for the executor.
    ///
    /// The resolver is invoked during compilation (between DAG construction and
    /// batch formation). For each pass with async task dependencies:
    ///   1. Query task status (Ready / Pending / Failed)
    ///   2. If Ready: elide the wait (no sync point emitted)
    ///   3. If Pending: inject a timeline semaphore WaitEntry into the pass's batch
    ///   4. If Failed: mark the pass for graceful degradation (optional skip or stall)
    ///
    /// This ensures that independent passes in the graph are never blocked by
    /// unrelated async tasks — only the declaring pass and its dependents stall.
    class MultiFrameTaskResolver {
       public:
        /// @brief Set the query function for async task status.
        void SetTaskQuery(AsyncTaskQueryFn query) { taskQuery_ = std::move(query); }

        /// @brief Set the timeline value resolution function.
        void SetTimelineResolver(AsyncTaskTimelineFn resolver) { timelineResolver_ = std::move(resolver); }

        /// @brief Resolve all async task dependencies in a compiled graph.
        /// @param passes    Compiled pass info (may be modified to inject waits).
        /// @param dependencies  Async task dependencies declared during graph build.
        /// @return Resolved wait info for each dependency.
        [[nodiscard]] auto ResolveAll(std::span<const AsyncTaskDependency> dependencies) const
            -> std::vector<ResolvedAsyncWait>;

        /// @brief Get statistics from the last resolution.
        struct Stats {
            uint32_t totalDependencies = 0;
            uint32_t readyElided = 0;      ///< Waits elided because task was already complete
            uint32_t pendingInjected = 0;  ///< Timeline waits injected
            uint32_t failedPasses = 0;     ///< Tasks that failed
        };
        [[nodiscard]] auto GetStats() const noexcept -> const Stats& { return stats_; }

        /// @brief Format a human-readable status string.
        [[nodiscard]] auto FormatStatus() const -> std::string;

       private:
        AsyncTaskQueryFn taskQuery_;
        AsyncTaskTimelineFn timelineResolver_;
        mutable Stats stats_{};
    };

    // =========================================================================
    // Device Affinity Resolver
    // =========================================================================

    /// @brief Per-device info for heterogeneous device configurations.
    struct DeviceInfo {
        uint8_t deviceIndex = 0;
        RGDeviceAffinity type = RGDeviceAffinity::Any;
        bool hasAsyncCompute = false;
        bool hasAsyncTransfer = false;
        uint64_t deviceLocalMemoryBytes = 0;
        bool isIntegrated = false;
        uint32_t vendorId = 0;
    };

    /// @brief Cross-device transfer edge injected by the affinity resolver.
    /// When a pass on Device A produces a resource consumed by a pass on Device B,
    /// a PCIe transfer pass is automatically injected between them.
    struct CrossDeviceTransferEdge {
        uint32_t srcPassIndex = RGPassHandle::kInvalid;
        uint32_t dstPassIndex = RGPassHandle::kInvalid;
        uint16_t resourceIndex = 0;
        uint8_t srcDevice = 0;
        uint8_t dstDevice = 0;
        uint64_t estimatedTransferBytes = 0;  ///< For PCIe bandwidth budgeting
    };

    /// @brief Resolves per-pass device affinity hints into concrete device assignments.
    ///
    /// Current implementation (single-GPU):
    ///   - All passes map to device 0
    ///   - Affinity hints are recorded for diagnostics but not enforced
    ///   - No cross-device transfers are injected
    ///
    /// Future multi-GPU implementation will:
    ///   1. Partition passes by affinity hint
    ///   2. Inject PCIe transfer passes for cross-device resource dependencies
    ///   3. Generate per-device command batches
    ///   4. Coordinate cross-device timeline semaphores
    class DeviceAffinityResolver {
       public:
        /// @brief Register available devices. Called once at initialization.
        void SetDevices(std::span<const DeviceInfo> devices);

        /// @brief Set the primary (default) device index.
        void SetPrimaryDevice(uint8_t index) noexcept { primaryDevice_ = index; }

        /// @brief Resolve device assignments for all passes.
        /// @param affinityHints  Per-pass affinity configs (indexed by pass index).
        /// @param edges          Graph dependency edges for cross-device detection.
        /// @return Per-pass device assignment (device index for each pass).
        [[nodiscard]] auto ResolveAll(
            std::span<const RGDeviceAffinityConfig> affinityHints, std::span<const DependencyEdge> edges
        ) const -> std::vector<uint8_t>;

        /// @brief Detect cross-device resource transfers needed.
        /// @param deviceAssignments  Per-pass device assignments from ResolveAll().
        /// @param edges              Graph dependency edges.
        /// @return List of cross-device transfer edges that need injection.
        [[nodiscard]] auto DetectCrossDeviceTransfers(
            std::span<const uint8_t> deviceAssignments, std::span<const DependencyEdge> edges
        ) const -> std::vector<CrossDeviceTransferEdge>;

        /// @brief Get the number of registered devices.
        [[nodiscard]] auto GetDeviceCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(devices_.size());
        }

        /// @brief Check if multi-device mode is active.
        [[nodiscard]] auto IsMultiDevice() const noexcept -> bool { return devices_.size() > 1; }

        struct Stats {
            uint32_t totalPasses = 0;
            uint32_t passesOnPrimary = 0;
            uint32_t passesOnSecondary = 0;
            uint32_t crossDeviceTransfers = 0;
            uint64_t estimatedTransferBytes = 0;
        };
        [[nodiscard]] auto GetStats() const noexcept -> const Stats& { return stats_; }

        [[nodiscard]] auto FormatStatus() const -> std::string;

       private:
        std::vector<DeviceInfo> devices_;
        uint8_t primaryDevice_ = 0;
        mutable Stats stats_{};
    };

    // =========================================================================
    // Compute Pass Annotation — extends PassBuilder with L-1..L-5 metadata
    // =========================================================================

    /// @brief Extended per-pass metadata for GPGPU features.
    /// Stored alongside RGPassNode and used by the compiler for scheduling decisions.
    struct ComputePassMetadata {
        // L-4: Cooperative matrix hint
        CooperativeMatrixConfig coopMatrix;

        // L-5: Device affinity
        RGDeviceAffinityConfig deviceAffinity;

        // L-3: Work graph descriptor (nullptr if not a work graph pass)
        const WorkGraphPassDesc* workGraphDesc = nullptr;
        WorkGraphFallbackFn workGraphFallback;

        // L-2: Async task dependencies for this pass
        std::vector<AsyncTaskDependency> asyncTaskDeps;

        // L-1: Subgraph classification (if pass was part of a compute subgraph)
        ComputeSubgraphType subgraphType = ComputeSubgraphType::Generic;
    };

}  // namespace miki::rg
