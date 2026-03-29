/** @file StagingRing.cpp
 *  @brief Per-frame ring buffer for CPU→GPU streaming uploads.
 *  See: StagingRing.h, specs/03-sync.md §7.1
 */

#include "miki/resource/StagingRing.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

#include "miki/rhi/backend/AllBackends.h"

namespace miki::resource {

    struct FrameChunk {
        uint64_t frameNumber;
        uint64_t offset;
        uint64_t size;
    };

    struct StagingRing::Impl {
        rhi::DeviceHandle device;
        rhi::BufferHandle buffer;
        void* mappedPtr = nullptr;
        uint64_t capacity = 0;
        uint64_t writeOffset = 0;   // Monotonic write head (wraps via modulo)
        uint64_t reclaimOffset = 0; // Oldest unreclaimable offset
        uint64_t unflushedStart = 0;
        std::vector<FrameChunk> chunks;
    };

    StagingRing::~StagingRing() {
        if (impl_ && impl_->buffer.IsValid()) {
            impl_->device.Dispatch([&](auto& dev) {
                dev.UnmapBuffer(impl_->buffer);
                dev.DestroyBuffer(impl_->buffer);
            });
        }
    }

    StagingRing::StagingRing(StagingRing&&) noexcept = default;
    auto StagingRing::operator=(StagingRing&&) noexcept -> StagingRing& = default;
    StagingRing::StagingRing(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto StagingRing::Create(rhi::DeviceHandle iDevice, uint64_t iCapacity) -> core::Result<StagingRing> {
        if (!iDevice.IsValid()) return std::unexpected(core::ErrorCode::InvalidArgument);

        auto bufResult = iDevice.Dispatch([&](auto& dev) {
            rhi::BufferDesc desc{
                .size = iCapacity,
                .usage = rhi::BufferUsage::TransferSrc,
                .memory = rhi::MemoryLocation::CpuToGpu,
            };
            return dev.CreateBuffer(desc);
        });
        if (!bufResult) return std::unexpected(core::ErrorCode::OutOfMemory);

        auto mapResult = iDevice.Dispatch([&](auto& dev) { return dev.MapBuffer(*bufResult); });
        if (!mapResult) {
            iDevice.Dispatch([&](auto& dev) { dev.DestroyBuffer(*bufResult); });
            return std::unexpected(core::ErrorCode::OutOfMemory);
        }

        auto impl = std::make_unique<Impl>();
        impl->device = iDevice;
        impl->buffer = *bufResult;
        impl->mappedPtr = *mapResult;
        impl->capacity = iCapacity;
        return StagingRing(std::move(impl));
    }

    auto StagingRing::Allocate(uint64_t iSize, uint64_t iAlignment) -> core::Result<StagingAllocation> {
        assert(impl_ && "StagingRing used after move");
        if (iSize == 0) return std::unexpected(core::ErrorCode::InvalidArgument);

        // Align write offset
        uint64_t aligned = (impl_->writeOffset + iAlignment - 1) & ~(iAlignment - 1);
        uint64_t wrapAligned = aligned % impl_->capacity;

        // Check if wrapping is needed
        if (wrapAligned + iSize > impl_->capacity) {
            // Need to wrap — skip to beginning
            wrapAligned = 0;
            aligned = ((impl_->writeOffset / impl_->capacity) + 1) * impl_->capacity;
        }

        // Check available space (write must not overtake reclaim)
        uint64_t used = aligned + iSize - impl_->reclaimOffset;
        if (used > impl_->capacity) {
            return std::unexpected(core::ErrorCode::OutOfMemory);
        }

        uint64_t physOffset = wrapAligned;
        impl_->writeOffset = aligned + iSize;

        return StagingAllocation{
            .cpuPtr = static_cast<uint8_t*>(impl_->mappedPtr) + physOffset,
            .gpuOffset = physOffset,
            .size = iSize,
            .buffer = impl_->buffer,
        };
    }

    auto StagingRing::FlushFrame(uint64_t iFrameNumber) -> void {
        assert(impl_ && "StagingRing used after move");
        uint64_t flushedSize = impl_->writeOffset - impl_->unflushedStart;
        if (flushedSize > 0) {
            impl_->chunks.push_back({
                .frameNumber = iFrameNumber,
                .offset = impl_->unflushedStart,
                .size = flushedSize,
            });
        }
        impl_->unflushedStart = impl_->writeOffset;
    }

    auto StagingRing::ReclaimCompleted(uint64_t iCompletedFrame) -> void {
        assert(impl_ && "StagingRing used after move");
        while (!impl_->chunks.empty() && impl_->chunks.front().frameNumber <= iCompletedFrame) {
            impl_->reclaimOffset = impl_->chunks.front().offset + impl_->chunks.front().size;
            impl_->chunks.erase(impl_->chunks.begin());
        }
    }

    auto StagingRing::Capacity() const noexcept -> uint64_t {
        return impl_ ? impl_->capacity : 0;
    }

    auto StagingRing::Available() const noexcept -> uint64_t {
        if (!impl_) return 0;
        uint64_t used = impl_->writeOffset - impl_->reclaimOffset;
        return (used < impl_->capacity) ? (impl_->capacity - used) : 0;
    }

}  // namespace miki::resource
