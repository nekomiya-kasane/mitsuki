/** @file OpenGLQuery.cpp
 *  @brief OpenGL 4.3+ (Tier 4) backend — QueryPool creation, results retrieval,
 *         timestamp period, command buffer creation/destruction.
 */

#include "miki/rhi/backend/OpenGLDevice.h"

#include "miki/rhi/backend/OpenGLCommandBuffer.h"

#include <algorithm>
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
    // Command buffer creation/destruction
    // =========================================================================

    auto OpenGLDevice::CreateCommandBufferImpl(const CommandBufferDesc& desc) -> RhiResult<CommandBufferHandle> {
        auto [handle, data] = commandBuffers_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->queueType = desc.type;
        data->isSecondary = desc.secondary;
        return handle;
    }

    void OpenGLDevice::DestroyCommandBufferImpl(CommandBufferHandle h) {
        auto* data = commandBuffers_.Lookup(h);
        if (!data) {
            return;
        }
        commandBuffers_.Free(h);
    }

    // =========================================================================
    // Command list acquisition (unified factory)
    // =========================================================================

    auto OpenGLDevice::AcquireCommandListImpl(QueueType queue) -> RhiResult<CommandListAcquisition> {
        CommandBufferDesc desc{.type = queue, .secondary = false};
        auto bufResult = CreateCommandBufferImpl(desc);
        if (!bufResult) {
            return std::unexpected(bufResult.error());
        }

        auto bufHandle = *bufResult;
        auto& cmdBuf = commandListArena_.emplace_back(std::make_unique<OpenGLCommandBuffer>());
        cmdBuf->Init(this);

        CommandListHandle listHandle(cmdBuf.get(), BackendType::OpenGL43);
        return CommandListAcquisition{.bufferHandle = bufHandle, .listHandle = listHandle};
    }

    void OpenGLDevice::ReleaseCommandListImpl(const CommandListAcquisition& acq) {
        void* raw = acq.listHandle.GetRawPtr();
        auto it = std::find_if(commandListArena_.begin(), commandListArena_.end(), [raw](const auto& p) {
            return p.get() == raw;
        });
        if (it != commandListArena_.end()) {
            commandListArena_.erase(it);
        }
        DestroyCommandBufferImpl(acq.bufferHandle);
    }

}  // namespace miki::rhi
