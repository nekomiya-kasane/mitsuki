/** @brief SlangCompiler -- 5-target shader compilation with Pimpl ABI.
 *
 * Wraps the Slang compiler API behind a stable Pimpl interface.
 * Supports SPIR-V + DXIL + GLSL 4.30 + WGSL + MSL 3.0 compilation,
 * reflection extraction, and configurable search paths for
 * #include / import resolution.
 *
 * Architecture improvements over reference:
 *   - Session pool: reuses slang::ISession across compilations for the same target
 *   - Structured error return via ShaderDiagnostic (no stderr)
 *   - CompileAllTargets: single source -> 5 blobs (SPIR-V, DXIL, GLSL, WGSL, MSL)
 *   - CompileActiveTargets: user-selected subset compilation
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/shader/ShaderTypes.h"

#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace miki::shader {

    /** @brief Session cache hit/miss statistics. */
    struct CacheStats {
        uint64_t sessionHits = 0;    ///< Pooled session reuse count
        uint64_t sessionMisses = 0;  ///< New session creation count
        uint64_t moduleLoads = 0;    ///< Total module load calls
    };

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

        static constexpr size_t kTargetCount = 5;

        /** @brief Compile a shader to all 5 targets (SPIR-V, DXIL, GLSL 4.30, WGSL, MSL 3.0).
         *
         *  Array index corresponds to `static_cast<size_t>(ShaderTargetType)`:
         *    [0] = SPIRV, [1] = DXIL, [2] = GLSL, [3] = WGSL, [4] = MSL
         */
        [[nodiscard]] auto CompileAllTargets(
            std::filesystem::path const& iSourcePath, std::string const& iEntryPoint, ShaderStage iStage
        ) -> core::Result<std::array<ShaderBlob, kTargetCount>>;

        /** @brief Compile a shader for a user-specified subset of targets.
         *
         *  Returns one ShaderBlob per requested target, in the order given.
         */
        [[nodiscard]] auto CompileActiveTargets(
            std::filesystem::path const& iSourcePath, std::string const& iEntryPoint, ShaderStage iStage,
            std::span<const ShaderTarget> iTargets
        ) -> core::Result<std::vector<ShaderBlob>>;

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

        /** @brief Add a directory containing precompiled .slang-module files.
         *  Enables UseUpToDateBinaryModule: sessions will prefer precompiled modules
         *  but transparently recompile from source if stale.
         */
        auto AddPrecompiledModulePath(std::filesystem::path const& iPath) -> void;

        /** @brief Get session cache hit/miss statistics. */
        [[nodiscard]] auto GetCacheStats() const noexcept -> CacheStats;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        explicit SlangCompiler(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::shader
