/** @brief SlangFeatureProbe -- exhaustive shader feature regression suite.
 *
 * Compiles ~29 focused test shaders against SPIR-V/DXIL/GLSL/WGSL targets,
 * collecting pass/fail results. No GPU required -- compilation-only.
 * Validates correct code generation and tier degradation behavior.
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/shader/ShaderTypes.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace miki::shader {

    class SlangCompiler;

    /** @brief Result of a single probe test. */
    struct ProbeTestResult {
        std::string name;
        ShaderTarget target;  // Default: SPIRV 1.5
        bool passed = false;
        bool skipped = false;
        std::string diagnostic;
    };

    /** @brief Aggregated probe report for all tests across all targets. */
    struct ProbeReport {
        std::vector<ProbeTestResult> results;
        uint32_t totalPassed = 0;
        uint32_t totalFailed = 0;
        uint32_t totalSkipped = 0;
    };

    /** @brief Stateless probe runner -- compiles test shaders and reports results. */
    class SlangFeatureProbe {
       public:
        [[nodiscard]] static auto RunAll(
            SlangCompiler& iCompiler, std::span<const ShaderTarget> iTargets, std::filesystem::path const& iShaderDir
        ) -> core::Result<ProbeReport>;

        [[nodiscard]] static auto RunSingle(
            SlangCompiler& iCompiler, std::string_view iTestName, ShaderTarget iTarget,
            std::filesystem::path const& iShaderDir
        ) -> core::Result<ProbeTestResult>;
    };

}  // namespace miki::shader
