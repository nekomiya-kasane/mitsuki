/** @brief PermutationCache -- thread-safe LRU cache with optional disk persistence.
 *
 * Caches compiled ShaderBlobs keyed by (source path, entry point, target, permutation).
 * Supports lazy compilation via SlangCompiler and disk-backed caching.
 *
 * Architecture improvement over reference:
 *   - #include-aware disk cache hash: hashes all transitively included files
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/shader/ShaderTypes.h"

#include <functional>
#include <memory>

namespace miki::shader {

    class SlangCompiler;

    class PermutationCache {
       public:
        explicit PermutationCache(PermutationCacheConfig iConfig = {});

        ~PermutationCache();
        PermutationCache(PermutationCache&&) noexcept;
        auto operator=(PermutationCache&&) noexcept -> PermutationCache&;

        PermutationCache(PermutationCache const&) = delete;
        auto operator=(PermutationCache const&) -> PermutationCache& = delete;

        /** @brief Get or compile a shader blob for the given descriptor.
         *
         * If the blob is cached (in memory or on disk), returns it immediately.
         * Otherwise, invokes the compiler to produce it, caches the result, and returns it.
         */
        [[nodiscard]] auto GetOrCompile(ShaderCompileDesc const& iDesc, SlangCompiler& iCompiler)
            -> core::Result<ShaderBlob const*>;

        /** @brief Insert a pre-compiled blob into the cache. */
        auto Insert(ShaderCompileDesc const& iDesc, ShaderBlob iBlob) -> void;

        /** @brief Clear all in-memory entries. Does not touch disk cache. */
        auto Clear() -> void;

        /** @brief Number of entries currently in the in-memory cache. */
        [[nodiscard]] auto Size() const -> uint32_t;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}  // namespace miki::shader
