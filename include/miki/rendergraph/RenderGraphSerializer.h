/** @file RenderGraphSerializer.h
 *  @brief JSON serialization for RenderGraph snapshots (Builder + Compiled output).
 *
 *  Exports the full render graph state as a self-contained JSON document suitable
 *  for offline analysis and the rg-visualizer interactive tool.
 *
 *  No external JSON library dependency — uses a minimal std::string-based writer.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <string>

#include "miki/rendergraph/RenderGraphCompiler.h"

namespace miki::rg {

    struct ExecutionStats;  // forward-declared; defined in RenderGraphExecutor.h

    struct SerializerOptions {
        bool prettyPrint = true;
        bool includeExecutionStats = false;
        uint64_t frameNumber = 0;
    };

    /// @brief Serialize a compiled render graph + builder metadata to JSON string.
    /// @param builder The finalized graph builder (source of pass/resource declarations).
    /// @param compiled The compiled render graph (output of Compiler::Compile).
    /// @param compilerOptions The options used during compilation.
    /// @param stats Optional execution stats (only included if options.includeExecutionStats).
    /// @param options Serialization options.
    /// @return JSON string.
    [[nodiscard]] auto SerializeToJSON(
        const RenderGraphBuilder& builder, const CompiledRenderGraph& compiled,
        const RenderGraphCompiler::Options& compilerOptions, const ExecutionStats* stats = nullptr,
        const SerializerOptions& options = {}
    ) -> std::string;

    /// @brief Serialize and write to file.
    /// @return true on success.
    [[nodiscard]] auto SerializeToFile(
        const RenderGraphBuilder& builder, const CompiledRenderGraph& compiled,
        const RenderGraphCompiler::Options& compilerOptions, const char* path, const ExecutionStats* stats = nullptr,
        const SerializerOptions& options = {}
    ) -> bool;

}  // namespace miki::rg
