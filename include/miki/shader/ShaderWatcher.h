/** @brief ShaderWatcher -- hot-reload system for .slang files.
 *
 * Monitors a directory for changes, tracks include/import dependencies,
 * recompiles affected shaders via SlangCompiler, and signals changes.
 *
 * Architecture improvements over reference:
 *   - std::jthread (C++23) for automatic cancellation
 *   - Transitive dependency closure via BFS (not just 1-level)
 *   - Structured error reporting
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/shader/ShaderTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace miki::shader {

    class SlangCompiler;

    /** @brief Configuration for ShaderWatcher. */
    struct ShaderWatcherConfig {
        uint32_t debounceMs = 100;
        std::vector<ShaderTarget> targets;
    };

    /** @brief A successfully recompiled shader change. */
    struct ShaderChange {
        std::filesystem::path path;
        ShaderTarget target;  // Default: SPIRV 1.5
        ShaderBlob blob;
        uint64_t generation = 0;
    };

    /** @brief A shader compilation error during hot-reload. */
    struct ShaderError {
        std::filesystem::path path;
        std::string message;
        uint32_t line = 0;
        uint32_t column = 0;
    };

    class ShaderWatcher {
       public:
        [[nodiscard]] static auto Create(SlangCompiler& iCompiler, ShaderWatcherConfig iConfig = {})
            -> core::Result<ShaderWatcher>;

        ~ShaderWatcher();
        ShaderWatcher(ShaderWatcher&&) noexcept;
        auto operator=(ShaderWatcher&&) noexcept -> ShaderWatcher&;

        ShaderWatcher(ShaderWatcher const&) = delete;
        auto operator=(ShaderWatcher const&) -> ShaderWatcher& = delete;

        /** @brief Start watching a directory for .slang file changes. */
        [[nodiscard]] auto Start(std::filesystem::path const& iWatchDir) -> core::Result<void>;

        /** @brief Stop watching. Idempotent. */
        auto Stop() -> void;

        /** @brief Poll for recompiled shader changes. Clears the internal queue. */
        [[nodiscard]] auto Poll() -> std::vector<ShaderChange>;

        /** @brief Monotonically increasing generation counter. Increments on each successful recompile. */
        [[nodiscard]] auto GetGeneration() const noexcept -> uint64_t;

        /** @brief Get errors from the most recent compilation attempts. */
        [[nodiscard]] auto GetLastErrors() const -> std::span<const ShaderError>;

        /** @brief Check if the watcher background thread is running. */
        [[nodiscard]] auto IsRunning() const noexcept -> bool;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        explicit ShaderWatcher(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::shader
