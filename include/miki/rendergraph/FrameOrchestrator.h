/** @file FrameOrchestrator.h
 *  @brief Multi-graph composition for per-layer render graphs (Phase I, §10.4).
 *
 *  Each LayerStack layer owns its own RenderGraph instance. The FrameOrchestrator
 *  composes them: independent compilation/caching, cross-graph imports, ordered execution.
 *
 *  Layer ordering (from rendering-pipeline-architecture.md §2.1):
 *    1. Scene layer graph       (main 88-pass pipeline)
 *    2. Preview layer graph     (boolean preview, lightweight)
 *    3. Overlay layer graph     (gizmos, grid, viewcube)
 *    4. Widgets layer graph     (UI overlay)
 *    5. SVG layer graph         (2D drawing export)
 *    6. HUD layer graph         (performance overlay)
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "miki/core/Result.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphCompiler.h"
#include "miki/rendergraph/RenderGraphTypes.h"

namespace miki::rg {

    /// @brief Identifies a layer in the composition stack.
    enum class LayerId : uint8_t {
        Scene = 0,
        Preview = 1,
        Overlay = 2,
        Widgets = 3,
        SVG = 4,
        HUD = 5,
        Count
    };

    /// @brief Cross-graph resource export: one layer publishes a resource for others to import.
    struct LayerExport {
        LayerId sourceLayer = LayerId::Scene;
        const char* name = nullptr;    ///< Logical name (e.g., "SceneDepth", "SceneColor")
        RGResourceHandle handle = {};  ///< Handle within the source layer's graph
        RGResourceKind kind = RGResourceKind::Texture;
    };

    /// @brief Per-layer compilation state maintained across frames.
    struct LayerState {
        std::string name;
        bool active = true;             ///< If false, layer is skipped entirely
        CompiledRenderGraph compiled;   ///< Last compiled graph (cached)
        bool hasCompiledGraph = false;  ///< True after first successful compile
    };

    /// @brief Composes multiple per-layer render graphs into a single frame.
    ///
    /// Usage pattern (per frame):
    ///   1. orchestrator.BeginFrame(frameCtx)
    ///   2. For each active layer: orchestrator.SetLayerBuilder(layer, std::move(builder))
    ///   3. orchestrator.CompileAll()
    ///   4. For each layer: orchestrator.GetCompiledGraph(layer) to execute
    class FrameOrchestrator {
       public:
        explicit FrameOrchestrator(const RenderGraphCompiler::Options& compilerOptions = {});

        /// @brief Begin a new frame. Patches external resources on cached graphs.
        void BeginFrame(const FrameContext& frameCtx);

        /// @brief Set or replace the builder for a layer. Layer must call Build() first.
        void SetLayerBuilder(LayerId layer, RenderGraphBuilder builder);

        /// @brief Activate or deactivate a layer (deactivated layers skip compile + execute).
        void SetLayerActive(LayerId layer, bool active);

        /// @brief Export a resource from one layer for import by other layers.
        void ExportResource(LayerId sourceLayer, const char* name, RGResourceHandle handle, RGResourceKind kind);

        /// @brief Import a resource exported by another layer into a builder.
        /// Must be called during the setup phase of the importing layer's builder.
        /// @param targetBuilder The importing layer's builder.
        /// @param sourceLayer Layer that exported the resource.
        /// @param name Logical name of the exported resource.
        /// @return Handle in the target builder's namespace.
        [[nodiscard]] auto ImportFromLayer(RenderGraphBuilder& targetBuilder, LayerId sourceLayer, const char* name)
            -> RGResourceHandle;

        /// @brief Compile all active layers. Uses caching: FullHit -> skip, DescOnly -> incremental.
        /// @return true if all layers compiled successfully.
        [[nodiscard]] auto CompileAll() -> bool;

        /// @brief Get compiled graph for a layer (valid after CompileAll).
        [[nodiscard]] auto GetCompiledGraph(LayerId layer) const -> const CompiledRenderGraph*;

        /// @brief Get cache hit result for a specific layer from last CompileAll.
        [[nodiscard]] auto GetLayerCacheResult(LayerId layer) const -> CacheHitResult;

        /// @brief Get the number of active layers.
        [[nodiscard]] auto GetActiveLayerCount() const noexcept -> uint32_t;

        /// @brief Get all layer exports (for debugging / visualization).
        [[nodiscard]] auto GetExports() const noexcept -> const std::vector<LayerExport>& { return exports_; }

       private:
        static constexpr auto kLayerCount = static_cast<size_t>(LayerId::Count);

        std::array<LayerState, kLayerCount> layers_;
        std::array<RenderGraphCompiler, kLayerCount> compilers_;
        std::array<std::optional<RenderGraphBuilder>, kLayerCount> builders_;

        std::vector<LayerExport> exports_;
        FrameContext currentFrameCtx_;
    };

}  // namespace miki::rg
