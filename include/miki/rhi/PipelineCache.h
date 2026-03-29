/** @brief Pipeline cache — serialize/deserialize compiled pipeline state to disk.
 *
 * Per-backend: Vulkan uses VkPipelineCache, D3D12 uses ID3D12PipelineLibrary,
 * GL/WebGPU/Mock are no-op pass-through.
 *
 * Cache header validates driver version + device ID. On mismatch the blob
 * is discarded and an empty cache is created (no error — graceful rebuild).
 *
 * Thread safety: single-threaded (init time only).
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <vector>

#include "miki/core/ErrorCode.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

    // ===========================================================================
    // Cache header (on-disk format)
    // ===========================================================================

    /** @brief On-disk header for pipeline cache validation. */
    struct PipelineCacheHeader {
        uint32_t magic = 0x4D4B5043;  ///< "MKPC"
        uint32_t version = 1;
        uint32_t driverVersion = 0;
        uint32_t deviceId = 0;
        uint64_t dataSize = 0;
    };
    static_assert(sizeof(PipelineCacheHeader) == 24);

    // ===========================================================================
    // PipelineCache
    // ===========================================================================

    /** @brief Pipeline cache — load from / save to disk.
     *
     * Load() reads a cache file, validates the header, and creates
     * a backend-specific cache object. If the file is missing or invalid,
     * an empty (valid) cache is created.
     *
     * Save() serializes the current cache state to disk.
     *
     * GetNativeHandle() returns the backend-specific handle (e.g., VkPipelineCache)
     * for use during pipeline creation.
     */
    class PipelineCache {
       public:
        /** @brief Load pipeline cache from disk (or create empty if missing/invalid).
         *  @param iDevice  Device handle for backend-specific cache creation.
         *  @param iPath    File path to load from.
         *  @return PipelineCache instance (always valid — empty on file miss/mismatch).
         */
        [[nodiscard]] static auto Load(DeviceHandle iDevice, const std::filesystem::path& iPath)
            -> std::expected<PipelineCache, core::ErrorCode>;

        /** @brief Save pipeline cache to disk.
         *  @param iPath File path to write to.
         *  @return void on success, ErrorCode on I/O failure.
         */
        [[nodiscard]] auto Save(const std::filesystem::path& iPath) -> std::expected<void, core::ErrorCode>;

        /** @brief Get the RHI handle for use in pipeline creation descriptors. */
        [[nodiscard]] auto GetHandle() const noexcept -> PipelineCacheHandle { return handle_; }

        /** @brief Check if this cache holds a valid backend object. */
        [[nodiscard]] auto IsValid() const noexcept -> bool { return valid_; }

        ~PipelineCache();

        PipelineCache(const PipelineCache&) = delete;
        auto operator=(const PipelineCache&) -> PipelineCache& = delete;
        PipelineCache(PipelineCache&& iOther) noexcept;
        auto operator=(PipelineCache&& iOther) noexcept -> PipelineCache&;

       private:
        PipelineCache() = default;

        bool valid_ = false;
        PipelineCacheHandle handle_;
        DeviceHandle device_;
        std::vector<uint8_t> cacheBlob_;
        BackendType backendType_ = BackendType::Mock;
    };

}  // namespace miki::rhi
