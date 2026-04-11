/** @file RenderGraphAdvanced.h
 *  @brief Advanced render graph features (Phase L-6 through L-12).
 *
 *  Implements spec §16.1-§16.8:
 *    L-6:  Automatic async compute discovery (critical path analysis)
 *    L-7:  Mesh/task shader graph nodes
 *    L-8:  Ray tracing pass integration (BLAS/TLAS as graph resources)
 *    L-9:  VRS image as graph resource
 *    L-10: GPU breadcrumbs for crash diagnosis
 *    L-11: Sparse resource graph nodes (VSM page commit/decommit)
 *    L-12: D3D12 Fence Barriers Tier-2 integration
 *
 *  Thread model: single-threaded (render thread only) unless noted.
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/AccelerationStructure.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/Handle.h"

namespace miki::rg {

    class RenderGraphBuilder;

    // =========================================================================
    // L-6: Automatic Async Compute Discovery (§16.1)
    // =========================================================================

    /// @brief Per-pass info used during critical path analysis.
    struct CriticalPathPassInfo {
        uint32_t passIndex = 0;
        float estimatedGpuTimeUs = 0.0f;
        RGQueueType currentQueue = RGQueueType::Graphics;
        RGPassFlags flags = RGPassFlags::None;
        float earliestStart = 0.0f;  ///< ASAP schedule time (forward pass)
        float latestStart = 0.0f;    ///< ALAP schedule time (backward pass)
        float slack = 0.0f;          ///< latestStart - earliestStart (0 = on critical path)
        bool onCriticalPath = false;
    };

    /// @brief Candidate for automatic async compute promotion.
    struct AsyncPromotionCandidate {
        uint32_t passIndex = 0;
        float estimatedGpuTimeUs = 0.0f;
        float slack = 0.0f;                    ///< How far off the critical path
        float estimatedOverlapBenefit = 0.0f;  ///< Time saved by async overlap
        float estimatedSyncCost = 0.0f;        ///< Cross-queue sync overhead
        bool profitable = false;               ///< overlapBenefit > syncCost
    };

    /// @brief Result of async compute discovery.
    struct AsyncDiscoveryResult {
        std::vector<CriticalPathPassInfo> passInfo;
        std::vector<AsyncPromotionCandidate> candidates;
        float criticalPathLengthUs = 0.0f;
        float estimatedFrameTimeUs = 0.0f;
        float estimatedSavingsUs = 0.0f;
        uint32_t promotedCount = 0;
    };

    /// @brief Configuration for async compute auto-discovery.
    struct AsyncDiscoveryConfig {
        float minPassDurationUs = 50.0f;     ///< Minimum pass GPU time to consider for promotion
        float syncOverheadUs = 10.0f;        ///< Estimated cross-queue sync cost
        float minOverlapRatio = 0.3f;        ///< Minimum overlap/passDuration ratio to be profitable
        bool respectUserQueueHints = true;   ///< Never demote passes explicitly marked async
        uint32_t maxPromotionsPerFrame = 8;  ///< Limit promotions to avoid thrashing
    };

    /// @brief Discovers compute passes that can be auto-promoted to async compute.
    ///
    /// Algorithm:
    ///   1. Forward pass: compute earliest start time (ASAP) for each pass
    ///   2. Backward pass: compute latest start time (ALAP) for each pass
    ///   3. Slack = ALAP - ASAP; zero-slack passes form the critical path
    ///   4. For each off-critical-path compute pass:
    ///      a. Estimate overlap benefit = min(slack, passDuration)
    ///      b. Estimate sync cost = syncOverheadUs (per cross-queue edge)
    ///      c. If benefit > cost: mark as candidate
    ///
    /// This replaces manual AddAsyncComputePass vs AddComputePass for most passes.
    /// The async compute scheduler (Phase G) still handles runtime queue assignment.
    class AsyncComputeDiscovery {
       public:
        explicit AsyncComputeDiscovery(const AsyncDiscoveryConfig& config = {});

        /// @brief Analyze the compiled DAG and discover async compute candidates.
        /// @param passes     Pass nodes from the builder.
        /// @param edges      Dependency edges from the compiler.
        /// @param passCount  Total number of passes.
        /// @return Discovery result with candidates and statistics.
        [[nodiscard]] auto Analyze(
            std::span<const RGPassNode> passes, std::span<const DependencyEdge> edges, uint32_t passCount
        ) const -> AsyncDiscoveryResult;

        /// @brief Apply discovered promotions to pass queue assignments.
        /// Modifies passes in-place: promotes profitable candidates to AsyncCompute queue.
        /// @return Number of passes promoted.
        auto ApplyPromotions(std::span<RGPassNode> passes, const AsyncDiscoveryResult& result) const -> uint32_t;

        [[nodiscard]] auto GetConfig() const noexcept -> const AsyncDiscoveryConfig& { return config_; }

        [[nodiscard]] auto FormatStatus(const AsyncDiscoveryResult& result) const -> std::string;

       private:
        AsyncDiscoveryConfig config_;
    };

    // =========================================================================
    // L-7: Mesh/Task Shader Graph Nodes (§16.3)
    // =========================================================================

    /// @brief Configuration for a mesh shader pass.
    /// The compiler uses amplification rate to estimate actual GPU workload.
    struct MeshShaderPassConfig {
        uint32_t taskGroupCountX = 1;  ///< Initial task shader dispatch X
        uint32_t taskGroupCountY = 1;  ///< Initial task shader dispatch Y
        uint32_t taskGroupCountZ = 1;  ///< Initial task shader dispatch Z

        /// @brief Estimated amplification rate: meshlets emitted per task shader workgroup.
        /// Used by the scheduler for GPU time estimation.
        /// Typical values: 32-128 for culling passes, 1 for non-amplified mesh passes.
        float amplificationRate = 32.0f;

        /// @brief Estimated vertices per meshlet (for GPU time estimation).
        uint32_t verticesPerMeshlet = 64;

        /// @brief Estimated triangles per meshlet.
        uint32_t trianglesPerMeshlet = 124;

        /// @brief Whether this is an indirect dispatch (DrawMeshTasksIndirectCount).
        bool isIndirect = false;

        /// @brief Buffer containing indirect dispatch arguments (if isIndirect).
        rhi::BufferHandle indirectBuffer;
        uint64_t indirectBufferOffset = 0;

        /// @brief Buffer containing the dispatch count (for IndirectCount).
        rhi::BufferHandle countBuffer;
        uint64_t countBufferOffset = 0;
        uint32_t maxDispatchCount = 0;
    };

    // =========================================================================
    // L-8: Ray Tracing Pass Integration (§16.4)
    // =========================================================================

    /// @brief Acceleration structure type for graph-level resources.
    enum class RGAccelStructType : uint8_t {
        BottomLevel,  ///< BLAS — per-mesh geometry
        TopLevel,     ///< TLAS — scene-level instance references
    };

    /// @brief Graph-level acceleration structure descriptor.
    struct RGAccelStructDesc {
        RGAccelStructType type = RGAccelStructType::BottomLevel;
        const char* debugName = nullptr;

        /// @brief For BLAS: geometry descriptions.
        uint32_t geometryCount = 0;

        /// @brief Build flags (prefer fast trace vs fast build).
        rhi::AccelStructBuildFlags buildFlags = rhi::AccelStructBuildFlags::PreferFastTrace;

        /// @brief If true, the AS supports incremental refit (update without full rebuild).
        bool allowUpdate = false;

        /// @brief Estimated sizes (pre-queried via device). 0 = auto-query at build time.
        uint64_t estimatedSize = 0;
        uint64_t estimatedScratchSize = 0;
    };

    // =========================================================================
    // L-9: VRS Image Integration (§16.5)
    // =========================================================================

    /// @brief Configuration for VRS image generation passes.
    struct VrsImageConfig {
        /// @brief Scale factor relative to render resolution (typically 1/8 or 1/16).
        float widthScale = 1.0f / 16.0f;
        float heightScale = 1.0f / 16.0f;

        /// @brief VRS tile size in pixels (hardware-dependent: 8, 16, or 32).
        uint32_t tileSize = 16;

        /// @brief Whether to combine per-draw and per-primitive VRS with image-based VRS.
        /// D3D12: ShadingRateCombiner; Vulkan: VK_KHR_fragment_shading_rate combiners.
        bool combineWithPerDraw = false;
        bool combineWithPerPrimitive = false;
    };

    // =========================================================================
    // L-10: GPU Breadcrumbs for Crash Diagnosis (§16.6)
    // =========================================================================

    /// @brief Single breadcrumb marker written by the GPU before/after each pass.
    struct GpuBreadcrumb {
        uint32_t passIndex;  ///< Which pass this marker belongs to
        uint32_t marker;     ///< Incremented: even = before pass, odd = after pass
    };

    /// @brief Breadcrumb tracking mode.
    enum class BreadcrumbMode : uint8_t {
        Disabled,     ///< No breadcrumbs (zero overhead)
        PerPass,      ///< One marker before each pass (minimal overhead)
        PerPassFull,  ///< Marker before and after each pass (slightly more overhead)
        PerDraw,      ///< Marker before each draw/dispatch (highest overhead, finest granularity)
    };

    /// @brief Configuration for GPU breadcrumb tracking.
    struct BreadcrumbConfig {
        BreadcrumbMode mode = BreadcrumbMode::Disabled;

        /// @brief Maximum number of breadcrumb entries in the ring buffer.
        /// Must be power of 2. Typical: 4096 (covers ~46 frames of 88 passes each).
        uint32_t maxEntries = 4096;

        /// @brief Whether to include pass names (requires persistent string storage).
        bool includePassNames = false;

        /// @brief Callback invoked on crash recovery with breadcrumb data.
        /// Receives the breadcrumb buffer contents at crash time.
        std::function<void(std::span<const GpuBreadcrumb>)> onCrashCallback;
    };

    /// @brief GPU breadcrumb tracker — writes markers to persistent-mapped readback buffer.
    ///
    /// Usage flow:
    ///   1. Create tracker with BreadcrumbConfig at device init
    ///   2. Each frame: BeginFrame() resets write pointer
    ///   3. Before each pass: WriteBreadcrumb(passIndex)
    ///   4. On device removal / TDR: ReadCrashData() returns last written markers
    ///
    /// Backend support:
    ///   - Vulkan: VK_AMD_buffer_marker (CmdWriteBufferMarkerAMD) or UAV write fallback
    ///   - D3D12: DRED auto-breadcrumbs + custom WriteBufferImmediate markers
    ///   - Other: UAV atomic increment (higher overhead)
    ///
    /// Thread safety: single-threaded (command recording thread).
    class GpuBreadcrumbTracker {
       public:
        explicit GpuBreadcrumbTracker(const BreadcrumbConfig& config = {});
        ~GpuBreadcrumbTracker();

        GpuBreadcrumbTracker(const GpuBreadcrumbTracker&) = delete;
        auto operator=(const GpuBreadcrumbTracker&) -> GpuBreadcrumbTracker& = delete;
        GpuBreadcrumbTracker(GpuBreadcrumbTracker&&) noexcept;
        auto operator=(GpuBreadcrumbTracker&&) noexcept -> GpuBreadcrumbTracker&;

        /// @brief Initialize the persistent-mapped readback buffer.
        /// Must be called once after device creation. Pass the allocated readback buffer.
        auto Initialize(rhi::BufferHandle readbackBuffer) -> bool;

        /// @brief Reset the write pointer for a new frame.
        void BeginFrame(uint64_t frameIndex);

        /// @brief Write a breadcrumb marker at the current position.
        /// The GPU writes this value into the persistent-mapped buffer.
        /// @param passIndex  Index of the pass being executed.
        /// @param isAfter    false = before pass, true = after pass.
        /// @return GPU virtual address and offset for the marker write.
        auto WriteBreadcrumb(uint32_t passIndex, bool isAfter = false) -> uint64_t;

        /// @brief Read crash data from the breadcrumb buffer after device removal.
        /// The persistent mapping survives device removal on most implementations.
        /// @return Vector of breadcrumb entries up to the last GPU-written marker.
        [[nodiscard]] auto ReadCrashData() const -> std::vector<GpuBreadcrumb>;

        /// @brief Get the last known good pass (highest completed marker).
        [[nodiscard]] auto GetLastGoodPass() const -> uint32_t;

        /// @brief Get the suspected crash pass (highest incomplete marker).
        [[nodiscard]] auto GetSuspectedCrashPass() const -> uint32_t;

        /// @brief Format a human-readable crash report.
        /// Includes pass names if includePassNames was enabled.
        [[nodiscard]] auto FormatCrashReport(std::span<const char* const> passNames = {}) const -> std::string;

        /// @brief Check if breadcrumbs are active (not Disabled mode).
        [[nodiscard]] auto IsActive() const noexcept -> bool { return config_.mode != BreadcrumbMode::Disabled; }

        /// @brief Get the breadcrumb buffer GPU handle (for command buffer marker writes).
        [[nodiscard]] auto GetBufferHandle() const noexcept -> rhi::BufferHandle { return buffer_; }

        struct Stats {
            uint64_t totalMarkersWritten = 0;
            uint64_t framesTracked = 0;
            uint32_t crashesDetected = 0;
        };
        [[nodiscard]] auto GetStats() const noexcept -> const Stats& { return stats_; }

       private:
        BreadcrumbConfig config_;
        rhi::BufferHandle buffer_;
        uint32_t writeOffset_ = 0;
        uint64_t currentFrame_ = 0;
        Stats stats_{};
    };

    // =========================================================================
    // L-11: Sparse Resource Graph Nodes (§16.7)
    // =========================================================================

    /// @brief Sparse resource commit/decommit operation type.
    enum class SparseOpType : uint8_t {
        Commit,    ///< Bind physical memory to virtual pages
        Decommit,  ///< Unbind physical memory from virtual pages
    };

    /// @brief A single sparse binding operation (page commit or decommit).
    struct SparseBindOp {
        SparseOpType type = SparseOpType::Commit;
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
        uint32_t offsetX = 0;  ///< Tile offset in X (sparse block granularity)
        uint32_t offsetY = 0;
        uint32_t offsetZ = 0;
        uint32_t extentX = 1;  ///< Tile count in X
        uint32_t extentY = 1;
        uint32_t extentZ = 1;
    };

    /// @brief Graph-level sparse texture descriptor.
    struct RGSparseTextureDesc {
        rhi::TextureDimension dimension = rhi::TextureDimension::Tex2D;
        rhi::Format format = rhi::Format::D32_FLOAT;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        const char* debugName = nullptr;

        /// @brief Sparse block dimensions (queried from device). 0 = auto-detect.
        uint32_t sparseBlockWidth = 0;
        uint32_t sparseBlockHeight = 0;
        uint32_t sparseBlockDepth = 1;
    };

    /// @brief Sparse buffer descriptor for graph-declared sparse buffers.
    struct RGSparseBufferDesc {
        uint64_t size = 0;          ///< Virtual size in bytes
        uint64_t pageSize = 65536;  ///< Physical page size (typically 64KB)
        const char* debugName = nullptr;
    };

    // =========================================================================
    // L-12: D3D12 Fence Barriers Tier-2 Integration (§16.8)
    // =========================================================================

    /// @brief Fence barrier sync point — replaces queue-level fence for cross-queue sync.
    /// Only active when D3D12 Fence Barrier Tier-2 is available.
    struct FenceBarrierSyncPoint {
        uint32_t srcPassIndex = RGPassHandle::kInvalid;  ///< Pass that signals
        uint32_t dstPassIndex = RGPassHandle::kInvalid;  ///< Pass that waits
        uint64_t fenceValue = 0;                         ///< Fence value to signal/wait on
        RGQueueType srcQueue = RGQueueType::Graphics;
        RGQueueType dstQueue = RGQueueType::AsyncCompute;
    };

    /// @brief Configuration for Fence Barrier Tier-2 emission.
    struct FenceBarrierTier2Config {
        /// @brief Enable command-buffer-level cross-queue signal/wait.
        /// When true and hardware supports Tier-2, the compiler emits
        /// SignalBarrier/WaitBarrier instead of ID3D12CommandQueue::Signal/Wait.
        bool enableTier2 = false;

        /// @brief Detected fence barrier tier from device capabilities.
        rhi::GpuCapabilityProfile::FenceBarrierTier tier = rhi::GpuCapabilityProfile::FenceBarrierTier::None;

        /// @brief If true, allow mid-command-list sync points (reduces submission splits).
        bool allowSubBatchSync = false;
    };

    /// @brief Resolves cross-queue sync points using Fence Barriers Tier-2.
    ///
    /// When Tier-2 is available:
    ///   - Cross-queue sync uses SignalBarrier/WaitBarrier within command lists
    ///   - No need to split command lists at sync boundaries
    ///   - Finer overlap between queues (sub-batch granularity)
    ///
    /// When Tier-2 is NOT available (fallback):
    ///   - Standard ID3D12CommandQueue::Signal/Wait or Vulkan timeline semaphore
    ///   - Command lists split at sync boundaries (current behavior)
    class FenceBarrierResolver {
       public:
        explicit FenceBarrierResolver(const FenceBarrierTier2Config& config = {});

        /// @brief Check if Tier-2 fence barriers are active.
        [[nodiscard]] auto IsTier2Active() const noexcept -> bool {
            return config_.enableTier2 && config_.tier >= rhi::GpuCapabilityProfile::FenceBarrierTier::Tier2;
        }

        /// @brief Convert cross-queue sync points to fence barrier pairs.
        /// @param syncPoints  Standard cross-queue sync points from Stage 5.
        /// @return Fence barrier sync points (or empty if Tier-2 not available).
        [[nodiscard]] auto ResolveSyncPoints(std::span<const CrossQueueSyncPoint> syncPoints) const
            -> std::vector<FenceBarrierSyncPoint>;

        /// @brief Estimate how many command list splits can be eliminated by Tier-2.
        [[nodiscard]] auto EstimateSplitReduction(std::span<const CrossQueueSyncPoint> syncPoints) const -> uint32_t;

        struct Stats {
            uint32_t totalSyncPoints = 0;
            uint32_t convertedToFenceBarrier = 0;
            uint32_t remainingQueueSync = 0;
            uint32_t splitsSaved = 0;
        };
        [[nodiscard]] auto GetStats() const noexcept -> const Stats& { return stats_; }

        [[nodiscard]] auto FormatStatus() const -> std::string;

       private:
        FenceBarrierTier2Config config_;
        mutable Stats stats_{};
    };

}  // namespace miki::rg
