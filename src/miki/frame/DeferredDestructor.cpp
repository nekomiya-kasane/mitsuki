/** @file DeferredDestructor.cpp
 *  @brief Type-erased per-frame deferred GPU resource destruction.
 *
 *  Internal storage: array of N bins, each bin is a vector of {HandleType, rawValue}.
 *  DrainBin iterates the vector and dispatches Destroy* via DeviceHandle.
 *
 *  See: DeferredDestructor.h for design rationale.
 */

#include "miki/frame/DeferredDestructor.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "miki/rhi/backend/AllBackends.h"

namespace miki::frame {

    // =========================================================================
    // HandleType — identifies which Destroy* to call
    // =========================================================================

    enum class DeferredHandleType : uint8_t {
        Buffer,
        Texture,
        TextureView,
        Sampler,
        Pipeline,
        DescriptorSet,
        ShaderModule,
        Fence,
        Semaphore,
        AccelStruct,
        PipelineLayout,
        DescriptorLayout,
        PipelineCache,
        QueryPool,
        DeviceMemory,
    };

    // =========================================================================
    // DeferredEntry — type-erased handle
    // =========================================================================

    struct DeferredEntry {
        DeferredHandleType type;
        uint64_t rawValue;  // Handle::value (opaque 64-bit)
    };

    // =========================================================================
    // Impl
    // =========================================================================

    struct DeferredDestructor::Impl {
        rhi::DeviceHandle device;
        uint32_t binCount = 2;
        uint32_t currentBin = 0;
        std::array<std::vector<DeferredEntry>, kMaxBins> bins;

        void ExecuteDestroy(const DeferredEntry& entry) {
            device.Dispatch([&](auto& dev) {
                switch (entry.type) {
                    case DeferredHandleType::Buffer: {
                        rhi::BufferHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyBuffer(h);
                        break;
                    }
                    case DeferredHandleType::Texture: {
                        rhi::TextureHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyTexture(h);
                        break;
                    }
                    case DeferredHandleType::TextureView: {
                        rhi::TextureViewHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyTextureView(h);
                        break;
                    }
                    case DeferredHandleType::Sampler: {
                        rhi::SamplerHandle h;
                        h.value = entry.rawValue;
                        dev.DestroySampler(h);
                        break;
                    }
                    case DeferredHandleType::Pipeline: {
                        rhi::PipelineHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyPipeline(h);
                        break;
                    }
                    case DeferredHandleType::DescriptorSet: {
                        rhi::DescriptorSetHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyDescriptorSet(h);
                        break;
                    }
                    case DeferredHandleType::ShaderModule: {
                        rhi::ShaderModuleHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyShaderModule(h);
                        break;
                    }
                    case DeferredHandleType::Fence: {
                        rhi::FenceHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyFence(h);
                        break;
                    }
                    case DeferredHandleType::Semaphore: {
                        rhi::SemaphoreHandle h;
                        h.value = entry.rawValue;
                        dev.DestroySemaphore(h);
                        break;
                    }
                    case DeferredHandleType::AccelStruct: {
                        rhi::AccelStructHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyAccelStruct(h);
                        break;
                    }
                    case DeferredHandleType::PipelineLayout: {
                        rhi::PipelineLayoutHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyPipelineLayout(h);
                        break;
                    }
                    case DeferredHandleType::DescriptorLayout: {
                        rhi::DescriptorLayoutHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyDescriptorLayout(h);
                        break;
                    }
                    case DeferredHandleType::PipelineCache: {
                        rhi::PipelineCacheHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyPipelineCache(h);
                        break;
                    }
                    case DeferredHandleType::QueryPool: {
                        rhi::QueryPoolHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyQueryPool(h);
                        break;
                    }
                    case DeferredHandleType::DeviceMemory: {
                        rhi::DeviceMemoryHandle h;
                        h.value = entry.rawValue;
                        dev.DestroyMemoryHeap(h);
                        break;
                    }
                }
            });
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    DeferredDestructor::~DeferredDestructor() {
        if (impl_) {
            DrainAll();
        }
    }

    DeferredDestructor::DeferredDestructor(DeferredDestructor&&) noexcept = default;
    auto DeferredDestructor::operator=(DeferredDestructor&&) noexcept -> DeferredDestructor& = default;
    DeferredDestructor::DeferredDestructor(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto DeferredDestructor::Create(rhi::DeviceHandle iDevice, uint32_t iBinCount) -> DeferredDestructor {
        auto impl = std::make_unique<Impl>();
        impl->device = iDevice;
        impl->binCount = std::clamp(iBinCount, 1u, kMaxBins);
        return DeferredDestructor(std::move(impl));
    }

    // =========================================================================
    // Deferred destroy — enqueue into current bin
    // =========================================================================

    auto DeferredDestructor::Destroy(rhi::BufferHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::Buffer, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::TextureHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::Texture, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::TextureViewHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::TextureView, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::SamplerHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::Sampler, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::PipelineHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::Pipeline, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::DescriptorSetHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::DescriptorSet, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::ShaderModuleHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::ShaderModule, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::FenceHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::Fence, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::SemaphoreHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::Semaphore, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::AccelStructHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::AccelStruct, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::PipelineLayoutHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::PipelineLayout, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::DescriptorLayoutHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::DescriptorLayout, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::PipelineCacheHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::PipelineCache, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::QueryPoolHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::QueryPool, iHandle.value});
        }
    }

    auto DeferredDestructor::Destroy(rhi::DeviceMemoryHandle iHandle) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        if (iHandle.IsValid()) {
            impl_->bins[impl_->currentBin].push_back({DeferredHandleType::DeviceMemory, iHandle.value});
        }
    }

    // =========================================================================
    // Bin management
    // =========================================================================

    auto DeferredDestructor::SetCurrentBin(uint32_t iBinIndex) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        assert(iBinIndex < impl_->binCount && "Bin index out of range");
        impl_->currentBin = iBinIndex;
    }

    auto DeferredDestructor::DrainBin(uint32_t iBinIndex) -> void {
        assert(impl_ && "DeferredDestructor used after move");
        assert(iBinIndex < impl_->binCount && "Bin index out of range");

        auto& bin = impl_->bins[iBinIndex];
        for (auto& entry : bin) {
            impl_->ExecuteDestroy(entry);
        }
        bin.clear();
    }

    auto DeferredDestructor::DrainAll() -> void {
        assert(impl_ && "DeferredDestructor used after move");
        for (uint32_t i = 0; i < impl_->binCount; ++i) {
            DrainBin(i);
        }
    }

    auto DeferredDestructor::PendingCount() const noexcept -> uint32_t {
        if (!impl_) {
            return 0;
        }
        uint32_t total = 0;
        for (uint32_t i = 0; i < impl_->binCount; ++i) {
            total += static_cast<uint32_t>(impl_->bins[i].size());
        }
        return total;
    }

}  // namespace miki::frame
