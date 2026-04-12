/** @brief PipelineCache implementation — file I/O + backend dispatch.
 *
 * Load: read file -> validate header -> create backend cache object.
 * Save: serialize backend cache -> write header + blob to file.
 * Vulkan: VkPipelineCache via vkCreatePipelineCache / vkGetPipelineCacheData.
 * D3D12/GL/WebGPU/Mock: no-op (empty blob, null native handle).
 */

#include "miki/rhi/PipelineCache.h"

#include <cstring>
#include <expected>
#include <fstream>
#include <utility>

#include "miki/rhi/backend/AllBackends.h"

namespace miki::rhi {

    // ===========================================================================
    // Load
    // ===========================================================================

    auto PipelineCache::Load(DeviceHandle iDevice, const std::filesystem::path& iPath) -> core::Result<PipelineCache> {
        PipelineCache pc;
        pc.valid_ = true;
        pc.backendType_ = iDevice.GetBackendType();

        // GL/WebGPU/Mock: no native pipeline cache — return valid empty cache
        if (pc.backendType_ != BackendType::Vulkan14 && pc.backendType_ != BackendType::VulkanCompat
            && pc.backendType_ != BackendType::D3D12) {
            return pc;
        }

        // Try to read existing cache file
        std::vector<uint8_t> fileData;
        if (std::filesystem::exists(iPath)) {
            std::ifstream file(iPath, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                auto size = file.tellg();
                if (size > static_cast<std::streampos>(sizeof(PipelineCacheHeader))) {
                    fileData.resize(static_cast<size_t>(size));
                    file.seekg(0);
                    file.read(reinterpret_cast<char*>(fileData.data()), static_cast<std::streamsize>(size));
                }
            }
        }

        // Validate header if we have data
        std::vector<uint8_t> cacheData;
        if (fileData.size() > sizeof(PipelineCacheHeader)) {
            PipelineCacheHeader header{};
            std::memcpy(&header, fileData.data(), sizeof(header));

            bool headerValid = (header.magic == PipelineCacheHeader{}.magic)
                               && (header.version == PipelineCacheHeader{}.version)
                               && (header.dataSize + sizeof(header) <= fileData.size());

            if (headerValid) {
                cacheData.assign(
                    fileData.begin() + sizeof(PipelineCacheHeader),
                    fileData.begin() + static_cast<ptrdiff_t>(sizeof(PipelineCacheHeader) + header.dataSize)
                );
            }
            // Invalid header -> discard silently, create empty cache
        }

        // Create backend pipeline cache from loaded blob (or empty)
        pc.device_ = iDevice;
        auto result = iDevice.Dispatch([&](auto& dev) { return dev.CreatePipelineCache(cacheData); });
        if (result) {
            pc.handle_ = *result;
        }
        pc.cacheBlob_ = std::move(cacheData);

        return pc;
    }

    // ===========================================================================
    // Save
    // ===========================================================================

    auto PipelineCache::Save(const std::filesystem::path& iPath) -> core::VoidResult {
        if (!valid_) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        // GL/WebGPU/Mock: no-op save (success, no file written)
        if (backendType_ != BackendType::Vulkan14 && backendType_ != BackendType::VulkanCompat
            && backendType_ != BackendType::D3D12) {
            return {};
        }

        // Query fresh cache data from the backend
        std::vector<uint8_t> freshBlob;
        if (handle_.IsValid() && device_.IsValid()) {
            freshBlob = device_.Dispatch([&](auto& dev) { return dev.GetPipelineCacheData(handle_); });
        }

        // Ensure parent directory exists
        auto parentDir = iPath.parent_path();
        if (!parentDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parentDir, ec);
            if (ec) {
                return std::unexpected(core::ErrorCode::IoError);
            }
        }

        // Write header + blob
        PipelineCacheHeader header{};
        header.dataSize = freshBlob.size();

        std::ofstream file(iPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return std::unexpected(core::ErrorCode::IoError);
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!freshBlob.empty()) {
            file.write(reinterpret_cast<const char*>(freshBlob.data()), static_cast<std::streamsize>(freshBlob.size()));
        }

        if (!file.good()) {
            return std::unexpected(core::ErrorCode::IoError);
        }

        return {};
    }

    // ===========================================================================
    // Destructor + move
    // ===========================================================================

    PipelineCache::~PipelineCache() {
        if (handle_.IsValid() && device_.IsValid()) {
            device_.Dispatch([&](auto& dev) { dev.DestroyPipelineCache(handle_); });
        }
    }

    PipelineCache::PipelineCache(PipelineCache&& o) noexcept
        : valid_{std::exchange(o.valid_, false)}
        , handle_{std::exchange(o.handle_, {})}
        , device_{o.device_}
        , cacheBlob_{std::move(o.cacheBlob_)}
        , backendType_{o.backendType_} {}

    auto PipelineCache::operator=(PipelineCache&& o) noexcept -> PipelineCache& {
        if (this != &o) {
            if (handle_.IsValid() && device_.IsValid()) {
                device_.Dispatch([&](auto& dev) { dev.DestroyPipelineCache(handle_); });
            }
            valid_ = std::exchange(o.valid_, false);
            handle_ = std::exchange(o.handle_, {});
            device_ = o.device_;
            cacheBlob_ = std::move(o.cacheBlob_);
            backendType_ = o.backendType_;
        }
        return *this;
    }

}  // namespace miki::rhi
