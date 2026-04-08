/** @file D3D12Device.cpp
 *  @brief D3D12 (Tier 1) backend — DXGI factory, adapter, device, queues,
 *         D3D12MA allocator, descriptor heaps, capability population,
 *         sync primitives, submit, memory stats.
 */

#include "miki/rhi/backend/D3D12Device.h"
#include "miki/rhi/backend/D3D12CommandBuffer.h"

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wnested-anon-types"
#    pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

#include <D3D12MemAlloc.h>

#include "miki/debug/StructuredLogger.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace miki::rhi {

    // =========================================================================
    // D3D12 debug message helpers
    // =========================================================================

    namespace {
        constexpr auto D3D12SeverityToString(D3D12_MESSAGE_SEVERITY sev) -> std::string_view {
            switch (sev) {
                case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "Corruption";
                case D3D12_MESSAGE_SEVERITY_ERROR: return "Error";
                case D3D12_MESSAGE_SEVERITY_WARNING: return "Warning";
                case D3D12_MESSAGE_SEVERITY_INFO: return "Info";
                case D3D12_MESSAGE_SEVERITY_MESSAGE: return "Message";
                default: return "Unknown";
            }
        }

        constexpr auto D3D12CategoryToString(D3D12_MESSAGE_CATEGORY cat) -> std::string_view {
            switch (cat) {
                case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "Application";
                case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS: return "Miscellaneous";
                case D3D12_MESSAGE_CATEGORY_INITIALIZATION: return "Initialization";
                case D3D12_MESSAGE_CATEGORY_CLEANUP: return "Cleanup";
                case D3D12_MESSAGE_CATEGORY_COMPILATION: return "Compilation";
                case D3D12_MESSAGE_CATEGORY_STATE_CREATION: return "StateCreation";
                case D3D12_MESSAGE_CATEGORY_STATE_SETTING: return "StateSetting";
                case D3D12_MESSAGE_CATEGORY_STATE_GETTING: return "StateGetting";
                case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "ResourceManipulation";
                case D3D12_MESSAGE_CATEGORY_EXECUTION: return "Execution";
                case D3D12_MESSAGE_CATEGORY_SHADER: return "Shader";
                default: return "Unknown";
            }
        }

        void LogD3D12Message(
            D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_ID id,
            const char* description
        ) {
            using enum ::miki::debug::LogCategory;
            auto msg = std::format(
                "[D3D12] [{}:{}] (id={}) {}", D3D12SeverityToString(severity), D3D12CategoryToString(category),
                static_cast<int>(id), description ? description : ""
            );

            switch (severity) {
                case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                case D3D12_MESSAGE_SEVERITY_ERROR: MIKI_LOG_ERROR(Rhi, "{}", msg); break;
                case D3D12_MESSAGE_SEVERITY_WARNING: MIKI_LOG_WARN(Rhi, "{}", msg); break;
                case D3D12_MESSAGE_SEVERITY_INFO: MIKI_LOG_INFO(Rhi, "{}", msg); break;
                case D3D12_MESSAGE_SEVERITY_MESSAGE:
                default: MIKI_LOG_TRACE(Rhi, "{}", msg); break;
            }
            MIKI_LOG_FLUSH();
        }

        void CALLBACK D3D12DebugMessageCallback(
            D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description,
            void* /*context*/
        ) {
            LogD3D12Message(severity, category, id, description);
        }
    }  // namespace

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

        // Try ID3D12Device10 first (Agility SDK / Win11), fallback to ID3D12Device5 (Win10 1809+)
        HRESULT hr = baseDevice.As(&device_);
        if (FAILED(hr)) {
            // ID3D12Device10 not available, try ID3D12Device5 (still supports most features)
            ComPtr<ID3D12Device5> device5;
            hr = baseDevice.As(&device5);
            if (FAILED(hr)) {
                return std::unexpected(RhiError::DeviceLost);
            }
            // Store as ID3D12Device10 pointer (will be null for unsupported methods)
            // We check hasEnhancedBarriers_ before using Device10-specific features
            device_ = reinterpret_cast<ID3D12Device10*>(device5.Detach());
            MIKI_LOG_WARN(
                ::miki::debug::LogCategory::Rhi,
                "[D3D12] ID3D12Device10 not available, using ID3D12Device5 fallback (Enhanced Barriers disabled)"
            );
        }

        // Check for Enhanced Barriers support
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)))) {
            hasEnhancedBarriers_ = options12.EnhancedBarriersSupported;
        }

        // Setup debug message routing
        if (debugController_) {
            SetupInfoQueue();
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
        auto createQueue = [&](D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_QUEUE_PRIORITY priority,
                               ComPtr<ID3D12CommandQueue>& out) -> HRESULT {
            D3D12_COMMAND_QUEUE_DESC desc{};
            desc.Type = type;
            desc.Priority = priority;
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 0;
            return device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&out));
        };

        if (FAILED(
                createQueue(D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, queues_.graphics)
            )) {
            return std::unexpected(RhiError::DeviceLost);
        }
        // Frame-sync compute queue at HIGH priority
        if (FAILED(createQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_QUEUE_PRIORITY_HIGH, queues_.compute))) {
            queues_.compute = queues_.graphics;  // Fallback to graphics queue
        }
        // Async compute queue at NORMAL priority (Level A: physically separate from frame-sync)
        if (queues_.compute.Get() != queues_.graphics.Get()) {
            if (FAILED(createQueue(
                    D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, queues_.computeAsync
                ))) {
                queues_.computeAsync = queues_.compute;  // Fallback: share with frame-sync
            }
        } else {
            queues_.computeAsync = queues_.compute;
        }
        if (FAILED(createQueue(D3D12_COMMAND_LIST_TYPE_COPY, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, queues_.copy))) {
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
    // Per-queue timeline semaphores (specs/03-sync.md §3.2)
    // =========================================================================

    auto D3D12Device::CreateQueueTimelines() -> RhiResult<void> {
        auto makeSem = [&]() -> RhiResult<SemaphoreHandle> {
            return CreateSemaphoreImpl({.type = SemaphoreType::Timeline, .initialValue = 0});
        };

        auto gfx = makeSem();
        if (!gfx) {
            return std::unexpected(gfx.error());
        }
        queueTimelines_.graphics = *gfx;

        if (queues_.compute && queues_.compute.Get() != queues_.graphics.Get()) {
            auto comp = makeSem();
            if (!comp) {
                return std::unexpected(comp.error());
            }
            queueTimelines_.compute = *comp;
        }

        if (queues_.copy && queues_.copy.Get() != queues_.graphics.Get()) {
            auto xfer = makeSem();
            if (!xfer) {
                return std::unexpected(xfer.error());
            }
            queueTimelines_.transfer = *xfer;
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
        capabilities_.hasAsyncTransfer = true;      // D3D12 always has a dedicated copy command queue
        capabilities_.computeQueueFamilyCount = 1;  // D3D12 COMPUTE type is always available
        capabilities_.hasGlobalPriority = true;     // D3D12_COMMAND_QUEUE_PRIORITY_HIGH always available
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
        capabilities_.maxPushConstantSize = kMaxPushConstantSize;  // 64 DWORDs root constants
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

        // Runtime format support probe
        PopulateFormatSupport();
    }

    void D3D12Device::PopulateFormatSupport() {
        static constexpr DXGI_FORMAT kFormatMap[] = {
            DXGI_FORMAT_UNKNOWN,               // Undefined
            DXGI_FORMAT_R8_UNORM,              // R8_UNORM
            DXGI_FORMAT_R8_SNORM,              // R8_SNORM
            DXGI_FORMAT_R8_UINT,               // R8_UINT
            DXGI_FORMAT_R8_SINT,               // R8_SINT
            DXGI_FORMAT_R8G8_UNORM,            // RG8_UNORM
            DXGI_FORMAT_R8G8_SNORM,            // RG8_SNORM
            DXGI_FORMAT_R8G8_UINT,             // RG8_UINT
            DXGI_FORMAT_R8G8_SINT,             // RG8_SINT
            DXGI_FORMAT_R8G8B8A8_UNORM,        // RGBA8_UNORM
            DXGI_FORMAT_R8G8B8A8_SNORM,        // RGBA8_SNORM
            DXGI_FORMAT_R8G8B8A8_UINT,         // RGBA8_UINT
            DXGI_FORMAT_R8G8B8A8_SINT,         // RGBA8_SINT
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,   // RGBA8_SRGB
            DXGI_FORMAT_B8G8R8A8_UNORM,        // BGRA8_UNORM
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,   // BGRA8_SRGB
            DXGI_FORMAT_R16_UNORM,             // R16_UNORM
            DXGI_FORMAT_R16_SNORM,             // R16_SNORM
            DXGI_FORMAT_R16_UINT,              // R16_UINT
            DXGI_FORMAT_R16_SINT,              // R16_SINT
            DXGI_FORMAT_R16_FLOAT,             // R16_FLOAT
            DXGI_FORMAT_R16G16_UNORM,          // RG16_UNORM
            DXGI_FORMAT_R16G16_SNORM,          // RG16_SNORM
            DXGI_FORMAT_R16G16_UINT,           // RG16_UINT
            DXGI_FORMAT_R16G16_SINT,           // RG16_SINT
            DXGI_FORMAT_R16G16_FLOAT,          // RG16_FLOAT
            DXGI_FORMAT_R16G16B16A16_UNORM,    // RGBA16_UNORM
            DXGI_FORMAT_R16G16B16A16_SNORM,    // RGBA16_SNORM
            DXGI_FORMAT_R16G16B16A16_UINT,     // RGBA16_UINT
            DXGI_FORMAT_R16G16B16A16_SINT,     // RGBA16_SINT
            DXGI_FORMAT_R16G16B16A16_FLOAT,    // RGBA16_FLOAT
            DXGI_FORMAT_R32_UINT,              // R32_UINT
            DXGI_FORMAT_R32_SINT,              // R32_SINT
            DXGI_FORMAT_R32_FLOAT,             // R32_FLOAT
            DXGI_FORMAT_R32G32_UINT,           // RG32_UINT
            DXGI_FORMAT_R32G32_SINT,           // RG32_SINT
            DXGI_FORMAT_R32G32_FLOAT,          // RG32_FLOAT
            DXGI_FORMAT_R32G32B32_UINT,        // RGB32_UINT
            DXGI_FORMAT_R32G32B32_SINT,        // RGB32_SINT
            DXGI_FORMAT_R32G32B32_FLOAT,       // RGB32_FLOAT
            DXGI_FORMAT_R32G32B32A32_UINT,     // RGBA32_UINT
            DXGI_FORMAT_R32G32B32A32_SINT,     // RGBA32_SINT
            DXGI_FORMAT_R32G32B32A32_FLOAT,    // RGBA32_FLOAT
            DXGI_FORMAT_R10G10B10A2_UNORM,     // RGB10A2_UNORM
            DXGI_FORMAT_R11G11B10_FLOAT,       // RG11B10_FLOAT
            DXGI_FORMAT_D16_UNORM,             // D16_UNORM
            DXGI_FORMAT_D32_FLOAT,             // D32_FLOAT
            DXGI_FORMAT_D24_UNORM_S8_UINT,     // D24_UNORM_S8_UINT
            DXGI_FORMAT_D32_FLOAT_S8X24_UINT,  // D32_FLOAT_S8_UINT
            DXGI_FORMAT_BC1_UNORM,             // BC1_UNORM
            DXGI_FORMAT_BC1_UNORM_SRGB,        // BC1_SRGB
            DXGI_FORMAT_BC2_UNORM,             // BC2_UNORM
            DXGI_FORMAT_BC2_UNORM_SRGB,        // BC2_SRGB
            DXGI_FORMAT_BC3_UNORM,             // BC3_UNORM
            DXGI_FORMAT_BC3_UNORM_SRGB,        // BC3_SRGB
            DXGI_FORMAT_BC4_UNORM,             // BC4_UNORM
            DXGI_FORMAT_BC4_SNORM,             // BC4_SNORM
            DXGI_FORMAT_BC5_UNORM,             // BC5_UNORM
            DXGI_FORMAT_BC5_SNORM,             // BC5_SNORM
            DXGI_FORMAT_BC6H_UF16,             // BC6H_UFLOAT
            DXGI_FORMAT_BC6H_SF16,             // BC6H_SFLOAT
            DXGI_FORMAT_BC7_UNORM,             // BC7_UNORM
            DXGI_FORMAT_BC7_UNORM_SRGB,        // BC7_SRGB
            DXGI_FORMAT_UNKNOWN,               // ASTC_4x4_UNORM (not supported on D3D12)
            DXGI_FORMAT_UNKNOWN,               // ASTC_4x4_SRGB
        };
        static_assert(std::size(kFormatMap) == GpuCapabilityProfile::kFormatCount);

        for (uint32_t i = 1; i < GpuCapabilityProfile::kFormatCount; ++i) {
            if (kFormatMap[i] == DXGI_FORMAT_UNKNOWN) {
                capabilities_.formatSupport[i] = FormatFeatureFlags::None;
                continue;
            }

            D3D12_FEATURE_DATA_FORMAT_SUPPORT fmtSupport{};
            fmtSupport.Format = kFormatMap[i];
            if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fmtSupport, sizeof(fmtSupport)))) {
                capabilities_.formatSupport[i] = FormatFeatureFlags::None;
                continue;
            }

            auto d3dFlags1 = fmtSupport.Support1;
            FormatFeatureFlags flags = FormatFeatureFlags::None;

            if (d3dFlags1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) {
                flags = flags | FormatFeatureFlags::Sampled;
            }
            if (d3dFlags1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) {
                flags = flags | FormatFeatureFlags::Storage;
            }
            if (d3dFlags1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
                flags = flags | FormatFeatureFlags::ColorAttachment;
            }
            if (d3dFlags1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) {
                flags = flags | FormatFeatureFlags::DepthStencil;
            }
            if (d3dFlags1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) {
                flags = flags | FormatFeatureFlags::BlendSrc;
            }
            if (d3dFlags1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON) {
                flags = flags | FormatFeatureFlags::Filter;
            }

            capabilities_.formatSupport[i] = flags;
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

        auto r5b = CreateQueueTimelines();
        if (!r5b) {
            return r5b;
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

    D3D12Device::D3D12Device() = default;

    D3D12Device::~D3D12Device() {
        if (device_) {
            WaitIdleImpl();
        }

        // Unregister info queue callback before releasing device
        if (infoQueueCallbackCookie_ != 0 && infoQueue_) {
            ComPtr<ID3D12InfoQueue1> iq1;
            if (SUCCEEDED(infoQueue_.As(&iq1))) {
                iq1->UnregisterMessageCallback(infoQueueCallbackCookie_);
            }
            infoQueueCallbackCookie_ = 0;
        }
        // Drain any remaining messages from the polling fallback path
        DrainInfoQueueMessages();
        infoQueue_.Reset();

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
            if (desc.preferAsyncQueue && queues_.computeAsync && queues_.computeAsync.Get() != queues_.compute.Get()) {
                targetQueue = queues_.computeAsync.Get();
            } else {
                targetQueue = queues_.compute.Get();
            }
        } else if (queue == QueueType::Transfer && queues_.copy) {
            targetQueue = queues_.copy.Get();
        }

        // Wait semaphores (GPU-side waits)
        for (auto& w : desc.waitSemaphores) {
            auto* semData = semaphores_.Lookup(w.semaphore);
            if (!semData) {
                continue;
            }
            // D3D12 fences are always timeline; Wait(fence, 0) is always satisfied (no-op).
            // Binary semaphores (value=0) are used for swapchain sync which DXGI handles implicitly.
            if (semData->type == SemaphoreType::Binary || w.value == 0) {
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
            // Skip binary semaphores — D3D12 swapchain sync is implicit via DXGI Present.
            if (semData->type == SemaphoreType::Binary || s.value == 0) {
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

        // Drain debug messages (polling fallback; no-op if real-time callback is active)
        DrainInfoQueueMessages();
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

    // =========================================================================
    // Debug message infrastructure
    // =========================================================================

    void D3D12Device::SetupInfoQueue() {
        if (FAILED(device_.As(&infoQueue_))) {
            return;
        }

        // Break on corruption/error in debugger
        infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);

        // Try real-time callback via ID3D12InfoQueue1 (Agility SDK / Win11)
        ComPtr<ID3D12InfoQueue1> iq1;
        if (SUCCEEDED(infoQueue_.As(&iq1))) {
            HRESULT hr = iq1->RegisterMessageCallback(
                D3D12DebugMessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &infoQueueCallbackCookie_
            );
            if (SUCCEEDED(hr) && infoQueueCallbackCookie_ != 0) {
                MIKI_LOG_INFO(::miki::debug::LogCategory::Rhi, "[D3D12] Real-time debug message callback registered");
                return;  // Callback path active, no need for polling
            }
        }

        // Fallback: polling mode. Allow storage so DrainInfoQueueMessages can retrieve them.
        infoQueue_->SetMuteDebugOutput(FALSE);
        MIKI_LOG_INFO(
            ::miki::debug::LogCategory::Rhi,
            "[D3D12] Using polling fallback for debug messages (ID3D12InfoQueue1 not available)"
        );
    }

    void D3D12Device::DrainInfoQueueMessages() {
        if (!infoQueue_ || infoQueueCallbackCookie_ != 0) {
            return;  // Either no info queue, or real-time callback is handling messages
        }

        UINT64 msgCount = infoQueue_->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < msgCount; ++i) {
            SIZE_T msgLen = 0;
            if (FAILED(infoQueue_->GetMessage(i, nullptr, &msgLen)) || msgLen == 0) {
                continue;
            }

            // Allocate on stack for small messages, heap for large
            constexpr SIZE_T kStackThreshold = 1024;
            alignas(D3D12_MESSAGE) char stackBuf[kStackThreshold];
            std::unique_ptr<char[]> heapBuf;
            D3D12_MESSAGE* msg = nullptr;

            if (msgLen <= kStackThreshold) {
                msg = reinterpret_cast<D3D12_MESSAGE*>(stackBuf);
            } else {
                heapBuf = std::make_unique<char[]>(msgLen);
                msg = reinterpret_cast<D3D12_MESSAGE*>(heapBuf.get());
            }

            if (SUCCEEDED(infoQueue_->GetMessage(i, msg, &msgLen))) {
                LogD3D12Message(msg->Severity, msg->Category, msg->ID, msg->pDescription);
            }
        }
        infoQueue_->ClearStoredMessages();
    }

}  // namespace miki::rhi

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif
