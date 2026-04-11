/** @file RenderGraphExtension.cpp
 *  @brief Implementation of the render graph plugin extension system (Phase K, §13).
 */

#include "miki/rendergraph/RenderGraphExtension.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>

#include "miki/rendergraph/RenderGraphBuilder.h"

namespace miki::rg {

    // =========================================================================
    // ExtensionRegistry
    // =========================================================================

    ExtensionRegistry::~ExtensionRegistry() {
        ShutdownAll();
    }

    auto ExtensionRegistry::RegisterExtension(std::unique_ptr<IRenderGraphExtension> ext) -> bool {
        assert(ext != nullptr && "Cannot register null extension");
        auto name = ext->GetName();
        assert(name != nullptr && "Extension must have a non-null name");

        // Check for existing extension with same name — replace if found
        for (auto& entry : entries_) {
            if (std::string_view(entry.extension->GetName()) == std::string_view(name)) {
                entry.extension->OnUnregistered();
                entry.extension = std::move(ext);
                entry.extension->OnRegistered();
                entry.manuallyDisabled = false;
                needsSort_ = true;
                return false;  // replaced
            }
        }

        ext->OnRegistered();
        entries_.push_back({.extension = std::move(ext), .manuallyDisabled = false});
        needsSort_ = true;
        return true;  // newly registered
    }

    auto ExtensionRegistry::UnregisterExtension(std::string_view name) -> bool {
        auto it = std::ranges::find_if(entries_, [name](const Entry& e) {
            return std::string_view(e.extension->GetName()) == name;
        });
        if (it == entries_.end()) {
            return false;
        }

        it->extension->OnUnregistered();
        entries_.erase(it);
        return true;
    }

    auto ExtensionRegistry::SetEnabled(std::string_view name, bool enabled) -> bool {
        auto it = std::ranges::find_if(entries_, [name](const Entry& e) {
            return std::string_view(e.extension->GetName()) == name;
        });
        if (it == entries_.end()) {
            return false;
        }

        it->manuallyDisabled = !enabled;
        return true;
    }

    auto ExtensionRegistry::IsRegistered(std::string_view name) const -> bool {
        return std::ranges::any_of(entries_, [name](const Entry& e) {
            return std::string_view(e.extension->GetName()) == name;
        });
    }

    auto ExtensionRegistry::IsEnabled(std::string_view name) const -> bool {
        auto it = std::ranges::find_if(entries_, [name](const Entry& e) {
            return std::string_view(e.extension->GetName()) == name;
        });
        return it != entries_.end() && !it->manuallyDisabled;
    }

    auto ExtensionRegistry::FindExtension(std::string_view name) const -> IRenderGraphExtension* {
        auto it = std::ranges::find_if(entries_, [name](const Entry& e) {
            return std::string_view(e.extension->GetName()) == name;
        });
        return it != entries_.end() ? it->extension.get() : nullptr;
    }

    auto ExtensionRegistry::InvokeExtensions(RenderGraphBuilder& builder, const ExtensionBuildContext& ctx)
        -> uint32_t {
        if (needsSort_) {
            SortByPriority();
            needsSort_ = false;
        }

        InvocationStats stats = {};
        stats.totalRegistered = static_cast<uint32_t>(entries_.size());

        for (auto& entry : entries_) {
            if (entry.manuallyDisabled) {
                continue;
            }
            stats.totalEnabled++;

            // Check GPU capability requirements
            auto required = entry.extension->GetRequiredCapabilities();
            if (required != ExtensionCapability::None && !HasCapability(availableCapabilities_, required)) {
                continue;
            }
            stats.totalCapable++;

            // Check per-frame activation
            if (!entry.extension->IsActiveThisFrame(ctx)) {
                continue;
            }
            stats.totalActive++;

            // Invoke BuildPasses
            uint32_t beforeInvoke = builder.GetPassCount();
            entry.extension->BuildPasses(builder, ctx);
            uint32_t afterInvoke = builder.GetPassCount();
            stats.passesInjected += (afterInvoke - beforeInvoke);
            stats.totalInvoked++;
        }

        lastStats_ = stats;
        return stats.totalInvoked;
    }

    void ExtensionRegistry::ShutdownAll() {
        for (auto& entry : entries_) {
            entry.extension->Shutdown();
        }
        entries_.clear();
    }

    auto ExtensionRegistry::GetExtensionNames() const -> std::vector<const char*> {
        std::vector<const char*> names;
        names.reserve(entries_.size());
        for (const auto& entry : entries_) {
            names.push_back(entry.extension->GetName());
        }
        return names;
    }

    void ExtensionRegistry::SortByPriority() {
        // Stable sort preserves registration order for equal priorities
        std::ranges::stable_sort(entries_, [](const Entry& a, const Entry& b) {
            return a.extension->GetPriority() < b.extension->GetPriority();
        });
    }

    // =========================================================================
    // PsoMissHandler
    // =========================================================================

    auto PsoMissHandler::ResolveAll(const CompiledRenderGraph& graph, std::span<const PassPsoConfig> configs)
        -> std::vector<PsoResolution> {
        auto numPasses = static_cast<uint32_t>(graph.passes.size());
        std::vector<PsoResolution> results(numPasses);

        stats_ = {};
        stats_.totalPasses = numPasses;

        // Phase 1: Resolve each pass independently
        // Track which resource indices have "not produced" status due to skipped passes
        // We use a flat bitset: resource index -> bool (produced or not)
        // For simplicity, we track the max resource index from edges
        uint32_t maxResourceIndex = 0;
        for (const auto& edge : graph.edges) {
            maxResourceIndex = std::max(maxResourceIndex, static_cast<uint32_t>(edge.resourceIndex) + 1);
        }
        // notProduced[i] = true means resource i was NOT produced this frame
        std::vector<bool> notProduced(maxResourceIndex, false);

        for (uint32_t i = 0; i < numPasses; ++i) {
            auto& res = results[i];

            // If no config for this pass, treat as "no PSO needed" (always ready)
            if (i >= configs.size()) {
                res.activePso = {};
                res.isPrimary = true;
                res.isSkipped = false;
                res.appliedPolicy = PsoMissPolicy::Skip;
                stats_.readyPasses++;
                continue;
            }

            const auto& cfg = configs[i];

            // If no primary PSO configured, pass doesn't need one (pure compute/transfer)
            if (!cfg.primaryPso.IsValid()) {
                res.activePso = {};
                res.isPrimary = true;
                res.isSkipped = false;
                res.appliedPolicy = cfg.missPolicy;
                stats_.readyPasses++;
                continue;
            }

            // Query primary PSO readiness
            auto status = readinessQuery_ ? readinessQuery_(cfg.primaryPso) : PsoCompileStatus::Ready;

            switch (status) {
                case PsoCompileStatus::Ready:
                    res.activePso = cfg.primaryPso;
                    res.isPrimary = true;
                    res.isSkipped = false;
                    res.appliedPolicy = cfg.missPolicy;
                    stats_.readyPasses++;
                    break;

                case PsoCompileStatus::Pending:
                    switch (cfg.missPolicy) {
                        case PsoMissPolicy::Fallback:
                            if (cfg.fallbackPso.IsValid()) {
                                res.activePso = cfg.fallbackPso;
                                res.isPrimary = false;
                                res.isSkipped = false;
                                res.appliedPolicy = PsoMissPolicy::Fallback;
                                stats_.fallbackPasses++;
                            } else {
                                // Fallback requested but not available — skip
                                res.isSkipped = true;
                                res.appliedPolicy = PsoMissPolicy::Skip;
                                stats_.skippedPasses++;
                            }
                            break;

                        case PsoMissPolicy::Skip:
                            res.isSkipped = true;
                            res.appliedPolicy = PsoMissPolicy::Skip;
                            stats_.skippedPasses++;
                            break;

                        case PsoMissPolicy::Stall:
                            // In stall mode, we report ready (caller must actually wait)
                            res.activePso = cfg.primaryPso;
                            res.isPrimary = true;
                            res.isSkipped = false;
                            res.appliedPolicy = PsoMissPolicy::Stall;
                            stats_.stalledPasses++;
                            break;
                    }
                    break;

                case PsoCompileStatus::Failed:
                    // PSO failed to compile — try fallback or skip
                    if (cfg.fallbackPso.IsValid()) {
                        res.activePso = cfg.fallbackPso;
                        res.isPrimary = false;
                        res.isSkipped = false;
                        res.appliedPolicy = PsoMissPolicy::Fallback;
                        stats_.fallbackPasses++;
                    } else {
                        res.isSkipped = true;
                        res.appliedPolicy = PsoMissPolicy::Skip;
                        stats_.skippedPasses++;
                    }
                    stats_.failedPasses++;
                    break;
            }

            // If pass is skipped, mark its output resources as "not produced"
            if (res.isSkipped) {
                // Find all resources written by this pass via graph edges
                for (const auto& edge : graph.edges) {
                    if (edge.srcPass == i && edge.resourceIndex < maxResourceIndex) {
                        notProduced[edge.resourceIndex] = true;
                    }
                }
            }
        }

        // Phase 2: Transitive DCE propagation
        // If a pass reads ONLY from "not produced" resources, it too should be skipped.
        // We iterate until convergence (typically 1-2 iterations for DAGs).
        bool changed = true;
        while (changed) {
            changed = false;
            for (uint32_t i = 0; i < numPasses; ++i) {
                if (results[i].isSkipped) {
                    continue;
                }

                // Check if all inputs to this pass come from skipped passes
                bool allInputsNotProduced = false;
                bool hasAnyInput = false;

                for (const auto& edge : graph.edges) {
                    if (edge.dstPass == i) {
                        hasAnyInput = true;
                        if (edge.resourceIndex < maxResourceIndex && notProduced[edge.resourceIndex]) {
                            // This input is not produced
                        } else {
                            // At least one input IS produced — pass can execute
                            allInputsNotProduced = false;
                            break;
                        }
                        allInputsNotProduced = true;
                    }
                }

                if (hasAnyInput && allInputsNotProduced) {
                    results[i].isSkipped = true;
                    results[i].appliedPolicy = PsoMissPolicy::Skip;
                    stats_.transitiveSkips++;
                    changed = true;

                    // Mark this pass's outputs as not produced too
                    for (const auto& edge : graph.edges) {
                        if (edge.srcPass == i && edge.resourceIndex < maxResourceIndex) {
                            notProduced[edge.resourceIndex] = true;
                        }
                    }
                }
            }
        }

        skippedCount_ = stats_.skippedPasses + stats_.transitiveSkips;
        fallbackCount_ = stats_.fallbackPasses;
        readyCount_ = stats_.readyPasses;
        return results;
    }

    auto PsoMissHandler::FormatStatus() const -> std::string {
        if (stats_.totalPasses == 0) {
            return "PSO: no passes";
        }

        auto ready = stats_.readyPasses;
        auto total = stats_.totalPasses;
        auto fallback = stats_.fallbackPasses;
        auto skipped = stats_.skippedPasses + stats_.transitiveSkips;

        if (skipped == 0 && fallback == 0) {
            return std::format("PSO: {}/{} ready", ready, total);
        }

        std::string result = std::format("PSO: {}/{} ready", ready, total);
        if (fallback > 0) {
            result += std::format(", {} fallback", fallback);
        }
        if (skipped > 0) {
            result += std::format(", {} skipped", skipped);
        }
        if (stats_.transitiveSkips > 0) {
            result += std::format(" ({} transitive)", stats_.transitiveSkips);
        }
        if (stats_.failedPasses > 0) {
            result += std::format(", {} FAILED", stats_.failedPasses);
        }
        return result;
    }

}  // namespace miki::rg
