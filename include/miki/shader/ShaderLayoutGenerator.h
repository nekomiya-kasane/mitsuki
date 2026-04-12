/** @file ShaderLayoutGenerator.h
 *  @brief Reflection-driven descriptor layout auto-generation (§9.3).
 *
 *  Bridges ShaderReflection::bindings -> DescriptorLayoutDesc for sets 0-2.
 *  Eliminates manual layout maintenance as shaders evolve.
 *  Debug-mode layout change detection triggers warnings on layout-breaking changes.
 *
 *  Namespace: miki::shader
 */
#pragma once

#include "miki/rhi/Descriptors.h"
#include "miki/shader/ShaderTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace miki::shader {

    /** @brief A fully-owned descriptor layout description (not using spans). */
    struct OwnedDescriptorLayoutDesc {
        std::vector<rhi::BindingDesc> bindings;
        bool pushDescriptor = false;
    };

    /** @brief Per-set layout generated from shader reflection. */
    struct ReflectedSetLayout {
        uint32_t set = 0;
        OwnedDescriptorLayoutDesc layout;
    };

    /** @brief Result of layout generation: one OwnedDescriptorLayoutDesc per used set (0-2). */
    struct ReflectedPipelineLayout {
        std::vector<ReflectedSetLayout> sets;  ///< Sorted by set index (0, 1, 2)
        uint32_t pushConstantSize = 0;         ///< From reflection
        rhi::ShaderStage pushConstantStages = rhi::ShaderStage::All;
    };

    /** @brief Layout change info for debug-mode validation. */
    struct LayoutChange {
        uint32_t set = 0;
        uint32_t binding = 0;
        std::string description;
    };

    /** @brief Generate descriptor set layouts from shader reflection data.
     *
     *  Filters bindings by set number (0, 1, 2). Set 3 is reserved for bindless (fixed layout).
     *  Each binding is mapped: ShaderReflection::BindingInfo -> rhi::BindingDesc.
     *
     *  @param iReflection  Reflection data from SlangCompiler::Reflect().
     *  @param iStages      Shader stages that use these bindings (for stage visibility mask).
     *  @return Pipeline layout with per-set binding descriptions.
     */
    [[nodiscard]] auto GeneratePipelineLayout(
        ShaderReflection const& iReflection, rhi::ShaderStage iStages = rhi::ShaderStage::All
    ) -> ReflectedPipelineLayout;

    /** @brief Compare two reflected layouts and report breaking changes.
     *
     *  A change is "breaking" if: binding type changed, binding removed, or binding count decreased.
     *  Adding new bindings is non-breaking.
     *
     *  @param iPrevious  Previous layout (from last successful compilation).
     *  @param iCurrent   Current layout (from new compilation).
     *  @return List of breaking changes (empty = compatible).
     */
    [[nodiscard]] auto DetectLayoutChanges(
        ReflectedPipelineLayout const& iPrevious, ReflectedPipelineLayout const& iCurrent
    ) -> std::vector<LayoutChange>;

}  // namespace miki::shader
