/** @file OpenGLQuery.cpp
 *  @brief OpenGL 4.3+ (Tier 4) backend — QueryPool creation, results retrieval,
 *         timestamp period, command buffer creation/destruction.
 */

#include "miki/rhi/backend/OpenGLDevice.h"

#include "miki/rhi/backend/OpenGLCommandBuffer.h"

#include <cstring>

namespace miki::rhi {

    // =========================================================================
    // QueryPool
    // =========================================================================

    auto OpenGLDevice::CreateQueryPoolImpl(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle> {
        auto [handle, data] = queryPools_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        data->queries.resize(desc.count);
        gl_->GenQueries(desc.count, data->queries.data());
        data->count = desc.count;
        data->type = desc.type;

        if (desc.debugName && gl_->KHR_debug) {
            for (uint32_t i = 0; i < desc.count; ++i) {
                gl_->ObjectLabel(GL_QUERY, data->queries[i], -1, desc.debugName);
            }
        }

        return handle;
    }

    void OpenGLDevice::DestroyQueryPoolImpl(QueryPoolHandle h) {
        auto* data = queryPools_.Lookup(h);
        if (!data) {
            return;
        }
        if (!data->queries.empty()) {
            gl_->DeleteQueries(static_cast<GLsizei>(data->queries.size()), data->queries.data());
        }
        queryPools_.Free(h);
    }

    auto OpenGLDevice::GetQueryResultsImpl(
        QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results
    ) -> RhiResult<void> {
        auto* data = queryPools_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        uint32_t copyCount = std::min(count, static_cast<uint32_t>(results.size()));
        for (uint32_t i = 0; i < copyCount; ++i) {
            uint32_t idx = first + i;
            if (idx >= data->count) {
                break;
            }

            GLuint64 result = 0;
            gl_->GetQueryObjectui64v(data->queries[idx], GL_QUERY_RESULT, &result);
            results[i] = result;
        }
        return {};
    }

    auto OpenGLDevice::GetTimestampPeriodImpl() -> double {
        return 1.0;  // GL timestamps are already in nanoseconds
    }

    // =========================================================================
    // Command pool management (§19 — pool-level API, metadata-only for OpenGL)
    // =========================================================================

    auto OpenGLDevice::CreateCommandPoolImpl(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle> {
        auto [handle, data] = commandPools_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->queueType = desc.queue;
        return handle;
    }

    void OpenGLDevice::DestroyCommandPoolImpl(CommandPoolHandle h) {
        auto* data = commandPools_.Lookup(h);
        if (data) {
            for (auto& entry : data->cachedEntries) {
                if (entry.bufHandle.IsValid()) {
                    commandBuffers_.Free(entry.bufHandle);
                }
            }
            data->cachedEntries.clear();
        }
        commandPools_.Free(h);
    }

    void OpenGLDevice::ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags) {
        auto* data = commandPools_.Lookup(h);
        if (!data) {
            return;
        }
        if (static_cast<uint8_t>(flags) & static_cast<uint8_t>(CommandPoolResetFlags::ReleaseResources)) {
            for (auto& entry : data->cachedEntries) {
                if (entry.bufHandle.IsValid()) {
                    commandBuffers_.Free(entry.bufHandle);
                }
            }
            data->cachedEntries.clear();
        }
        data->nextFreeIndex = 0;
    }

    auto OpenGLDevice::AllocateFromPoolImpl(CommandPoolHandle pool, bool /*secondary*/)
        -> RhiResult<CommandListAcquisition> {
        auto* poolData = commandPools_.Lookup(pool);
        if (!poolData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Helper: populate HandlePool data and build acquisition result
        auto buildAcquisition
            = [&](CommandBufferHandle bufHandle, OpenGLCommandBuffer* wrapper) -> CommandListAcquisition {
            auto* bufData = commandBuffers_.Lookup(bufHandle);
            bufData->queueType = poolData->queueType;
            bufData->isSecondary = false;
            wrapper->Init(this);
            return {.bufferHandle = bufHandle, .listHandle = CommandListHandle(wrapper, BackendType::OpenGL43)};
        };

        // ----- Fast path: reuse cached entry (steady-state, zero-alloc) -----
        if (poolData->nextFreeIndex < poolData->cachedEntries.size()) {
            auto& entry = poolData->cachedEntries[poolData->nextFreeIndex++];
            return buildAcquisition(entry.bufHandle, entry.wrapper.get());
        }

        // ----- Cold path: create new wrapper (warm-up only) -----
        auto [bufHandle, bufData] = commandBuffers_.Allocate();
        if (!bufData) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        auto wrapper = std::make_unique<OpenGLCommandBuffer>();
        auto acq = buildAcquisition(bufHandle, wrapper.get());

        poolData->cachedEntries.push_back({.bufHandle = bufHandle, .wrapper = std::move(wrapper)});
        poolData->nextFreeIndex++;
        return acq;
    }

    void OpenGLDevice::FreeFromPoolImpl(CommandPoolHandle /*pool*/, const CommandListAcquisition& /*acq*/) {
        // No-op: pool-reset model (spec §19) reclaims all buffers via ResetCommandPool.
        // Cached entries persist until pool destruction.
    }

}  // namespace miki::rhi
