/** @file LayerCompositor.cpp
 *  @brief Multi-graph composition for per-layer render graphs (Phase I, §10.4).
 */

#include "miki/rendergraph/LayerCompositor.h"

#include <cassert>
#include <string_view>

namespace miki::rg {

    static constexpr const char* kLayerNames[] = {"Scene", "Preview", "Overlay", "Widgets", "SVG", "HUD"};

    LayerCompositor::LayerCompositor(const RenderGraphCompiler::Options& compilerOptions) {
        for (size_t i = 0; i < kLayerCount; ++i) {
            layers_[i].name = kLayerNames[i];
            compilers_[i] = RenderGraphCompiler(compilerOptions);
        }
    }

    void LayerCompositor::BeginFrame(const FrameContext& frameCtx) {
        currentFrameCtx_ = frameCtx;
        exports_.clear();

        // Patch external resources on cached graphs (O(E) per layer, runs even on cache hit)
        for (size_t i = 0; i < kLayerCount; ++i) {
            if (layers_[i].active && layers_[i].hasCompiledGraph) {
                PatchExternalResources(layers_[i].compiled, frameCtx);
            }
        }
    }

    void LayerCompositor::SetLayerBuilder(LayerId layer, RenderGraphBuilder builder) {
        auto idx = static_cast<size_t>(layer);
        assert(idx < kLayerCount);
        assert(builder.IsBuilt() && "Builder must be finalized via Build() before setting on orchestrator");
        builders_[idx] = std::move(builder);
    }

    void LayerCompositor::SetLayerActive(LayerId layer, bool active) {
        auto idx = static_cast<size_t>(layer);
        assert(idx < kLayerCount);
        layers_[idx].active = active;
    }

    void LayerCompositor::ExportResource(
        LayerId sourceLayer, const char* name, RGResourceHandle handle, RGResourceKind kind
    ) {
        exports_.push_back({.sourceLayer = sourceLayer, .name = name, .handle = handle, .kind = kind});
    }

    auto LayerCompositor::ImportFromLayer(RenderGraphBuilder& targetBuilder, LayerId sourceLayer, const char* name)
        -> RGResourceHandle {
        // Find the export
        for (auto& exp : exports_) {
            if (exp.sourceLayer == sourceLayer && std::string_view(exp.name) == name) {
                // Get the physical handle from the source layer's compiled graph
                auto srcIdx = static_cast<size_t>(sourceLayer);
                if (layers_[srcIdx].hasCompiledGraph) {
                    for (auto& slot : layers_[srcIdx].compiled.externalResources) {
                        if (slot.resourceIndex == exp.handle.GetIndex()) {
                            if (exp.kind == RGResourceKind::Texture) {
                                return targetBuilder.ImportTexture(slot.physicalTexture, name);
                            }
                            return targetBuilder.ImportBuffer(slot.physicalBuffer, name);
                        }
                    }
                }
                // Source not yet compiled — import with placeholder (will be patched later)
                if (exp.kind == RGResourceKind::Texture) {
                    return targetBuilder.ImportTexture({}, name);
                }
                return targetBuilder.ImportBuffer({}, name);
            }
        }
        // Export not found — create placeholder
        return targetBuilder.ImportTexture({}, name);
    }

    auto LayerCompositor::CompileAll() -> bool {
        bool allOk = true;
        for (size_t i = 0; i < kLayerCount; ++i) {
            if (!layers_[i].active || !builders_[i].has_value()) {
                continue;
            }
            auto& builder = *builders_[i];
            auto& state = layers_[i];
            auto& compiler = compilers_[i];

            if (state.hasCompiledGraph) {
                // Try cache classification
                std::vector<bool> activeSet(builder.GetPasses().size(), true);
                for (uint32_t p = 0; p < builder.GetPasses().size(); ++p) {
                    auto& pass = builder.GetPasses()[p];
                    if (pass.conditionFn) {
                        activeSet[p] = pass.conditionFn();
                    }
                }
                auto cacheResult = compiler.ClassifyCache(state.compiled, builder, activeSet);

                if (cacheResult == CacheHitResult::FullHit) {
                    state.compiled.cacheResult = CacheHitResult::FullHit;
                    state.compiled.currentFrameIndex++;
                    PatchExternalResources(state.compiled, currentFrameCtx_);
                    continue;
                }

                if (cacheResult == CacheHitResult::DescriptorOnlyChange) {
                    auto result = compiler.CompileIncremental(builder, state.compiled);
                    if (result.has_value()) {
                        state.compiled = std::move(*result);
                        PatchExternalResources(state.compiled, currentFrameCtx_);
                        continue;
                    }
                    // Fall through to full recompile on failure
                }
            }

            // Full recompile
            auto result = compiler.Compile(builder);
            if (result.has_value()) {
                state.compiled = std::move(*result);
                state.hasCompiledGraph = true;
                PatchExternalResources(state.compiled, currentFrameCtx_);
            } else {
                allOk = false;
            }
        }
        return allOk;
    }

    auto LayerCompositor::GetCompiledGraph(LayerId layer) const -> const CompiledRenderGraph* {
        auto idx = static_cast<size_t>(layer);
        if (idx < kLayerCount && layers_[idx].hasCompiledGraph) {
            return &layers_[idx].compiled;
        }
        return nullptr;
    }

    auto LayerCompositor::GetLayerCacheResult(LayerId layer) const -> CacheHitResult {
        auto idx = static_cast<size_t>(layer);
        if (idx < kLayerCount && layers_[idx].hasCompiledGraph) {
            return layers_[idx].compiled.cacheResult;
        }
        return CacheHitResult::Miss;
    }

    auto LayerCompositor::GetActiveLayerCount() const noexcept -> uint32_t {
        uint32_t count = 0;
        for (auto& layer : layers_) {
            if (layer.active) {
                count++;
            }
        }
        return count;
    }

}  // namespace miki::rg
