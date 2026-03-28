/** @file D3D12Device.cpp
 *  @brief D3D12 (Tier 1) backend — DXGI factory, adapter, device, queues,
 *         D3D12MA allocator, descriptor heaps, capability population,
 *         sync primitives, submit, memory stats.
 */

#include "miki/rhi/backend/D3D12Device.h"

#include <D3D12MemAlloc.h>

#include "miki/debug/StructuredLogger.h"

#include <algorithm>
#include <cstring>

namespace miki::rhi {

    // =========================================================================
    // DXGI Factory creation
    // =========================================================================

    auto D3D12Device::CreateFactory(bool enableValidation) -> RhiResult<void> {
        UINT dxgiFlags = 0;

        if (enableValidation) {
            ComPtr<ID3D12Debug> debug0;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug0)))) {
                debug0->EnableDebugLayer();
                dxgiFlags = DXGI_CREATE_FACTORY_DEBUG;

                ComPtr<ID3D12Debug6> debug6;
                if (SUCCEEDED(debug0.As(&debug6))) {
                    debug6->SetEnableGPUBasedValidation(FALSE);
                    debug6->SetEnableSynchronizedCommandQueueValidation(TRUE);
                    debugController_ = debug6;
                }
            }
        }

        HRESULT hr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory_));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::DeviceLost);
        }
        return {};
    }

    // =========================================================================
    // Adapter selection
    // =========================================================================

    auto D3D12Device::SelectAdapter(uint32_t adapterIndex) -> RhiResult<void> {
        ComPtr<IDXGIAdapter1> adapter1;

        // Try GPU preference (high performance) first
        HRESULT hr = factory_->EnumAdapterByGpuPreference(
            adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter1)
        );
        if (FAILED(hr)) {
            hr = factory_->EnumAdapters1(adapterIndex, &adapter1);
        }
        if (FAILED(hr)) {
            return std::unexpected(RhiError::DeviceLost);
        }

        hr = adapter1.As(&adapter_);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::DeviceLost);
        }

        // Verify D3D12 support
        hr = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::DeviceLost);
        }

        return {};
    }

    // =========================================================================
    // Device creation
    // =========================================================================

    auto D3D12Device::CreateDevice() -> RhiResult<void> {
        // Try highest feature level first, fallback progressively
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_12_2,
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
        };

        ComPtr<ID3D12Device> baseDevice;
        for (auto fl : featureLevels) {
            HRESULT hr = D3D12CreateDevice(adapter_.Get(), fl, IID_PPV_ARGS(&baseDevice));
            if (SUCCEEDED(hr)) {
                featureLevel_ = fl;
                break;
            }
        }
        if (!baseDevice) {
            return std::unexpected(RhiError::DeviceLost);
        }

        HRESULT hr = baseDevice.As(&device_);
        if (FAILED(hr)) {
            // Fall back: ID3D12Device10 not available, try lower interface
            // This shouldn't happen with Agility SDK but handle gracefully
            return std::unexpected(RhiError::DeviceLost);
        }

        // Check for Enhanced Barriers support
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)))) {
            hasEnhancedBarriers_ = options12.EnhancedBarriersSupported;
        }

        // Set info queue break on errors in debug mode
        if (debugController_) {
            ComPtr<ID3D12InfoQueue> infoQueue;
            if (SUCCEEDED(device_.As(&infoQueue))) {
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            }
        }

        return {};
    }

    // =========================================================================
    // D3D12MA allocator
    // =========================================================================

    auto D3D12Device::CreateAllocator() -> RhiResult<void> {
        D3D12MA::ALLOCATOR_DESC allocDesc{};
        allocDesc.pDevice = device_.Get();
        allocDesc.pAdapter = adapter_.Get();
        allocDesc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;

        HRESULT hr = D3D12MA::CreateAllocator(&allocDesc, &allocator_);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }
        return {};
    }

    // =========================================================================
    // Command queue creation
    // =========================================================================

    auto D3D12Device::CreateQueues() -> RhiResult<void> {
        auto createQueue = [&](D3D12_COMMAND_LIST_TYPE type, ComPtr<ID3D12CommandQueue>& out) -> HRESULT {
            D3D12_COMMAND_QUEUE_DESC desc{};
            desc.Type = type;
            desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 0;
            return device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&out));
        };

        if (FAILED(createQueue(D3D12_COMMAND_LIST_TYPE_DIRECT, queues_.graphics))) {
            return std::unexpected(RhiError::DeviceLost);
        }
        if (FAILED(createQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE, queues_.compute))) {
            queues_.compute = queues_.graphics;  // Fallback to graphics queue
        }
        if (FAILED(createQueue(D3D12_COMMAND_LIST_TYPE_COPY, queues_.copy))) {
            queues_.copy = queues_.graphics;  // Fallback to graphics queue
        }

        // Create frame fence for WaitIdle
        HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frameFence_));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }
        frameFenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!frameFenceEvent_) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }

        return {};
    }

    // =========================================================================
    // Descriptor heap creation
    // =========================================================================

    auto D3D12Device::CreateDescriptorHeaps() -> RhiResult<void> {
        auto createHeap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool shaderVisible,
                              D3D12DescriptorHeapAllocator& out) -> HRESULT {
            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            desc.Type = type;
            desc.NumDescriptors = count;
            desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 0;
            HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&out.heap));
            if (SUCCEEDED(hr)) {
                out.capacity = count;
                out.allocatedCount = 0;
                out.descriptorSize = device_->GetDescriptorHandleIncrementSize(type);
            }
            return hr;
        };

        // CPU-only heaps for RTV/DSV
        if (FAILED(createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1024, false, rtvHeap_))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }
        if (FAILED(createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256, false, dsvHeap_))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        // Shader-visible heaps
        if (FAILED(createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000, true, shaderVisibleCbvSrvUav_))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }
        if (FAILED(createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true, shaderVisibleSampler_))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        // CPU-only staging heaps for descriptor copies
        if (FAILED(createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, false, stagingCbvSrvUav_))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }
        if (FAILED(createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, false, stagingSampler_))) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        return {};
    }

    // =========================================================================
    // Capability population
    // =========================================================================

    void D3D12Device::PopulateCapabilities() {
        capabilities_.tier = CapabilityTier::Tier1_D3D12;
        capabilities_.backendType = BackendType::D3D12;

        DXGI_ADAPTER_DESC3 adapterDesc{};
        adapter_->GetDesc3(&adapterDesc);

        // Convert wide string device name to narrow
        char name[128]{};
        wcstombs(name, adapterDesc.Description, sizeof(name) - 1);
        capabilities_.deviceName = name;
        capabilities_.vendorId = adapterDesc.VendorId;
        capabilities_.deviceId = adapterDesc.DeviceId;

        // Memory
        capabilities_.deviceLocalMemoryBytes = adapterDesc.DedicatedVideoMemory;
        capabilities_.hostVisibleMemoryBytes = adapterDesc.SharedSystemMemory;

        // Feature level determines many capabilities
        capabilities_.hasTimelineSemaphore = true;  // D3D12 fences are inherently timeline
        capabilities_.hasAsyncCompute = (queues_.compute.Get() != queues_.graphics.Get());
        capabilities_.hasMultiDrawIndirect = true;
        capabilities_.hasMultiDrawIndirectCount = true;  // ExecuteIndirect
        capabilities_.hasBindless = true;
        capabilities_.hasSubgroupOps = true;

        // Shader Model
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_8};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))) {
            capabilities_.hasFloat64 = true;
        }

        // Mesh shader
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)))) {
            capabilities_.hasMeshShader = (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);
            capabilities_.hasTaskShader = capabilities_.hasMeshShader;
            if (capabilities_.hasMeshShader) {
                capabilities_.enabledFeatures.Add(DeviceFeature::MeshShader);
            }
        }

        // Ray tracing
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
            capabilities_.hasRayQuery = (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1);
            capabilities_.hasRayTracingPipeline = (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);
            capabilities_.hasAccelerationStructure = capabilities_.hasRayTracingPipeline;
            if (capabilities_.hasRayTracingPipeline) {
                capabilities_.enabledFeatures.Add(DeviceFeature::RayTracingPipeline);
            }
        }

        // Variable rate shading
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6)))) {
            capabilities_.hasVariableRateShading
                = (options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2);
            if (capabilities_.hasVariableRateShading) {
                capabilities_.enabledFeatures.Add(DeviceFeature::VariableRateShading);
            }
        }

        // Tiled resources (sparse binding)
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
            capabilities_.hasSparseBinding = (options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_1);
            if (capabilities_.hasSparseBinding) {
                capabilities_.enabledFeatures.Add(DeviceFeature::SparseBinding);
            }
        }

        // Work graphs
        D3D12_FEATURE_DATA_D3D12_OPTIONS21 options21{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &options21, sizeof(options21)))) {
            capabilities_.hasWorkGraphs = (options21.WorkGraphsTier >= D3D12_WORK_GRAPHS_TIER_1_0);
        }

        // Limits
        capabilities_.maxTextureSize2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        capabilities_.maxTextureSizeCube = D3D12_REQ_TEXTURECUBE_DIMENSION;
        capabilities_.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        capabilities_.maxViewports = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        capabilities_.maxPushConstantSize = 64 * sizeof(uint32_t);  // 64 DWORDs root constants
        capabilities_.maxBoundDescriptorSets = 4;
        capabilities_.maxDrawIndirectCount = UINT32_MAX;
        capabilities_.maxStorageBufferSize = UINT64_MAX;
        capabilities_.maxFramebufferWidth = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        capabilities_.maxFramebufferHeight = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;

        capabilities_.maxComputeWorkGroupCount = {
            D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
            D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
            D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
        };
        capabilities_.maxComputeWorkGroupSize = {
            D3D12_CS_THREAD_GROUP_MAX_X,
            D3D12_CS_THREAD_GROUP_MAX_Y,
            D3D12_CS_THREAD_GROUP_MAX_Z,
        };

        // Subgroup size: NVIDIA=32, AMD=64, Intel=varies
        capabilities_.subgroupSize = 32;  // Conservative default
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)))) {
            capabilities_.subgroupSize = options1.WaveLaneCountMin;
        }

        // Descriptor model
        capabilities_.descriptorModel = DescriptorModel::DescriptorHeap;

        // ReBAR detection
        capabilities_.hasResizableBAR
            = (adapterDesc.SharedSystemMemory > 0 && adapterDesc.DedicatedVideoMemory > 256ULL * 1024 * 1024);

        // Memory budget query — always available on D3D12 via DXGI
        capabilities_.hasMemoryBudgetQuery = true;

        // Always-on features
        capabilities_.enabledFeatures.Add(DeviceFeature::Present);
        capabilities_.enabledFeatures.Add(DeviceFeature::TimelineSemaphore);
        capabilities_.enabledFeatures.Add(DeviceFeature::DynamicRendering);
        if (capabilities_.hasAsyncCompute) {
            capabilities_.enabledFeatures.Add(DeviceFeature::AsyncCompute);
        }
    }

    // =========================================================================
    // Init / Destroy
    // =========================================================================

    auto D3D12Device::Init(const D3D12DeviceDesc& desc) -> RhiResult<void> {
        auto r1 = CreateFactory(desc.enableValidation);
        if (!r1) {
            return r1;
        }

        auto r2 = SelectAdapter(desc.adapterIndex);
        if (!r2) {
            return r2;
        }

        auto r3 = CreateDevice();
        if (!r3) {
            return r3;
        }

        auto r4 = CreateAllocator();
        if (!r4) {
            return r4;
        }

        auto r5 = CreateQueues();
        if (!r5) {
            return r5;
        }

        auto r6 = CreateDescriptorHeaps();
        if (!r6) {
            return r6;
        }

        PopulateCapabilities();

        MIKI_LOG_INFO(
            ::miki::debug::LogCategory::Rhi, "[D3D12] Device initialized: {} (FL {})", capabilities_.deviceName,
            static_cast<int>(featureLevel_)
        );

        return {};
    }

    D3D12Device::~D3D12Device() {
        if (device_) {
            WaitIdleImpl();
        }

        if (frameFenceEvent_) {
            CloseHandle(frameFenceEvent_);
            frameFenceEvent_ = nullptr;
        }

        if (allocator_) {
            allocator_->Release();
            allocator_ = nullptr;
        }

        // ComPtr members auto-release in reverse declaration order
    }

    // =========================================================================
    // Sync primitives — HandlePool-based
    // =========================================================================

    auto D3D12Device::CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle> {
        ComPtr<ID3D12Fence> fence;
        HRESULT hr = device_->CreateFence(signaled ? 1 : 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }

        auto [handle, data] = fences_.Allocate();
        if (!data) {
            CloseHandle(event);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->fence = std::move(fence);
        data->value = signaled ? 1 : 0;
        data->event = event;
        return handle;
    }

    void D3D12Device::DestroyFenceImpl(FenceHandle h) {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->event) {
            CloseHandle(data->event);
        }
        fences_.Free(h);
    }

    void D3D12Device::WaitFenceImpl(FenceHandle h, uint64_t timeout) {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return;
        }

        if (data->fence->GetCompletedValue() < data->value) {
            data->fence->SetEventOnCompletion(data->value, data->event);
            DWORD ms = (timeout == UINT64_MAX) ? INFINITE : static_cast<DWORD>(timeout / 1000000);  // ns -> ms
            WaitForSingleObject(data->event, ms);
        }
    }

    void D3D12Device::ResetFenceImpl(FenceHandle h) {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return;
        }
        // D3D12 fences don't have a reset concept — they are monotonic.
        // The closest equivalent is signaling with 0, but that's not valid
        // for timeline semantics. We increment the expected value instead.
        data->value = data->fence->GetCompletedValue();
    }

    auto D3D12Device::GetFenceStatusImpl(FenceHandle h) -> bool {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return false;
        }
        return data->fence->GetCompletedValue() >= data->value;
    }

    auto D3D12Device::CreateSemaphoreImpl(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle> {
        ComPtr<ID3D12Fence> fence;
        HRESULT hr = device_->CreateFence(desc.initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = semaphores_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->fence = std::move(fence);
        data->value = desc.initialValue;
        data->type = desc.type;
        return handle;
    }

    void D3D12Device::DestroySemaphoreImpl(SemaphoreHandle h) {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return;
        }
        semaphores_.Free(h);
    }

    void D3D12Device::SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value) {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return;
        }
        data->fence->Signal(value);
        data->value = value;
    }

    void D3D12Device::WaitSemaphoreImpl(SemaphoreHandle h, uint64_t value, uint64_t timeout) {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return;
        }

        if (data->fence->GetCompletedValue() < value) {
            HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!event) {
                return;
            }
            data->fence->SetEventOnCompletion(value, event);
            DWORD ms = (timeout == UINT64_MAX) ? INFINITE : static_cast<DWORD>(timeout / 1000000);
            WaitForSingleObject(event, ms);
            CloseHandle(event);
        }
    }

    auto D3D12Device::GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return 0;
        }
        return data->fence->GetCompletedValue();
    }

    void D3D12Device::WaitIdleImpl() {
        if (!device_ || !queues_.graphics) {
            return;
        }

        ++frameFenceValue_;
        queues_.graphics->Signal(frameFence_.Get(), frameFenceValue_);

        if (queues_.compute && queues_.compute.Get() != queues_.graphics.Get()) {
            queues_.compute->Signal(frameFence_.Get(), frameFenceValue_);
        }
        if (queues_.copy && queues_.copy.Get() != queues_.graphics.Get()) {
            queues_.copy->Signal(frameFence_.Get(), frameFenceValue_);
        }

        if (frameFence_->GetCompletedValue() < frameFenceValue_) {
            frameFence_->SetEventOnCompletion(frameFenceValue_, frameFenceEvent_);
            WaitForSingleObject(frameFenceEvent_, INFINITE);
        }
    }

    // =========================================================================
    // Submit
    // =========================================================================

    void D3D12Device::SubmitImpl(QueueType queue, const SubmitDesc& desc) {
        ID3D12CommandQueue* targetQueue = queues_.graphics.Get();
        if (queue == QueueType::Compute && queues_.compute) {
            targetQueue = queues_.compute.Get();
        } else if (queue == QueueType::Transfer && queues_.copy) {
            targetQueue = queues_.copy.Get();
        }

        // Wait semaphores (GPU-side waits)
        for (auto& w : desc.waitSemaphores) {
            auto* semData = semaphores_.Lookup(w.semaphore);
            if (!semData) {
                continue;
            }
            targetQueue->Wait(semData->fence.Get(), w.value);
        }

        // Collect command lists
        std::vector<ID3D12CommandList*> cmdLists;
        cmdLists.reserve(desc.commandBuffers.size());
        for (auto h : desc.commandBuffers) {
            auto* data = commandBuffers_.Lookup(h);
            if (!data) {
                continue;
            }
            cmdLists.push_back(data->list.Get());
        }

        if (!cmdLists.empty()) {
            targetQueue->ExecuteCommandLists(static_cast<UINT>(cmdLists.size()), cmdLists.data());
        }

        // Signal semaphores
        for (auto& s : desc.signalSemaphores) {
            auto* semData = semaphores_.Lookup(s.semaphore);
            if (!semData) {
                continue;
            }
            targetQueue->Signal(semData->fence.Get(), s.value);
            semData->value = s.value;
        }

        // Signal fence
        if (desc.signalFence.IsValid()) {
            auto* fenceData = fences_.Lookup(desc.signalFence);
            if (fenceData) {
                ++fenceData->value;
                targetQueue->Signal(fenceData->fence.Get(), fenceData->value);
            }
        }
    }

    // =========================================================================
    // Memory stats
    // =========================================================================

    auto D3D12Device::GetMemoryStatsImpl() const -> MemoryStats {
        if (!allocator_) {
            return {};
        }

        D3D12MA::TotalStatistics stats{};
        allocator_->CalculateStatistics(&stats);

        MemoryStats result{};
        result.totalAllocationCount = static_cast<uint32_t>(stats.Total.Stats.AllocationCount);
        result.totalAllocatedBytes = stats.Total.Stats.AllocationBytes;
        result.totalUsedBytes = stats.Total.Stats.AllocationBytes;
        result.heapCount = 2;  // Video memory + system memory
        return result;
    }

    auto D3D12Device::GetMemoryHeapBudgetsImpl(std::span<MemoryHeapBudget> out) const -> uint32_t {
        if (!adapter_ || out.empty()) {
            return 0;
        }

        DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
        DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo{};
        adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo);
        adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo);

        uint32_t count = 0;
        if (count < out.size()) {
            out[count].heapIndex = 0;
            out[count].budgetBytes = localInfo.Budget;
            out[count].usageBytes = localInfo.CurrentUsage;
            out[count].isDeviceLocal = true;
            ++count;
        }
        if (count < out.size()) {
            out[count].heapIndex = 1;
            out[count].budgetBytes = nonLocalInfo.Budget;
            out[count].usageBytes = nonLocalInfo.CurrentUsage;
            out[count].isDeviceLocal = false;
            ++count;
        }
        return count;
    }

}  // namespace miki::rhi
