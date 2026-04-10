/** @file D3D12Query.cpp
 *  @brief D3D12 (Tier 1) backend — QueryPool creation, results retrieval,
 *         timestamp period, command buffer creation/destruction.
 */

#include "miki/rhi/backend/D3D12Device.h"

#include "miki/rhi/backend/D3D12CommandBuffer.h"

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

namespace miki::rhi {

    namespace {
        auto ToD3D12QueryHeapType(QueryType type) -> D3D12_QUERY_HEAP_TYPE {
            switch (type) {
                case QueryType::Timestamp: return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
                case QueryType::Occlusion: return D3D12_QUERY_HEAP_TYPE_OCCLUSION;
                case QueryType::PipelineStatistics: return D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
            }
            return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        }
    }  // namespace

    auto D3D12Device::CreateQueryPoolImpl(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle> {
        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = ToD3D12QueryHeapType(desc.type);
        heapDesc.Count = desc.count;
        heapDesc.NodeMask = 0;

        ComPtr<ID3D12QueryHeap> heap;
        HRESULT hr = device_->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&heap));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        // Create readback buffer for resolved query results
        uint64_t readbackSize = desc.count * sizeof(uint64_t);
        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = readbackSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        ComPtr<ID3D12Resource> readbackBuffer;
        hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readbackBuffer)
        );
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = queryPools_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->heap = std::move(heap);
        data->readbackBuffer = std::move(readbackBuffer);
        data->count = desc.count;
        data->type = desc.type;

        if (desc.debugName) {
            wchar_t wname[256]{};
            MultiByteToWideChar(CP_UTF8, 0, desc.debugName, -1, wname, 256);
            data->heap->SetName(wname);
        }

        return handle;
    }

    void D3D12Device::DestroyQueryPoolImpl(QueryPoolHandle h) {
        auto* data = queryPools_.Lookup(h);
        if (!data) {
            return;
        }
        queryPools_.Free(h);
    }

    auto D3D12Device::GetQueryResultsImpl(
        QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results
    ) -> RhiResult<void> {
        auto* data = queryPools_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Map the readback buffer and copy results
        void* mapped = nullptr;
        D3D12_RANGE readRange{first * sizeof(uint64_t), (first + count) * sizeof(uint64_t)};
        HRESULT hr = data->readbackBuffer->Map(0, &readRange, &mapped);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::DeviceLost);
        }

        auto* src = static_cast<const uint64_t*>(mapped) + first;
        uint32_t copyCount = std::min(count, static_cast<uint32_t>(results.size()));
        std::memcpy(results.data(), src, copyCount * sizeof(uint64_t));

        D3D12_RANGE writeRange{0, 0};
        data->readbackBuffer->Unmap(0, &writeRange);
        return {};
    }

    auto D3D12Device::GetTimestampPeriodImpl() -> double {
        uint64_t frequency = 0;
        queues_.graphics->GetTimestampFrequency(&frequency);
        if (frequency == 0) {
            return 0.0;
        }
        return 1e9 / static_cast<double>(frequency);  // Nanoseconds per tick
    }

    // =========================================================================
    // Command pool management (§19 — pool-level API)
    // =========================================================================

    auto D3D12Device::CreateCommandPoolImpl(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle> {
        D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (desc.queue == QueueType::Compute) {
            type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        } else if (desc.queue == QueueType::Transfer) {
            type = D3D12_COMMAND_LIST_TYPE_COPY;
        }

        auto [handle, data] = commandPools_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->listType = type;
        data->queueType = desc.queue;
        data->nextFreeIndex = 0;
        data->ownerDevice = device_;
        return handle;
    }

    void D3D12Device::DestroyCommandPoolImpl(CommandPoolHandle h) {
        auto* data = commandPools_.Lookup(h);
        if (!data) {
            return;
        }
        for (auto& entry : data->cachedEntries) {
            if (entry.bufHandle.IsValid()) {
                commandBuffers_.Free(entry.bufHandle);
            }
        }
        data->cachedEntries.clear();
        commandPools_.Free(h);
    }

    void D3D12Device::ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags) {
        auto* data = commandPools_.Lookup(h);
        if (!data) {
            return;
        }
        // Reset each entry's own allocator (per-entry model — D3D12 spec §3.3)
        for (auto& entry : data->cachedEntries) {
            if (entry.allocator) {
                entry.allocator->Reset();
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

    auto D3D12Device::AllocateFromPoolImpl(CommandPoolHandle pool, bool secondary)
        -> RhiResult<CommandListAcquisition> {
        // -------------------------------------------------------------------------
        // Per-entry allocator model (spec §19):
        //   - Each CachedEntry owns its own ID3D12CommandAllocator
        //   - D3D12 requires at most one recording command list per allocator
        //   - ResetCommandPool resets all per-entry allocators + nextFreeIndex to 0
        //   - AllocateFromPool picks cachedEntries[nextFreeIndex++], resets entry's list
        //   - Steady-state: zero allocation, zero Create calls
        //   - Warm-up: first few frames create new allocators + command lists
        // -------------------------------------------------------------------------
        auto* poolData = commandPools_.Lookup(pool);
        if (!poolData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Helper: populate HandlePool data and build acquisition result
        auto buildAcquisition
            = [&](CommandBufferHandle bufHandle, ID3D12GraphicsCommandList7* list,
                  const ComPtr<ID3D12GraphicsCommandList7>& listPtr, const ComPtr<ID3D12CommandAllocator>& allocator,
                  D3D12CommandBuffer* wrapper) -> CommandListAcquisition {
            auto* bufData = commandBuffers_.Lookup(bufHandle);
            bufData->allocator = allocator;
            bufData->list = listPtr;
            bufData->queueType = poolData->queueType;
            bufData->isSecondary = secondary;
            wrapper->Init(this, list, allocator.Get(), poolData->queueType);
            return {.bufferHandle = bufHandle, .listHandle = CommandListHandle(wrapper, BackendType::D3D12)};
        };

        // ----- Fast path: reuse cached entry (steady-state, zero-alloc) -----
        if (poolData->nextFreeIndex < poolData->cachedEntries.size()) {
            auto& entry = poolData->cachedEntries[poolData->nextFreeIndex++];
            if (FAILED(entry.list->Reset(entry.allocator.Get(), nullptr))) {
                return std::unexpected(RhiError::OutOfDeviceMemory);
            }
            return buildAcquisition(
                entry.bufHandle, entry.list.Get(), entry.list, entry.allocator, entry.wrapper.get()
            );
        }

        // ----- Cold path: create new allocator + command list (warm-up only) -----
        auto type = secondary ? D3D12_COMMAND_LIST_TYPE_BUNDLE : poolData->listType;

        ComPtr<ID3D12CommandAllocator> newAllocator;
        if (FAILED(device_->CreateCommandAllocator(type, IID_PPV_ARGS(&newAllocator)))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        ComPtr<ID3D12GraphicsCommandList7> cmdList;
        if (FAILED(device_->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList)))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }
        if (FAILED(cmdList->Reset(newAllocator.Get(), nullptr))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [bufHandle, bufData] = commandBuffers_.Allocate();
        if (!bufData) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        auto wrapper = std::make_unique<D3D12CommandBuffer>();
        auto acq = buildAcquisition(bufHandle, cmdList.Get(), cmdList, newAllocator, wrapper.get());

        poolData->cachedEntries.push_back({
            .allocator = std::move(newAllocator),
            .list = std::move(cmdList),
            .bufHandle = bufHandle,
            .wrapper = std::move(wrapper),
        });
        poolData->nextFreeIndex++;
        return acq;
    }

    void D3D12Device::FreeFromPoolImpl(CommandPoolHandle /*pool*/, const CommandListAcquisition& /*acq*/) {
        // No-op: pool-reset model (spec §19) reclaims all buffers via ResetCommandPool.
        // Cached entries persist until pool destruction.
    }

}  // namespace miki::rhi

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif