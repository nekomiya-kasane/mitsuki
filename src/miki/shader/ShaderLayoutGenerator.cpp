/** @brief Reflection-driven descriptor layout auto-generation (§9.3).
 *
 *  Maps ShaderReflection BindingType -> rhi::BindingType and groups
 *  bindings by set index (0, 1, 2). Set 3+ are excluded (bindless / reserved).
 */

#include "miki/shader/ShaderLayoutGenerator.h"

#include <algorithm>
#include <unordered_map>

namespace miki::shader {

    static auto MapBindingType(BindingType iShaderType) -> rhi::BindingType {
        // shader::BindingType is an alias for rhi::BindingType — direct passthrough
        return iShaderType;
    }

    auto GeneratePipelineLayout(ShaderReflection const& iReflection, rhi::ShaderStage iStages)
        -> ReflectedPipelineLayout {
        // Group bindings by set index, only sets 0-2 (set 3 = bindless, fixed layout)
        std::unordered_map<uint32_t, std::vector<rhi::BindingDesc>> setBindings;

        for (auto const& b : iReflection.bindings) {
            if (b.set > 2) {
                continue;  // Skip bindless (set 3) and beyond
            }

            rhi::BindingDesc desc;
            desc.binding = b.binding;
            desc.type = MapBindingType(b.type);
            desc.stages = iStages;
            desc.count = b.count;
            setBindings[b.set].push_back(desc);
        }

        ReflectedPipelineLayout result;
        result.pushConstantSize = iReflection.pushConstantSize;
        result.pushConstantStages = iStages;

        // Collect and sort by set index
        for (auto& [setIdx, bindings] : setBindings) {
            // Sort bindings by binding index within each set
            std::ranges::sort(bindings, {}, &rhi::BindingDesc::binding);

            ReflectedSetLayout setLayout;
            setLayout.set = setIdx;
            setLayout.layout.bindings = std::move(bindings);
            result.sets.push_back(std::move(setLayout));
        }

        std::ranges::sort(result.sets, {}, &ReflectedSetLayout::set);
        return result;
    }

    auto DetectLayoutChanges(ReflectedPipelineLayout const& iPrevious, ReflectedPipelineLayout const& iCurrent)
        -> std::vector<LayoutChange> {
        std::vector<LayoutChange> changes;

        // Build lookup: set -> binding -> BindingDesc for previous layout
        std::unordered_map<uint64_t, rhi::BindingDesc const*> prevBindings;
        for (auto const& setLayout : iPrevious.sets) {
            for (auto const& b : setLayout.layout.bindings) {
                uint64_t key = (static_cast<uint64_t>(setLayout.set) << 32) | b.binding;
                prevBindings[key] = &b;
            }
        }

        // Check current layout against previous
        std::unordered_map<uint64_t, bool> currentKeys;
        for (auto const& setLayout : iCurrent.sets) {
            for (auto const& b : setLayout.layout.bindings) {
                uint64_t key = (static_cast<uint64_t>(setLayout.set) << 32) | b.binding;
                currentKeys[key] = true;

                auto it = prevBindings.find(key);
                if (it != prevBindings.end()) {
                    auto const* prev = it->second;
                    if (prev->type != b.type) {
                        changes.push_back(
                            {setLayout.set, b.binding,
                             "Binding type changed from " + std::to_string(static_cast<int>(prev->type)) + " to "
                                 + std::to_string(static_cast<int>(b.type))}
                        );
                    }
                    if (b.count < prev->count) {
                        changes.push_back(
                            {setLayout.set, b.binding,
                             "Binding count decreased from " + std::to_string(prev->count) + " to "
                                 + std::to_string(b.count)}
                        );
                    }
                }
            }
        }

        // Check for removed bindings (present in previous but not current)
        for (auto const& [key, prevDesc] : prevBindings) {
            if (!currentKeys.contains(key)) {
                uint32_t set = static_cast<uint32_t>(key >> 32);
                uint32_t binding = static_cast<uint32_t>(key & 0xFFFFFFFF);
                changes.push_back({set, binding, "Binding removed"});
            }
        }

        return changes;
    }

}  // namespace miki::shader
