/** @brief SlangCompiler -- quad-target shader compilation with Pimpl ABI.
 *
 * Wraps the Slang compiler API behind a stable Pimpl interface.
 * Supports SPIR-V + DXIL + GLSL 4.30 + WGSL quad-target compilation,
 * reflection extraction, and configurable search paths for
 * #include / import resolution.
 *
 * Architecture improvements over reference:
 *   - Session pool: reuses slang::ISession across compilations for the same target
 *   - Structured error return via ShaderDiagnostic (no stderr)
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/shader/ShaderTypes.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace miki::shader {

    /** @brief Structured shader compilation diagnostic. */
    struct ShaderDiagnostic {
        std::string message;
        std::string filePath;
        uint32_t line = 0;
        uint32_t column = 0;
        enum class Severity : uint8_t {
            Info,
            Warning,
            Error
        } severity = Severity::Error;
    };

    class SlangCompiler {
       public:
        [[nodiscard]] static auto Create() -> core::Result<SlangCompiler>;

        ~SlangCompiler();
        SlangCompiler(SlangCompiler&&) noexcept;
        auto operator=(SlangCompiler&&) noexcept -> SlangCompiler&;

        SlangCompiler(SlangCompiler const&) = delete;
        auto operator=(SlangCompiler const&) -> SlangCompiler& = delete;

        /** @brief Compile a shader to the target specified in the descriptor. */
        [[nodiscard]] auto Compile(ShaderCompileDesc const& iDesc) -> core::Result<ShaderBlob>;

        /** @brief Compile a shader to both SPIR-V and DXIL from the same source. */
        [[nodiscard]] auto CompileDualTarget(
            std::filesystem::path const& iSourcePath, std::string const& iEntryPoint, ShaderStage iStage
        ) -> core::Result<std::pair<ShaderBlob, ShaderBlob>>;

        static constexpr size_t kTargetCount = 4;

        /** @brief Compile a shader to all 4 targets (SPIR-V, DXIL, GLSL 4.30, WGSL).
         *
         *  Array index corresponds to `static_cast<size_t>(ShaderTarget)`:
         *    [0] = SPIRV, [1] = DXIL, [2] = GLSL, [3] = WGSL
         */
        [[nodiscard]] auto CompileQuadTarget(
            std::filesystem::path const& iSourcePath, std::string const& iEntryPoint, ShaderStage iStage
        ) -> core::Result<std::array<ShaderBlob, kTargetCount>>;

        /** @brief Full reflection: bindings, thread group, vertex inputs, push constant size,
         *  module constants (AST), and struct layouts.
         */
        [[nodiscard]] auto Reflect(ShaderCompileDesc const& iDesc) -> core::Result<ShaderReflection>;

        /** @brief Add a search path for Slang #include / import resolution. */
        auto AddSearchPath(std::filesystem::path const& iPath) -> void;

        /** @brief Get diagnostics from the most recent compilation. Thread-local buffer. */
        [[nodiscard]] auto GetLastDiagnostics() const -> std::span<const ShaderDiagnostic>;

        /** @brief Invalidate cached sessions (call after search path changes). */
        auto InvalidateSessionCache() -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        explicit SlangCompiler(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::shader
