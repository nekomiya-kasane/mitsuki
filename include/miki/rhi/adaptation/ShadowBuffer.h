/** @file ShadowBuffer.h
 *  @brief CPU-side shadow buffer for GPU buffers that forbid direct mapping.
 *
 *  Designed as a composable utility for backend BufferData structs.
 *  Zero overhead when inactive: empty vector, IsActive() = false.
 *
 *  Usage pattern (backend Impl):
 *    1. CreateBufferImpl: if adaptation requires ShadowBuffer, call Activate(size)
 *    2. MapBufferImpl:    if shadow.IsActive(), return shadow.Map()
 *    3. UnmapBufferImpl:  if shadow.IsActive(), flush shadow.Data() to GPU, then shadow.Unmap()
 *    4. DestroyBufferImpl: shadow destructor handles cleanup
 *
 *  Namespace: miki::rhi::adaptation
 *  Spec reference: rendering-pipeline-architecture.md §20b
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace miki::rhi::adaptation {

    class ShadowBuffer {
       public:
        ShadowBuffer() = default;
        ~ShadowBuffer() = default;

        ShadowBuffer(const ShadowBuffer&) = delete;
        auto operator=(const ShadowBuffer&) -> ShadowBuffer& = delete;
        ShadowBuffer(ShadowBuffer&&) noexcept = default;
        auto operator=(ShadowBuffer&&) noexcept -> ShadowBuffer& = default;

        /// Allocate CPU shadow storage. Called once at buffer creation.
        void Activate(uint64_t size) {
            data_.resize(static_cast<size_t>(size));
            active_ = true;
        }

        /// Release shadow storage. Idempotent.
        void Deactivate() {
            data_.clear();
            data_.shrink_to_fit();
            active_ = false;
        }

        [[nodiscard]] auto IsActive() const noexcept -> bool { return active_; }

        /// Return writable pointer to shadow data (equivalent to GPU map).
        [[nodiscard]] auto Map() noexcept -> void* { return data_.data(); }

        /// Mark shadow as unmapped. Does NOT flush to GPU — caller is responsible.
        void Unmap() noexcept { /* no-op: state tracking if needed in future */ }

        /// Read-only view for flushing to GPU (e.g., wgpuQueueWriteBuffer).
        [[nodiscard]] auto Data() const noexcept -> std::span<const uint8_t> { return data_; }

        /// Shadow buffer size in bytes.
        [[nodiscard]] auto Size() const noexcept -> uint64_t { return data_.size(); }

       private:
        std::vector<uint8_t> data_;
        bool active_ = false;
    };

}  // namespace miki::rhi::adaptation
