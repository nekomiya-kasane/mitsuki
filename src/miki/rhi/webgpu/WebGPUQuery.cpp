/** @file WebGPUQuery.cpp
 *  @brief WebGPU / Dawn (Tier 3) backend — query pool, command buffer create/destroy, timestamps.
 */

#include "miki/rhi/backend/WebGPUDevice.h"

#include "miki/rhi/backend/WebGPUCommandBuffer.h"
#include "miki/debug/StructuredLogger.h"

#include <algorithm>
#include <dawn/webgpu.h>

namespace miki::rhi {

    // =========================================================================
    // Command buffer create / destroy
    // =========================================================================

    auto WebGPUDevice::CreateCommandBufferImpl(const CommandBufferDesc& desc) -> RhiResult<CommandBufferHandle> {
        auto [handle, data] = commandBuffers_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        data->queueType = desc.type;
        data->isSecondary = desc.secondary;
        data->encoder = nullptr;  // Created lazily at Begin()

        return handle;
    }

    void WebGPUDevice::DestroyCommandBufferImpl(CommandBufferHandle h) {
        auto* data = commandBuffers_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->encoder) {
            wgpuCommandEncoderRelease(data->encoder);
            data->encoder = nullptr;
        }
        commandBuffers_.Free(h);
    }

    // =========================================================================
    // Query pool
    // =========================================================================

    auto WebGPUDevice::CreateQueryPoolImpl(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle> {
        auto [handle, data] = queryPools_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        WGPUQuerySetDescriptor qsDesc{};
        switch (desc.type) {
            case QueryType::Timestamp: qsDesc.type = WGPUQueryType_Timestamp; break;
            case QueryType::Occlusion: qsDesc.type = WGPUQueryType_Occlusion; break;
            default:
                queryPools_.Free(handle);
                MIKI_LOG_WARN(
                    ::miki::debug::LogCategory::Rhi, "WebGPU T3: query type {} not supported",
                    static_cast<int>(desc.type)
                );
                return std::unexpected(RhiError::FeatureNotSupported);
        }
        qsDesc.count = desc.count;

        data->querySet = wgpuDeviceCreateQuerySet(device_, &qsDesc);
        if (!data->querySet) {
            queryPools_.Free(handle);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        data->type = desc.type;
        data->count = desc.count;

        // Create resolve buffer (GPU-side, for resolving query results)
        if (desc.type == QueryType::Timestamp) {
            WGPUBufferDescriptor resolveBufDesc{};
            resolveBufDesc.label = {.data = "miki_query_resolve", .length = WGPU_STRLEN};
            resolveBufDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
            resolveBufDesc.size = desc.count * sizeof(uint64_t);
            data->resolveBuffer = wgpuDeviceCreateBuffer(device_, &resolveBufDesc);

            // Create readback buffer (MAP_READ, for CPU access)
            WGPUBufferDescriptor readbackBufDesc{};
            readbackBufDesc.label = {.data = "miki_query_readback", .length = WGPU_STRLEN};
            readbackBufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            readbackBufDesc.size = desc.count * sizeof(uint64_t);
            data->readbackBuffer = wgpuDeviceCreateBuffer(device_, &readbackBufDesc);
        }

        return handle;
    }

    void WebGPUDevice::DestroyQueryPoolImpl(QueryPoolHandle h) {
        auto* data = queryPools_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->querySet) {
            wgpuQuerySetDestroy(data->querySet);
            wgpuQuerySetRelease(data->querySet);
        }
        if (data->resolveBuffer) {
            wgpuBufferDestroy(data->resolveBuffer);
            wgpuBufferRelease(data->resolveBuffer);
        }
        if (data->readbackBuffer) {
            wgpuBufferDestroy(data->readbackBuffer);
            wgpuBufferRelease(data->readbackBuffer);
        }
        queryPools_.Free(h);
    }

    auto WebGPUDevice::GetQueryResultsImpl(
        QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results
    ) -> RhiResult<void> {
        auto* data = queryPools_.Lookup(h);
        if (!data || !data->querySet) {
            return std::unexpected(RhiError::InvalidHandle);
        }
        if (!data->resolveBuffer || !data->readbackBuffer) {
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        uint64_t byteOffset = first * sizeof(uint64_t);
        uint64_t byteSize = count * sizeof(uint64_t);

        // Resolve queries into resolve buffer, then copy to readback buffer
        WGPUCommandEncoderDescriptor encDesc{};
        encDesc.label = {.data = "miki_query_resolve_enc", .length = WGPU_STRLEN};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encDesc);

        wgpuCommandEncoderResolveQuerySet(encoder, data->querySet, first, count, data->resolveBuffer, byteOffset);
        wgpuCommandEncoderCopyBufferToBuffer(
            encoder, data->resolveBuffer, byteOffset, data->readbackBuffer, byteOffset, byteSize
        );

        WGPUCommandBufferDescriptor cbDesc{};
        cbDesc.label = {.data = "miki_query_resolve_cb", .length = WGPU_STRLEN};
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbDesc);
        wgpuQueueSubmit(queue_, 1, &cmdBuf);
        wgpuCommandBufferRelease(cmdBuf);
        wgpuCommandEncoderRelease(encoder);

        // Map readback buffer synchronously
        struct MapData {
            WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
            bool done = false;
        } mapData;

        WGPUBufferMapCallbackInfo mapCbInfo{};
        mapCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        mapCbInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
            auto* md = static_cast<MapData*>(userdata1);
            md->status = status;
            md->done = true;
        };
        mapCbInfo.userdata1 = &mapData;
        wgpuBufferMapAsync(data->readbackBuffer, WGPUMapMode_Read, byteOffset, byteSize, mapCbInfo);

#ifndef EMSCRIPTEN
        while (!mapData.done) {
            wgpuDeviceTick(device_);
        }
#endif

        if (mapData.status != WGPUMapAsyncStatus_Success) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }

        const void* mapped = wgpuBufferGetConstMappedRange(data->readbackBuffer, byteOffset, byteSize);
        if (mapped && results.size() >= count) {
            std::memcpy(results.data(), mapped, byteSize);
        }
        wgpuBufferUnmap(data->readbackBuffer);

        return {};
    }

    auto WebGPUDevice::GetTimestampPeriodImpl() -> double {
        // Dawn reports timestamp period in nanoseconds
        // wgpuAdapterGetLimits doesn't expose timestamp period directly,
        // but Dawn's timestamp counter is in nanoseconds (period = 1.0)
        return 1.0;
    }

    // =========================================================================
    // Command buffer creation/destruction (continued)
    // =========================================================================

    auto WebGPUDevice::AcquireCommandListImpl(QueueType queue) -> RhiResult<CommandListAcquisition> {
        CommandBufferDesc desc{.type = queue, .secondary = false};
        auto bufResult = CreateCommandBufferImpl(desc);
        if (!bufResult) {
            return std::unexpected(bufResult.error());
        }

        auto bufHandle = *bufResult;
        auto& cmdBuf = commandListArena_.emplace_back(std::make_unique<WebGPUCommandBuffer>());
        cmdBuf->Init(this, bufHandle);

        CommandListHandle listHandle(cmdBuf.get(), BackendType::WebGPU);
        return CommandListAcquisition{.bufferHandle = bufHandle, .listHandle = listHandle};
    }

    void WebGPUDevice::ReleaseCommandListImpl(const CommandListAcquisition& acq) {
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
