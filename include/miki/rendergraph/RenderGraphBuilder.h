/** @file RenderGraphBuilder.h
 *  @brief Declarative render graph construction API.
 *
 *  RenderGraphBuilder is the primary user-facing interface for declaring
 *  passes, resources, and their dependencies. Single-threaded ownership
 *  model — one builder per frame, constructed and consumed on the render thread.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <vector>

#include "miki/core/LinearAllocator.h"
#include "miki/rendergraph/PassBuilder.h"
#include "miki/rendergraph/RenderGraphTypes.h"

namespace miki::rg {

    class RenderGraphCompiler;
    class ComputeSubgraphBuilder;
    struct RGAccelStructDesc;
    struct RGSparseTextureDesc;
    struct RGSparseBufferDesc;
    struct MeshShaderPassConfig;

    /// @brief Declarative render graph builder.
    /// All graph construction happens through this class. After declaring
    /// all passes and resources, call Build() to finalize the description.
    class RenderGraphBuilder {
       public:
        /// @brief Default arena capacity: 64 KiB. Covers ~500 resource accesses.
        static constexpr size_t kDefaultArenaCapacity = 64 * 1024;

        explicit RenderGraphBuilder(size_t arenaCapacity = kDefaultArenaCapacity);
        ~RenderGraphBuilder() = default;

        RenderGraphBuilder(const RenderGraphBuilder&) = delete;
        auto operator=(const RenderGraphBuilder&) -> RenderGraphBuilder& = delete;
        RenderGraphBuilder(RenderGraphBuilder&&) noexcept = default;
        auto operator=(RenderGraphBuilder&&) noexcept -> RenderGraphBuilder& = default;

        // =====================================================================
        // Pass declaration
        // =====================================================================

        /// @brief Add a graphics pass (rasterization pipeline).
        [[nodiscard]] auto AddGraphicsPass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle;

        /// @brief Add a compute pass on the graphics queue.
        [[nodiscard]] auto AddComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle;

        /// @brief Add a compute pass eligible for the async compute queue.
        [[nodiscard]] auto AddAsyncComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute)
            -> RGPassHandle;

        /// @brief Add a transfer-only pass (copy/blit on dedicated transfer queue).
        [[nodiscard]] auto AddTransferPass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle;

        /// @brief Add a present pass (final swapchain output).
        void AddPresentPass(const char* name, RGResourceHandle backbuffer);

        /// @brief Add a mesh/task shader graphics pass (L-7, §16.3).
        /// The compiler uses amplification rate from config to estimate GPU workload.
        [[nodiscard]] auto AddMeshShaderPass(
            const char* name, const MeshShaderPassConfig& config, PassSetupFn setup, PassExecuteFn execute
        ) -> RGPassHandle;

        /// @brief Add a sparse bind pass (L-11, §16.7).
        /// Sparse bind operations (commit/decommit) run on the sparse binding queue.
        [[nodiscard]] auto AddSparseBindPass(const char* name, PassSetupFn setup, PassExecuteFn execute)
            -> RGPassHandle;

        // =====================================================================
        // Resource declaration
        // =====================================================================

        /// @brief Create a transient texture (lifetime managed by the graph).
        [[nodiscard]] auto CreateTexture(const RGTextureDesc& desc) -> RGResourceHandle;

        /// @brief Create a transient buffer (lifetime managed by the graph).
        [[nodiscard]] auto CreateBuffer(const RGBufferDesc& desc) -> RGResourceHandle;

        /// @brief Import an external texture (persistent, not aliased).
        [[nodiscard]] auto ImportTexture(rhi::TextureHandle texture, const char* name = nullptr) -> RGResourceHandle;

        /// @brief Import an external buffer (persistent, not aliased).
        [[nodiscard]] auto ImportBuffer(rhi::BufferHandle buffer, const char* name = nullptr) -> RGResourceHandle;

        /// @brief Import the swapchain backbuffer.
        [[nodiscard]] auto ImportBackbuffer(rhi::TextureHandle backbuffer, const char* name = "Backbuffer")
            -> RGResourceHandle;

        /// @brief Create a graph-managed acceleration structure (L-8, §16.4).
        [[nodiscard]] auto CreateAccelStruct(const RGAccelStructDesc& desc) -> RGResourceHandle;

        /// @brief Declare a sparse texture (L-11, §16.7). Virtual size, physical pages bound dynamically.
        [[nodiscard]] auto DeclareSparseTexture(const RGSparseTextureDesc& desc) -> RGResourceHandle;

        /// @brief Declare a sparse buffer (L-11, §16.7).
        [[nodiscard]] auto DeclareSparseBuffer(const RGSparseBufferDesc& desc) -> RGResourceHandle;

        // =====================================================================
        // Conditional execution
        // =====================================================================

        /// @brief Gate a pass on a runtime condition. Evaluated at Build() time.
        void EnableIf(RGPassHandle pass, ConditionFn condition);

        // =====================================================================
        // Subgraph composition
        // =====================================================================

        /// @brief Insert a subgraph builder's passes and resources into this graph.
        void InsertSubgraph(RenderGraphBuilder&& subgraph);

        /// @brief Insert a compute-only subgraph (L-1, §8.1).
        /// The subgraph's passes are merged into this graph with their queue/affinity hints.
        void InsertComputeSubgraph(ComputeSubgraphBuilder&& subgraph);

        // =====================================================================
        // Build finalization
        // =====================================================================

        /// @brief Finalize the graph description. No further modifications allowed.
        /// The builder is consumed (moved-from) after this call.
        void Build();

        // =====================================================================
        // Internal access (for PassBuilder and Compiler)
        // =====================================================================

        [[nodiscard]] auto GetPasses() const noexcept -> const std::vector<RGPassNode>& { return passes_; }
        [[nodiscard]] auto GetPasses() noexcept -> std::vector<RGPassNode>& { return passes_; }
        [[nodiscard]] auto GetResources() const noexcept -> const std::vector<RGResourceNode>& { return resources_; }
        [[nodiscard]] auto GetResources() noexcept -> std::vector<RGResourceNode>& { return resources_; }
        [[nodiscard]] auto GetPassCount() const noexcept -> uint32_t { return static_cast<uint32_t>(passes_.size()); }
        [[nodiscard]] auto GetResourceCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(resources_.size());
        }
        [[nodiscard]] auto IsBuilt() const noexcept -> bool { return built_; }

        /// @brief Allocate a new resource index and bump its version. Used by PassBuilder.
        [[nodiscard]] auto AllocateResourceVersion(uint16_t resourceIndex) -> RGResourceHandle;

        /// @brief Access staging buffers (used by PassBuilder during setup phase).
        [[nodiscard]] auto GetStagingReads() noexcept -> std::vector<RGResourceAccess>& { return stagingReads_; }
        [[nodiscard]] auto GetStagingWrites() noexcept -> std::vector<RGResourceAccess>& { return stagingWrites_; }

       private:
        auto AddPass(const char* name, RGPassFlags flags, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle;
        auto AllocateResource(RGResourceKind kind, const char* name) -> uint16_t;
        void CommitStagedAccesses(RGPassNode& pass);

        core::LinearAllocator arena_;
        std::vector<RGResourceAccess> stagingReads_;   ///< Temp buffer during pass setup
        std::vector<RGResourceAccess> stagingWrites_;  ///< Temp buffer during pass setup
        std::vector<RGPassNode> passes_;
        std::vector<RGResourceNode> resources_;
        bool built_ = false;
    };

}  // namespace miki::rg
