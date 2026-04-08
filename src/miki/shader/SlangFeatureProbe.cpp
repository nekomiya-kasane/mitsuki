/** @brief SlangFeatureProbe implementation -- compiles probe shaders and collects results. */

#include "miki/shader/SlangFeatureProbe.h"

#include "miki/core/ErrorCode.h"
#include "miki/debug/StructuredLogger.h"
#include "miki/shader/SlangCompiler.h"

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
        ProbeDesc{
            .name = "struct_array",
            .filename = "probe_struct_array.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "atomics_32",
            .filename = "probe_atomics_32.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "atomics_64", .filename = "probe_atomics_64.slang", .stage = ShaderStage::Compute, .tier1Only = true
        },
        ProbeDesc{
            .name = "subgroup_ballot",
            .filename = "probe_subgroup_ballot.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "subgroup_shuffle",
            .filename = "probe_subgroup_shuffle.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "subgroup_clustered",
            .filename = "probe_subgroup_clustered.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "push_constants",
            .filename = "probe_push_constants.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "texture_array",
            .filename = "probe_texture_array.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "compute_shared",
            .filename = "probe_compute_shared.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "barrier_semantics",
            .filename = "probe_barrier_semantics.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "binding_map",
            .filename = "probe_binding_map.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "half_precision",
            .filename = "probe_half_precision.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "image_atomics",
            .filename = "probe_image_atomics.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "mesh_shader", .filename = "probe_mesh_shader.slang", .stage = ShaderStage::Mesh, .tier1Only = true
        },
        // --- GLSL-specific probes ---
        ProbeDesc{
            .name = "glsl_ssbo_mapping",
            .filename = "probe_glsl_ssbo_mapping.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "glsl_binding_layout",
            .filename = "probe_glsl_binding_layout.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "glsl_texture_units",
            .filename = "probe_glsl_texture_units.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "glsl_workgroup",
            .filename = "probe_glsl_workgroup.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "glsl_shared_memory",
            .filename = "probe_glsl_shared_memory.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "glsl_image_load_store",
            .filename = "probe_glsl_image_load_store.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "glsl_atomic_32",
            .filename = "probe_glsl_atomic_32.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "glsl_push_constant_ubo",
            .filename = "probe_glsl_push_constant_ubo.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        // --- WGSL-specific probes ---
        ProbeDesc{
            .name = "wgsl_storage_alignment",
            .filename = "probe_wgsl_storage_alignment.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "wgsl_workgroup_limits",
            .filename = "probe_wgsl_workgroup_limits.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "wgsl_no_64bit_atomics",
            .filename = "probe_wgsl_no_64bit_atomics.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = true
        },
        ProbeDesc{
            .name = "wgsl_group_binding",
            .filename = "probe_wgsl_group_binding.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "wgsl_texture_sample",
            .filename = "probe_wgsl_texture_sample.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "wgsl_array_stride",
            .filename = "probe_wgsl_array_stride.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
        ProbeDesc{
            .name = "wgsl_push_constant_ubo",
            .filename = "probe_wgsl_push_constant_ubo.slang",
            .stage = ShaderStage::Compute,
            .tier1Only = false
        },
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
                MIKI_LOG_WARN(
                    debug::LogCategory::Shader, "[FeatureProbe] {} EMPTY for target {}", iProbe.name,
                    static_cast<int>(iTarget.type)
                );
            } else {
                MIKI_LOG_TRACE(
                    debug::LogCategory::Shader, "[FeatureProbe] {} PASS ({} bytes)", iProbe.name, blob->data.size()
                );
            }
        } else {
            result.passed = false;
            result.diagnostic = "Compilation failed";
            MIKI_LOG_TRACE(debug::LogCategory::Shader, "[FeatureProbe] {} FAIL", iProbe.name);
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

        MIKI_LOG_INFO(
            debug::LogCategory::Shader, "[FeatureProbe] RunAll: {} target(s), {} probe(s)", iTargets.size(),
            kProbes.size()
        );

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

        MIKI_LOG_INFO(
            debug::LogCategory::Shader, "[FeatureProbe] Results: {} passed, {} failed, {} skipped", report.totalPassed,
            report.totalFailed, report.totalSkipped
        );
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
