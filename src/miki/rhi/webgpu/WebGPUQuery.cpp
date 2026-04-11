/** @file WebGPUQuery.cpp
 *  @brief WebGPU / Dawn (Tier 3) backend — query pool, command buffer create/destroy, timestamps.
 */

#include "miki/rhi/backend/WebGPUDevice.h"

#include "miki/rhi/backend/WebGPUCommandBuffer.h"
#include "miki/debug/StructuredLogger.h"

#include <dawn/webgpu.h>

namespace miki::rhi {

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
    // Command pool management (§19 — pool-level API, metadata-only for WebGPU)
    // =========================================================================

    auto WebGPUDevice::CreateCommandPoolImpl(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle> {
        auto [handle, data] = commandPools_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->queueType = desc.queue;
        return handle;
    }

    void WebGPUDevice::DestroyCommandPoolImpl(CommandPoolHandle h) {
        auto* data = commandPools_.Lookup(h);
        if (data) {
            for (auto& entry : data->cachedEntries) {
                auto* bufData = commandBuffers_.Lookup(entry.bufHandle);
                if (bufData && bufData->encoder) {
                    wgpuCommandEncoderRelease(bufData->encoder);
                    bufData->encoder = nullptr;
                }
                if (entry.bufHandle.IsValid()) {
                    commandBuffers_.Free(entry.bufHandle);
                }
            }
            data->cachedEntries.clear();
        }
        commandPools_.Free(h);
    }

    void WebGPUDevice::ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags) {
        auto* data = commandPools_.Lookup(h);
        if (!data) {
            return;
        }
        // Release any live encoders from previous frame
        for (uint32_t i = 0; i < data->nextFreeIndex && i < data->cachedEntries.size(); ++i) {
            auto* bufData = commandBuffers_.Lookup(data->cachedEntries[i].bufHandle);
            if (bufData && bufData->encoder) {
                wgpuCommandEncoderRelease(bufData->encoder);
                bufData->encoder = nullptr;
            }
            if (bufData) {
                bufData->finishedBuffer = nullptr;
            }
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

    auto WebGPUDevice::AllocateFromPoolImpl(CommandPoolHandle pool, bool /*secondary*/)
        -> RhiResult<CommandListAcquisition> {
        auto* poolData = commandPools_.Lookup(pool);
        if (!poolData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Helper: populate HandlePool data and build acquisition result
        auto buildAcquisition
            = [&](CommandBufferHandle bufHandle, WebGPUCommandBuffer* wrapper) -> CommandListAcquisition {
            auto* bufData = commandBuffers_.Lookup(bufHandle);
            bufData->queueType = poolData->queueType;
            bufData->isSecondary = false;
            bufData->encoder = nullptr;
            bufData->finishedBuffer = nullptr;
            wrapper->Init(this, bufHandle);
            return {.bufferHandle = bufHandle, .listHandle = CommandListHandle(wrapper, BackendType::WebGPU)};
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

        auto wrapper = std::make_unique<WebGPUCommandBuffer>();
        auto acq = buildAcquisition(bufHandle, wrapper.get());

        poolData->cachedEntries.push_back({.bufHandle = bufHandle, .wrapper = std::move(wrapper)});
        poolData->nextFreeIndex++;
        return acq;
    }

}  // namespace miki::rhi
