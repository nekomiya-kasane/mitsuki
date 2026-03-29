/** @brief SlangFeatureProbe implementation -- compiles probe shaders and collects results. */

#include "miki/shader/SlangFeatureProbe.h"
#include "miki/shader/SlangCompiler.h"
#include "miki/core/ErrorCode.h"

#include <algorithm>
#include <array>
#include <string>

namespace miki::shader {

    // ===========================================================================
    // Probe descriptor table -- one entry per test shader
    // ===========================================================================

    struct ProbeDesc {
        const char* name;
        const char* filename;
        ShaderStage stage;
        bool tier1Only;
    };

    static constexpr std::array kProbes = {
        ProbeDesc{"struct_array", "probe_struct_array.slang", ShaderStage::Compute, false},
        ProbeDesc{"atomics_32", "probe_atomics_32.slang", ShaderStage::Compute, false},
        ProbeDesc{"atomics_64", "probe_atomics_64.slang", ShaderStage::Compute, true},
        ProbeDesc{"subgroup_ballot", "probe_subgroup_ballot.slang", ShaderStage::Compute, false},
        ProbeDesc{"subgroup_shuffle", "probe_subgroup_shuffle.slang", ShaderStage::Compute, false},
        ProbeDesc{"subgroup_clustered", "probe_subgroup_clustered.slang", ShaderStage::Compute, false},
        ProbeDesc{"push_constants", "probe_push_constants.slang", ShaderStage::Compute, false},
        ProbeDesc{"texture_array", "probe_texture_array.slang", ShaderStage::Compute, false},
        ProbeDesc{"compute_shared", "probe_compute_shared.slang", ShaderStage::Compute, false},
        ProbeDesc{"barrier_semantics", "probe_barrier_semantics.slang", ShaderStage::Compute, false},
        ProbeDesc{"binding_map", "probe_binding_map.slang", ShaderStage::Compute, false},
        ProbeDesc{"half_precision", "probe_half_precision.slang", ShaderStage::Compute, false},
        ProbeDesc{"image_atomics", "probe_image_atomics.slang", ShaderStage::Compute, false},
        ProbeDesc{"mesh_shader", "probe_mesh_shader.slang", ShaderStage::Mesh, true},
        // --- GLSL-specific probes ---
        ProbeDesc{"glsl_ssbo_mapping", "probe_glsl_ssbo_mapping.slang", ShaderStage::Compute, false},
        ProbeDesc{"glsl_binding_layout", "probe_glsl_binding_layout.slang", ShaderStage::Compute, false},
        ProbeDesc{"glsl_texture_units", "probe_glsl_texture_units.slang", ShaderStage::Compute, false},
        ProbeDesc{"glsl_workgroup", "probe_glsl_workgroup.slang", ShaderStage::Compute, false},
        ProbeDesc{"glsl_shared_memory", "probe_glsl_shared_memory.slang", ShaderStage::Compute, false},
        ProbeDesc{"glsl_image_load_store", "probe_glsl_image_load_store.slang", ShaderStage::Compute, false},
        ProbeDesc{"glsl_atomic_32", "probe_glsl_atomic_32.slang", ShaderStage::Compute, false},
        ProbeDesc{"glsl_push_constant_ubo", "probe_glsl_push_constant_ubo.slang", ShaderStage::Compute, false},
        // --- WGSL-specific probes ---
        ProbeDesc{"wgsl_storage_alignment", "probe_wgsl_storage_alignment.slang", ShaderStage::Compute, false},
        ProbeDesc{"wgsl_workgroup_limits", "probe_wgsl_workgroup_limits.slang", ShaderStage::Compute, false},
        ProbeDesc{"wgsl_no_64bit_atomics", "probe_wgsl_no_64bit_atomics.slang", ShaderStage::Compute, true},
        ProbeDesc{"wgsl_group_binding", "probe_wgsl_group_binding.slang", ShaderStage::Compute, false},
        ProbeDesc{"wgsl_texture_sample", "probe_wgsl_texture_sample.slang", ShaderStage::Compute, false},
        ProbeDesc{"wgsl_array_stride", "probe_wgsl_array_stride.slang", ShaderStage::Compute, false},
        ProbeDesc{"wgsl_push_constant_ubo", "probe_wgsl_push_constant_ubo.slang", ShaderStage::Compute, false},
    };

    // ===========================================================================
    // Internal: run one probe
    // ===========================================================================

    static auto RunProbe(
        SlangCompiler& iCompiler, ProbeDesc const& iProbe, ShaderTarget iTarget, std::filesystem::path const& iShaderDir
    ) -> ProbeTestResult {
        ProbeTestResult result;
        result.name = iProbe.name;
        result.target = iTarget;

        auto shaderPath = iShaderDir / iProbe.filename;
        if (!std::filesystem::exists(shaderPath)) {
            result.passed = false;
            result.skipped = true;
            result.diagnostic = "Shader file not found: " + shaderPath.string();
            return result;
        }

        ShaderCompileDesc desc;
        desc.sourcePath = shaderPath;
        desc.entryPoint = "main";
        desc.stage = iProbe.stage;
        desc.target = iTarget;

        auto blob = iCompiler.Compile(desc);
        if (blob.has_value()) {
            result.passed = !blob->data.empty();
            if (!result.passed) {
                result.diagnostic = "Compilation produced empty blob";
            }
        } else {
            result.passed = false;
            result.diagnostic = "Compilation failed";
        }
        return result;
    }

    // ===========================================================================
    // Public API
    // ===========================================================================

    auto SlangFeatureProbe::RunAll(
        SlangCompiler& iCompiler, std::span<const ShaderTarget> iTargets, std::filesystem::path const& iShaderDir
    ) -> core::Result<ProbeReport> {
        if (iTargets.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        ProbeReport report;
        for (auto target : iTargets) {
            for (auto const& probe : kProbes) {
                auto testResult = RunProbe(iCompiler, probe, target, iShaderDir);
                if (testResult.skipped) {
                    ++report.totalSkipped;
                } else if (testResult.passed) {
                    ++report.totalPassed;
                } else {
                    ++report.totalFailed;
                }
                report.results.push_back(std::move(testResult));
            }
        }
        return report;
    }

    auto SlangFeatureProbe::RunSingle(
        SlangCompiler& iCompiler, std::string_view iTestName, ShaderTarget iTarget,
        std::filesystem::path const& iShaderDir
    ) -> core::Result<ProbeTestResult> {
        auto it
            = std::ranges::find_if(kProbes, [&](ProbeDesc const& p) { return std::string_view(p.name) == iTestName; });
        if (it == kProbes.end()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return RunProbe(iCompiler, *it, iTarget, iShaderDir);
    }

}  // namespace miki::shader
