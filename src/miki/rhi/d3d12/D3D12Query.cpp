/** @file D3D12Query.cpp
 *  @brief D3D12 (Tier 1) backend — QueryPool creation, results retrieval,
 *         timestamp period, command buffer creation/destruction.
 */

#include "miki/rhi/backend/D3D12Device.h"

#include "miki/rhi/backend/D3D12CommandBuffer.h"

#include <algorithm>

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
    // Command buffer creation/destruction
    // =========================================================================

    auto D3D12Device::CreateCommandBufferImpl(const CommandBufferDesc& desc) -> RhiResult<CommandBufferHandle> {
        D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (desc.type == QueueType::Compute) {
            type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        }
        if (desc.type == QueueType::Transfer) {
            type = D3D12_COMMAND_LIST_TYPE_COPY;
        }
        if (desc.secondary) {
            type = D3D12_COMMAND_LIST_TYPE_BUNDLE;
        }

        ComPtr<ID3D12CommandAllocator> allocator;
        HRESULT hr = device_->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        ComPtr<ID3D12GraphicsCommandList7> cmdList;
        hr = device_->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = commandBuffers_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->allocator = std::move(allocator);
        data->list = std::move(cmdList);
        data->queueType = desc.type;
        data->isSecondary = desc.secondary;
        return handle;
    }

    void D3D12Device::DestroyCommandBufferImpl(CommandBufferHandle h) {
        auto* data = commandBuffers_.Lookup(h);
        if (!data) {
            return;
        }
        commandBuffers_.Free(h);
    }

    // =========================================================================
    // Command list acquisition (unified factory)
    // =========================================================================

    auto D3D12Device::AcquireCommandListImpl(QueueType queue) -> RhiResult<CommandListAcquisition> {
        CommandBufferDesc desc{.type = queue, .secondary = false};
        auto bufResult = CreateCommandBufferImpl(desc);
        if (!bufResult) {
            return std::unexpected(bufResult.error());
        }

        auto bufHandle = *bufResult;
        auto* data = commandBuffers_.Lookup(bufHandle);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        auto& cmdBuf = commandListArena_.emplace_back(std::make_unique<D3D12CommandBuffer>());
        cmdBuf->Init(this, data->list.Get(), data->allocator.Get(), queue);

        CommandListHandle listHandle(cmdBuf.get(), BackendType::D3D12);
        return CommandListAcquisition{.bufferHandle = bufHandle, .listHandle = listHandle};
    }

    void D3D12Device::ReleaseCommandListImpl(const CommandListAcquisition& acq) {
        void* raw = acq.listHandle.GetRawPtr();
        auto it = std::find_if(commandListArena_.begin(), commandListArena_.end(), [raw](const auto& p) {
            return p.get() == raw;
        });
        if (it != commandListArena_.end()) {
            commandListArena_.erase(it);
        }
        DestroyCommandBufferImpl(acq.bufferHandle);
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

        ComPtr<ID3D12CommandAllocator> allocator;
        HRESULT hr = device_->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = commandPools_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->allocator = std::move(allocator);
        data->listType = type;
        data->queueType = desc.queue;
        data->nextFreeList = 0;
        return handle;
    }

    void D3D12Device::DestroyCommandPoolImpl(CommandPoolHandle h) {
        auto* data = commandPools_.Lookup(h);
        if (!data) {
            return;
        }
        data->cachedLists.clear();
        data->allocator.Reset();
        commandPools_.Free(h);
    }

    void D3D12Device::ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags) {
        auto* data = commandPools_.Lookup(h);
        if (!data) {
            return;
        }
        data->allocator->Reset();
        if (static_cast<uint8_t>(flags) & static_cast<uint8_t>(CommandPoolResetFlags::ReleaseResources)) {
            data->cachedLists.clear();
        }
        data->nextFreeList = 0;
    }

    auto D3D12Device::AllocateFromPoolImpl(CommandPoolHandle pool, bool secondary)
        -> RhiResult<CommandListAcquisition> {
        auto* poolData = commandPools_.Lookup(pool);
        if (!poolData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        ComPtr<ID3D12GraphicsCommandList7> cmdList;
        if (poolData->nextFreeList < poolData->cachedLists.size()) {
            cmdList = poolData->cachedLists[poolData->nextFreeList];
            HRESULT hr = cmdList->Reset(poolData->allocator.Get(), nullptr);
            if (FAILED(hr)) {
                return std::unexpected(RhiError::OutOfDeviceMemory);
            }
        } else {
            auto type = secondary ? D3D12_COMMAND_LIST_TYPE_BUNDLE : poolData->listType;
            HRESULT hr = device_->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));
            if (FAILED(hr)) {
                return std::unexpected(RhiError::OutOfDeviceMemory);
            }
            poolData->cachedLists.push_back(cmdList);
        }
        poolData->nextFreeList++;

        auto [bufHandle, bufData] = commandBuffers_.Allocate();
        if (!bufData) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        bufData->allocator = poolData->allocator;
        bufData->list = cmdList;
        bufData->queueType = poolData->queueType;
        bufData->isSecondary = secondary;

        auto& cmdBuf = commandListArena_.emplace_back(std::make_unique<D3D12CommandBuffer>());
        cmdBuf->Init(this, cmdList.Get(), poolData->allocator.Get(), poolData->queueType);

        CommandListHandle listHandle(cmdBuf.get(), BackendType::D3D12);
        return CommandListAcquisition{.bufferHandle = bufHandle, .listHandle = listHandle};
    }

    void D3D12Device::FreeFromPoolImpl(CommandPoolHandle /*pool*/, const CommandListAcquisition& acq) {
        void* raw = acq.listHandle.GetRawPtr();
        auto it = std::find_if(commandListArena_.begin(), commandListArena_.end(), [raw](const auto& p) {
            return p.get() == raw;
        });
        if (it != commandListArena_.end()) {
            commandListArena_.erase(it);
        }
        if (acq.bufferHandle.IsValid()) {
            commandBuffers_.Free(acq.bufferHandle);
        }
    }

}  // namespace miki::rhi

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif