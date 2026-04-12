/** @brief PermutationCache implementation.
 *
 * Thread-safe LRU cache with optional disk persistence for compiled shaders.
 *
 * Architecture improvement over reference:
 *   - #include-aware disk cache hash: scans source for #include / import
 *     directives and hashes all transitively referenced files.
 */

#include "miki/shader/PermutationCache.h"

#include "miki/debug/StructuredLogger.h"
#include "miki/shader/SlangCompiler.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace miki::shader {

    // ===========================================================================
    // Cache key
    // ===========================================================================

    struct CacheKey {
        std::string sourcePath;
        std::string entryPoint;
        ShaderTarget target;
        ShaderStage stage;
        ShaderPermutationKey permutation;

        auto operator==(CacheKey const&) const noexcept -> bool = default;
    };

    struct CacheKeyHash {
        auto operator()(CacheKey const& k) const noexcept -> size_t {
            size_t h = std::hash<std::string>{}(k.sourcePath);
            h ^= std::hash<std::string>{}(k.entryPoint) + 0x9e3779b9 + (h << 6) + (h >> 2);
            // Hash ShaderTarget: combine type + versionMajor + versionMinor
            uint32_t targetKey = static_cast<uint32_t>(k.target.type)
                                 | (static_cast<uint32_t>(k.target.versionMajor) << 8)
                                 | (static_cast<uint32_t>(k.target.versionMinor) << 16);
            h ^= std::hash<uint32_t>{}(targetKey) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(k.stage)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint64_t>{}(k.permutation.bits) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    static auto MakeCacheKey(ShaderCompileDesc const& iDesc) -> CacheKey {
        return CacheKey{
            .sourcePath = iDesc.sourcePath.string(),
            .entryPoint = iDesc.entryPoint,
            .target = iDesc.target,
            .stage = iDesc.stage,
            .permutation = iDesc.permutation,
        };
    }

    // ===========================================================================
    // #include-aware source hash
    // ===========================================================================

    namespace {
        namespace fs = std::filesystem;

        auto ReadFileBytes(fs::path const& iPath) -> std::string {
            std::ifstream file(iPath, std::ios::binary | std::ios::ate);
            if (!file) {
                return {};
            }
            auto size = file.tellg();
            if (size <= 0) {
                return {};
            }
            file.seekg(0);
            std::string content(static_cast<size_t>(size), '\0');
            file.read(content.data(), size);
            return content;
        }

        /** @brief Collect all transitively #include'd / import'd files. */
        auto CollectTransitiveDeps(fs::path const& iRootFile, std::unordered_set<std::string>& oVisited) -> void {
            auto canonical = fs::exists(iRootFile) ? fs::canonical(iRootFile).string() : iRootFile.string();
            if (oVisited.contains(canonical)) {
                return;
            }
            oVisited.insert(canonical);

            std::ifstream in(iRootFile);
            if (!in.is_open()) {
                return;
            }

            static std::regex const includeRe(R"(^\s*#\s*include\s*\"([^\"]+)\")");
            static std::regex const importRe(R"(^\s*import\s+([a-zA-Z0-9_./\\]+)\s*;)");

            auto parentDir = iRootFile.parent_path();
            std::string line;
            while (std::getline(in, line)) {
                std::smatch match;
                if (std::regex_search(line, match, includeRe)) {
                    auto incPath = parentDir / match[1].str();
                    if (fs::exists(incPath)) {
                        CollectTransitiveDeps(incPath, oVisited);
                    }
                } else if (std::regex_search(line, match, importRe)) {
                    auto modName = match[1].str();
                    std::replace(modName.begin(), modName.end(), '.', '/');
                    auto modPath = parentDir / (modName + ".slang");
                    if (fs::exists(modPath)) {
                        CollectTransitiveDeps(modPath, oVisited);
                    }
                }
            }
        }

        /** @brief Compute a combined hash of root file + all transitive deps. */
        auto ComputeTransitiveSourceHash(fs::path const& iSourcePath) -> uint64_t {
            std::unordered_set<std::string> visited;
            CollectTransitiveDeps(iSourcePath, visited);

            // Sort for deterministic hash
            std::vector<std::string> sorted(visited.begin(), visited.end());
            std::ranges::sort(sorted);

            uint64_t combined = 0;
            for (auto const& path : sorted) {
                auto content = ReadFileBytes(path);
                auto h = std::hash<std::string>{}(content);
                combined ^= h + 0x9e3779b97f4a7c15ULL + (combined << 12) + (combined >> 4);
            }
            return combined;
        }
    }  // namespace

    // ===========================================================================
    // Disk cache helpers
    // ===========================================================================

    static auto DiskCachePath(fs::path const& iCacheDir, CacheKey const& iKey) -> fs::path {
        auto hash = CacheKeyHash{}(iKey);
        auto filename = std::to_string(hash);
        switch (iKey.target.type) {
            case ShaderTargetType::SPIRV: filename += ".spv"; break;
            case ShaderTargetType::DXIL: filename += ".dxil"; break;
            case ShaderTargetType::GLSL: filename += ".glsl"; break;
            case ShaderTargetType::WGSL: filename += ".wgsl"; break;
            case ShaderTargetType::MSL: filename += ".msl"; break;
        }
        return iCacheDir / filename;
    }

    static auto HashFilePath(fs::path const& iBlobPath) -> fs::path {
        auto p = iBlobPath;
        p += ".hash";
        return p;
    }

    static auto WriteBlobToDisk(fs::path const& iPath, ShaderBlob const& iBlob, uint64_t iSourceHash) -> bool {
        std::error_code ec;
        fs::create_directories(iPath.parent_path(), ec);
        if (ec) {
            return false;
        }

        std::ofstream file(iPath, std::ios::binary);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<char const*>(iBlob.data.data()), static_cast<std::streamsize>(iBlob.data.size()));
        if (!file.good()) {
            return false;
        }

        std::ofstream hashFile(HashFilePath(iPath), std::ios::binary);
        if (!hashFile) {
            return false;
        }
        hashFile.write(reinterpret_cast<char const*>(&iSourceHash), sizeof(iSourceHash));
        return hashFile.good();
    }

    static auto ReadBlobFromDisk(
        fs::path const& iPath, ShaderTarget iTarget, ShaderStage iStage, std::string const& iEntryPoint,
        uint64_t iExpectedSourceHash
    ) -> std::optional<ShaderBlob> {
        std::ifstream hashFile(HashFilePath(iPath), std::ios::binary);
        if (hashFile) {
            uint64_t storedHash = 0;
            hashFile.read(reinterpret_cast<char*>(&storedHash), sizeof(storedHash));
            if (hashFile.good() && storedHash != iExpectedSourceHash) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }

        std::ifstream file(iPath, std::ios::binary | std::ios::ate);
        if (!file) {
            return std::nullopt;
        }
        auto size = file.tellg();
        if (size <= 0) {
            return std::nullopt;
        }
        file.seekg(0);

        ShaderBlob blob;
        blob.data.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(blob.data.data()), size);
        if (!file.good()) {
            return std::nullopt;
        }

        blob.target = iTarget;
        blob.stage = iStage;
        blob.entryPoint = iEntryPoint;
        return blob;
    }

    // ===========================================================================
    // Impl -- LRU cache
    // ===========================================================================

    struct PermutationCache::Impl {
        PermutationCacheConfig config;

        using LruList = std::list<std::pair<CacheKey, ShaderBlob>>;
        LruList lruList;
        std::unordered_map<CacheKey, LruList::iterator, CacheKeyHash> map;
        mutable std::mutex mutex;

        void Evict() {
            while (map.size() > config.maxEntries && !lruList.empty()) {
                auto& back = lruList.back();
                map.erase(back.first);
                lruList.pop_back();
            }
        }

        void Touch(LruList::iterator it) { lruList.splice(lruList.begin(), lruList, it); }

        auto Insert(CacheKey key, ShaderBlob blob) -> ShaderBlob const* {
            auto it = map.find(key);
            if (it != map.end()) {
                it->second->second = std::move(blob);
                Touch(it->second);
                return &it->second->second;
            }
            lruList.emplace_front(std::move(key), std::move(blob));
            auto listIt = lruList.begin();
            map[listIt->first] = listIt;
            Evict();
            return &listIt->second;
        }

        auto Find(CacheKey const& key) -> ShaderBlob const* {
            auto it = map.find(key);
            if (it == map.end()) {
                return nullptr;
            }
            Touch(it->second);
            return &it->second->second;
        }
    };

    // ===========================================================================
    // Public API
    // ===========================================================================

    PermutationCache::PermutationCache(PermutationCacheConfig iConfig) : impl_(std::make_unique<Impl>()) {
        impl_->config = std::move(iConfig);
    }

    PermutationCache::~PermutationCache() = default;
    PermutationCache::PermutationCache(PermutationCache&&) noexcept = default;
    auto PermutationCache::operator=(PermutationCache&&) noexcept -> PermutationCache& = default;

    auto PermutationCache::GetOrCompile(ShaderCompileDesc const& iDesc, SlangCompiler& iCompiler)
        -> core::Result<ShaderBlob const*> {
        auto key = MakeCacheKey(iDesc);

        {
            std::lock_guard lock(impl_->mutex);
            if (auto* cached = impl_->Find(key)) {
                MIKI_LOG_DEBUG(
                    debug::LogCategory::Shader, "[PermutationCache] Memory hit: {} [{}]", iDesc.sourcePath.string(),
                    iDesc.entryPoint
                );
                return cached;
            }
        }

        // #include-aware transitive hash
        auto sourceHash = ComputeTransitiveSourceHash(iDesc.sourcePath);

        if (impl_->config.enableDiskCache) {
            auto diskPath = DiskCachePath(impl_->config.cacheDir, key);
            auto diskBlob = ReadBlobFromDisk(diskPath, iDesc.target, iDesc.stage, iDesc.entryPoint, sourceHash);
            if (diskBlob) {
                MIKI_LOG_DEBUG(
                    debug::LogCategory::Shader, "[PermutationCache] Disk hit: {} ({} bytes)", iDesc.sourcePath.string(),
                    diskBlob->data.size()
                );
                std::lock_guard lock(impl_->mutex);
                return impl_->Insert(std::move(key), std::move(*diskBlob));
            }
        }

        MIKI_LOG_DEBUG(
            debug::LogCategory::Shader, "[PermutationCache] Cache miss, compiling: {} -> {}", iDesc.sourcePath.string(),
            iDesc.entryPoint
        );
        auto compileResult = iCompiler.Compile(iDesc);
        if (!compileResult) {
            return std::unexpected(compileResult.error());
        }

        if (impl_->config.enableDiskCache) {
            auto diskPath = DiskCachePath(impl_->config.cacheDir, key);
            if (WriteBlobToDisk(diskPath, *compileResult, sourceHash)) {
                MIKI_LOG_TRACE(debug::LogCategory::Shader, "[PermutationCache] Disk write: {}", diskPath.string());
            }
        }

        std::lock_guard lock(impl_->mutex);
        return impl_->Insert(std::move(key), std::move(*compileResult));
    }

    auto PermutationCache::Insert(ShaderCompileDesc const& iDesc, ShaderBlob iBlob) -> void {
        auto key = MakeCacheKey(iDesc);
        std::lock_guard lock(impl_->mutex);
        impl_->Insert(std::move(key), std::move(iBlob));
    }

    auto PermutationCache::Clear() -> void {
        std::lock_guard lock(impl_->mutex);
        auto count = impl_->map.size();
        impl_->map.clear();
        impl_->lruList.clear();
        if (count > 0) {
            MIKI_LOG_DEBUG(debug::LogCategory::Shader, "[PermutationCache] Cleared {} entries", count);
        }
    }

    auto PermutationCache::Size() const -> uint32_t {
        std::lock_guard lock(impl_->mutex);
        return static_cast<uint32_t>(impl_->map.size());
    }

}  // namespace miki::shader
