/** @file ReadbackRing.cpp
 *  @brief Per-frame ring buffer for GPU→CPU readback.
 *  See: ReadbackRing.h, specs/03-sync.md §7.2
 */

#include "miki/resource/ReadbackRing.h"

#include <algorithm>
#include <cassert>
#include <vector>

#include "miki/rhi/backend/AllBackends.h"

namespace miki::resource {

    struct ReadbackEntry {
        ReadbackFuture future;
        uint64_t offset;
        uint64_t size;
        uint64_t frameNumber;
        bool completed = false;
    };

    struct ReadbackRing::Impl {
        rhi::DeviceHandle device;
        rhi::BufferHandle buffer;
        void* mappedPtr = nullptr;
        uint64_t capacity = 0;
        uint64_t writeOffset = 0;
        uint64_t reclaimOffset = 0;
        uint64_t unflushedStart = 0;
        uint64_t nextFutureId = 1;
        std::vector<ReadbackEntry> entries;
    };

    ReadbackRing::~ReadbackRing() {
        if (impl_ && impl_->buffer.IsValid()) {
            impl_->device.Dispatch([&](auto& dev) {
                dev.UnmapBuffer(impl_->buffer);
                dev.DestroyBuffer(impl_->buffer);
            });
        }
    }

    ReadbackRing::ReadbackRing(ReadbackRing&&) noexcept = default;
    auto ReadbackRing::operator=(ReadbackRing&&) noexcept -> ReadbackRing& = default;
    ReadbackRing::ReadbackRing(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto ReadbackRing::Create(rhi::DeviceHandle iDevice, uint64_t iCapacity) -> core::Result<ReadbackRing> {
        if (!iDevice.IsValid()) return std::unexpected(core::ErrorCode::InvalidArgument);

        auto bufResult = iDevice.Dispatch([&](auto& dev) {
            rhi::BufferDesc desc{
                .size = iCapacity,
                .usage = rhi::BufferUsage::TransferDst,
                .memory = rhi::MemoryLocation::GpuToCpu,
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
        return ReadbackRing(std::move(impl));
    }

    auto ReadbackRing::Reserve(uint64_t iSize, uint64_t iAlignment) -> core::Result<ReadbackReservation> {
        assert(impl_ && "ReadbackRing used after move");
        if (iSize == 0) return std::unexpected(core::ErrorCode::InvalidArgument);

        uint64_t aligned = (impl_->writeOffset + iAlignment - 1) & ~(iAlignment - 1);
        uint64_t wrapAligned = aligned % impl_->capacity;

        if (wrapAligned + iSize > impl_->capacity) {
            wrapAligned = 0;
            aligned = ((impl_->writeOffset / impl_->capacity) + 1) * impl_->capacity;
        }

        uint64_t used = aligned + iSize - impl_->reclaimOffset;
        if (used > impl_->capacity) return std::unexpected(core::ErrorCode::OutOfMemory);

        uint64_t physOffset = wrapAligned;
        impl_->writeOffset = aligned + iSize;

        ReadbackFuture future{.id = impl_->nextFutureId++};
        impl_->entries.push_back({.future = future, .offset = physOffset, .size = iSize, .frameNumber = 0});

        return ReadbackReservation{.future = future, .dstBuffer = impl_->buffer, .dstOffset = physOffset};
    }

    auto ReadbackRing::FlushFrame(uint64_t iFrameNumber) -> void {
        assert(impl_ && "ReadbackRing used after move");
        for (auto& e : impl_->entries) {
            if (e.frameNumber == 0) e.frameNumber = iFrameNumber;
        }
        impl_->unflushedStart = impl_->writeOffset;
    }

    auto ReadbackRing::ReclaimCompleted(uint64_t iCompletedFrame) -> void {
        assert(impl_ && "ReadbackRing used after move");
        for (auto& e : impl_->entries) {
            if (!e.completed && e.frameNumber > 0 && e.frameNumber <= iCompletedFrame) {
                e.completed = true;
            }
        }
        // Reclaim from front while completed
        while (!impl_->entries.empty() && impl_->entries.front().completed) {
            auto& front = impl_->entries.front();
            impl_->reclaimOffset = front.offset + front.size;
            impl_->entries.erase(impl_->entries.begin());
        }
    }

    auto ReadbackRing::Resolve(ReadbackFuture iFuture) const noexcept -> std::span<const uint8_t> {
        if (!impl_) return {};
        auto it = std::ranges::find_if(impl_->entries, [&](const ReadbackEntry& e) { return e.future.id == iFuture.id; });
        if (it == impl_->entries.end() || !it->completed) return {};
        return {static_cast<const uint8_t*>(impl_->mappedPtr) + it->offset, it->size};
    }

    auto ReadbackRing::Capacity() const noexcept -> uint64_t { return impl_ ? impl_->capacity : 0; }

    auto ReadbackRing::Available() const noexcept -> uint64_t {
        if (!impl_) return 0;
        uint64_t used = impl_->writeOffset - impl_->reclaimOffset;
        return (used < impl_->capacity) ? (impl_->capacity - used) : 0;
    }

}  // namespace miki::resource
