/** @file WebGPUDevice.cpp
 *  @brief WebGPU / Dawn (Tier 3) backend — device init, sync, submit, memory stats.
 */

#include "miki/rhi/backend/WebGPUDevice.h"
#include "miki/rhi/backend/WebGPUCommandBuffer.h"
#include "miki/debug/StructuredLogger.h"

#include <dawn/webgpu.h>
#include <cstring>

#ifndef EMSCRIPTEN
#    include <GLFW/glfw3.h>
#endif

namespace miki::rhi {

    // =========================================================================
    // Lifecycle
    // =========================================================================

    WebGPUDevice::WebGPUDevice() = default;

    WebGPUDevice::~WebGPUDevice() {
        if (device_) {
            WaitIdleImpl();
        }

        if (pushConstantBindGroup_) {
            wgpuBindGroupRelease(pushConstantBindGroup_);
        }
        if (pushConstantBindGroupLayout_) {
            wgpuBindGroupLayoutRelease(pushConstantBindGroupLayout_);
        }
        if (pushConstantBuffer_) {
            wgpuBufferDestroy(pushConstantBuffer_);
            wgpuBufferRelease(pushConstantBuffer_);
        }

        if (queue_) {
            wgpuQueueRelease(queue_);
        }
        if (device_) {
            wgpuDeviceDestroy(device_);
            wgpuDeviceRelease(device_);
        }
        if (ownsAdapter_ && adapter_) {
            wgpuAdapterRelease(adapter_);
        }
        if (ownsInstance_ && instance_) {
            wgpuInstanceRelease(instance_);
        }
    }

    auto WebGPUDevice::Init(const WebGPUDeviceDesc& desc) -> RhiResult<void> {
        // --- Instance ---
        if (desc.instance) {
            instance_ = desc.instance;
            ownsInstance_ = false;
        } else {
            WGPUInstanceDescriptor instanceDesc{};
            instanceDesc.nextInChain = nullptr;
            instance_ = wgpuCreateInstance(&instanceDesc);
            if (!instance_) {
                MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "WebGPU: wgpuCreateInstance failed");
                return std::unexpected(RhiError::DeviceLost);
            }
            ownsInstance_ = true;
        }

        // --- Adapter ---
        if (desc.adapter) {
            adapter_ = desc.adapter;
            ownsAdapter_ = false;
        } else {
            WGPURequestAdapterOptions adapterOpts{};
            adapterOpts.nextInChain = nullptr;
            adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;
            adapterOpts.compatibleSurface = desc.surface;

            struct AdapterCallbackData {
                WGPUAdapter adapter = nullptr;
                bool done = false;
            } cbData;

            WGPURequestAdapterCallbackInfo adapterCbInfo{};
            adapterCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
            adapterCbInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                                        void* userdata1, void*) {
                auto* data = static_cast<AdapterCallbackData*>(userdata1);
                if (status == WGPURequestAdapterStatus_Success) {
                    data->adapter = adapter;
                } else {
                    MIKI_LOG_ERROR(
                        ::miki::debug::LogCategory::Rhi, "WebGPU: adapter request failed: {}",
                        std::string_view(message.data, message.length)
                    );
                }
                data->done = true;
            };
            adapterCbInfo.userdata1 = &cbData;

            wgpuInstanceRequestAdapter(instance_, &adapterOpts, adapterCbInfo);

            // Pump events until callback fires (needed for Dawn native)
#ifndef EMSCRIPTEN
            while (!cbData.done) {
                wgpuInstanceProcessEvents(instance_);
            }
#endif
            if (!cbData.adapter) {
                MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "WebGPU: no suitable adapter found");
                return std::unexpected(RhiError::DeviceLost);
            }
            adapter_ = cbData.adapter;
            ownsAdapter_ = true;
        }

        // --- Device ---
        {
            // Query adapter limits to request them on the device
            WGPULimits adapterLimits{};
            wgpuAdapterGetLimits(adapter_, &adapterLimits);

            WGPUDeviceDescriptor deviceDesc{};
            deviceDesc.nextInChain = nullptr;
            deviceDesc.requiredLimits = &adapterLimits;
            deviceDesc.label = {.data = "miki WebGPU Device", .length = WGPU_STRLEN};

            // Enable timestamp query if available
            WGPUFeatureName timestampFeature = WGPUFeatureName_TimestampQuery;
            if (wgpuAdapterHasFeature(adapter_, timestampFeature)) {
                deviceDesc.requiredFeatureCount = 1;
                deviceDesc.requiredFeatures = &timestampFeature;
            }

            // Uncaptured error callback (set on device descriptor in Dawn 7187+)
            deviceDesc.uncapturedErrorCallbackInfo.callback
                = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
                      const char* typeStr = "Unknown";
                      switch (type) {
                          case WGPUErrorType_Validation: typeStr = "Validation"; break;
                          case WGPUErrorType_OutOfMemory: typeStr = "OutOfMemory"; break;
                          case WGPUErrorType_Internal: typeStr = "Internal"; break;
                          default: break;
                      }
                      MIKI_LOG_ERROR(
                          ::miki::debug::LogCategory::Rhi, "WebGPU {}: {}", typeStr,
                          std::string_view(message.data, message.length)
                      );
                  };

            struct DeviceCallbackData {
                WGPUDevice device = nullptr;
                bool done = false;
            } cbData;

            WGPURequestDeviceCallbackInfo deviceCbInfo{};
            deviceCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
            deviceCbInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                                       void* userdata1, void*) {
                auto* data = static_cast<DeviceCallbackData*>(userdata1);
                if (status == WGPURequestDeviceStatus_Success) {
                    data->device = device;
                } else {
                    MIKI_LOG_ERROR(
                        ::miki::debug::LogCategory::Rhi, "WebGPU: device request failed: {}",
                        std::string_view(message.data, message.length)
                    );
                }
                data->done = true;
            };
            deviceCbInfo.userdata1 = &cbData;

            wgpuAdapterRequestDevice(adapter_, &deviceDesc, deviceCbInfo);

#ifndef EMSCRIPTEN
            while (!cbData.done) {
                wgpuInstanceProcessEvents(instance_);
            }
#endif
            if (!cbData.device) {
                MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "WebGPU: device creation failed");
                return std::unexpected(RhiError::DeviceLost);
            }
            device_ = cbData.device;
        }

        // --- Queue ---
        queue_ = wgpuDeviceGetQueue(device_);
        if (!queue_) {
            MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "WebGPU: failed to get device queue");
            return std::unexpected(RhiError::DeviceLost);
        }

        // --- Capabilities ---
        PopulateCapabilities();

        // --- Push constant emulation resources ---
        CreatePushConstantResources();

        MIKI_LOG_INFO(::miki::debug::LogCategory::Rhi, "WebGPU (Dawn) device initialized successfully");
        return {};
    }

    // =========================================================================
    // Capabilities
    // =========================================================================

    void WebGPUDevice::PopulateCapabilities() {
        auto& cap = capabilities_;
        cap.tier = CapabilityTier::Tier3_WebGPU;

        WGPULimits lim{};
        wgpuDeviceGetLimits(device_, &lim);

        // Geometry
        cap.hasMeshShader = false;
        cap.hasTaskShader = false;
        cap.hasMultiDrawIndirect = false;
        cap.hasMultiDrawIndirectCount = false;
        cap.maxDrawIndirectCount = 1;

        // Descriptors
        cap.descriptorModel = DescriptorModel::BindGroup;
        cap.hasBindless = false;
        cap.hasPushDescriptors = false;
        cap.maxPushConstantSize = 256;  // Emulated via UBO
        cap.maxBoundDescriptorSets = lim.maxBindGroups;
        cap.maxStorageBufferSize = lim.maxStorageBufferBindingSize;

        // Compute
        cap.hasAsyncCompute = false;
        cap.hasTimelineSemaphore = false;
        cap.maxComputeWorkGroupCount[0] = lim.maxComputeWorkgroupsPerDimension;
        cap.maxComputeWorkGroupCount[1] = lim.maxComputeWorkgroupsPerDimension;
        cap.maxComputeWorkGroupCount[2] = lim.maxComputeWorkgroupsPerDimension;
        cap.maxComputeWorkGroupSize[0] = lim.maxComputeWorkgroupSizeX;
        cap.maxComputeWorkGroupSize[1] = lim.maxComputeWorkgroupSizeY;
        cap.maxComputeWorkGroupSize[2] = lim.maxComputeWorkgroupSizeZ;

        // Ray tracing
        cap.hasRayQuery = false;
        cap.hasRayTracingPipeline = false;
        cap.hasAccelerationStructure = false;

        // Shading
        cap.hasVariableRateShading = false;
        cap.hasFloat64 = false;
        cap.hasInt64Atomics = false;
        cap.hasSubgroupOps = false;
        cap.subgroupSize = 0;

        // Memory
        cap.deviceLocalMemoryBytes = 0;  // No memory introspection in WebGPU
        cap.hostVisibleMemoryBytes = 0;
        cap.hasResizableBAR = false;
        cap.hasSparseBinding = false;
        cap.hasHardwareDecompression = false;
        cap.hasMemoryBudgetQuery = false;
        cap.hasWorkGraphs = false;
        cap.hasCooperativeMatrix = false;

        // Limits
        cap.maxColorAttachments = lim.maxColorAttachments;
        cap.maxTextureSize2D = lim.maxTextureDimension2D;
        cap.maxTextureSizeCube = lim.maxTextureDimension2D;
        cap.maxFramebufferWidth = lim.maxTextureDimension2D;
        cap.maxFramebufferHeight = lim.maxTextureDimension2D;
        cap.maxViewports = 1;  // WebGPU supports 1 viewport
        cap.maxClipDistances = 0;

        // Runtime format support probe
        PopulateFormatSupport();
    }

    void WebGPUDevice::PopulateFormatSupport() {
        // WebGPU format support is spec-defined. We set it statically based on the spec,
        // then check optional features (e.g. texture-compression-bc) for compressed formats.
        using F = FormatFeatureFlags;
        constexpr auto SF = F::Sampled | F::Filter;
        constexpr auto SFC = F::Sampled | F::Filter | F::ColorAttachment | F::BlendSrc;
        constexpr auto SFI = F::Sampled | F::ColorAttachment;  // Integer (no filter/blend)
        constexpr auto SFCS = SFC | F::Storage;
        constexpr auto SFIS = SFI | F::Storage;
        constexpr auto DS = F::Sampled | F::Filter | F::DepthStencil;

        auto& fs = capabilities_.formatSupport;
        // 8-bit
        fs[static_cast<uint32_t>(Format::R8_UNORM)] = SFC;
        fs[static_cast<uint32_t>(Format::R8_SNORM)] = SF;  // Not renderable in WebGPU
        fs[static_cast<uint32_t>(Format::R8_UINT)] = SFI;
        fs[static_cast<uint32_t>(Format::R8_SINT)] = SFI;
        fs[static_cast<uint32_t>(Format::RG8_UNORM)] = SFC;
        fs[static_cast<uint32_t>(Format::RG8_SNORM)] = SF;
        fs[static_cast<uint32_t>(Format::RG8_UINT)] = SFI;
        fs[static_cast<uint32_t>(Format::RG8_SINT)] = SFI;
        fs[static_cast<uint32_t>(Format::RGBA8_UNORM)] = SFCS;
        fs[static_cast<uint32_t>(Format::RGBA8_SNORM)] = F::Sampled | F::Filter | F::Storage;
        fs[static_cast<uint32_t>(Format::RGBA8_UINT)] = SFIS;
        fs[static_cast<uint32_t>(Format::RGBA8_SINT)] = SFIS;
        fs[static_cast<uint32_t>(Format::RGBA8_SRGB)] = SFC;
        fs[static_cast<uint32_t>(Format::BGRA8_UNORM)] = SFC;
        fs[static_cast<uint32_t>(Format::BGRA8_SRGB)] = SFC;
        // 16-bit
        fs[static_cast<uint32_t>(Format::R16_UNORM)] = F::None;  // Not in WebGPU spec
        fs[static_cast<uint32_t>(Format::R16_SNORM)] = F::None;
        fs[static_cast<uint32_t>(Format::R16_UINT)] = SFI;
        fs[static_cast<uint32_t>(Format::R16_SINT)] = SFI;
        fs[static_cast<uint32_t>(Format::R16_FLOAT)] = SFC;
        fs[static_cast<uint32_t>(Format::RG16_UNORM)] = F::None;
        fs[static_cast<uint32_t>(Format::RG16_SNORM)] = F::None;
        fs[static_cast<uint32_t>(Format::RG16_UINT)] = SFI;
        fs[static_cast<uint32_t>(Format::RG16_SINT)] = SFI;
        fs[static_cast<uint32_t>(Format::RG16_FLOAT)] = SFC;
        fs[static_cast<uint32_t>(Format::RGBA16_UNORM)] = F::None;
        fs[static_cast<uint32_t>(Format::RGBA16_SNORM)] = F::None;
        fs[static_cast<uint32_t>(Format::RGBA16_UINT)] = SFIS;
        fs[static_cast<uint32_t>(Format::RGBA16_SINT)] = SFIS;
        fs[static_cast<uint32_t>(Format::RGBA16_FLOAT)] = SFCS;
        // 32-bit
        fs[static_cast<uint32_t>(Format::R32_UINT)] = F::Sampled | F::ColorAttachment | F::Storage;
        fs[static_cast<uint32_t>(Format::R32_SINT)] = F::Sampled | F::ColorAttachment | F::Storage;
        fs[static_cast<uint32_t>(Format::R32_FLOAT)] = F::Sampled | F::ColorAttachment | F::Storage;
        fs[static_cast<uint32_t>(Format::RG32_UINT)] = F::Sampled | F::ColorAttachment | F::Storage;
        fs[static_cast<uint32_t>(Format::RG32_SINT)] = F::Sampled | F::ColorAttachment | F::Storage;
        fs[static_cast<uint32_t>(Format::RG32_FLOAT)] = F::Sampled | F::ColorAttachment | F::Storage;
        fs[static_cast<uint32_t>(Format::RGB32_UINT)] = F::None;  // Not in WebGPU spec
        fs[static_cast<uint32_t>(Format::RGB32_SINT)] = F::None;
        fs[static_cast<uint32_t>(Format::RGB32_FLOAT)] = F::None;
        fs[static_cast<uint32_t>(Format::RGBA32_UINT)] = F::Sampled | F::ColorAttachment | F::Storage;
        fs[static_cast<uint32_t>(Format::RGBA32_SINT)] = F::Sampled | F::ColorAttachment | F::Storage;
        fs[static_cast<uint32_t>(Format::RGBA32_FLOAT)] = F::Sampled | F::ColorAttachment | F::Storage;
        // Packed
        fs[static_cast<uint32_t>(Format::RGB10A2_UNORM)] = SFC;
        fs[static_cast<uint32_t>(Format::RG11B10_FLOAT)] = SF;  // Not renderable in base spec
        // Depth/stencil
        fs[static_cast<uint32_t>(Format::D16_UNORM)] = DS;
        fs[static_cast<uint32_t>(Format::D32_FLOAT)] = DS;
        fs[static_cast<uint32_t>(Format::D24_UNORM_S8_UINT)] = DS;
        fs[static_cast<uint32_t>(Format::D32_FLOAT_S8_UINT)] = DS;

        // BC compression — requires "texture-compression-bc" feature
        bool hasBcCompression = wgpuDeviceHasFeature(device_, WGPUFeatureName_TextureCompressionBC);
        FormatFeatureFlags bcFlags = hasBcCompression ? SF : F::None;
        fs[static_cast<uint32_t>(Format::BC1_UNORM)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC1_SRGB)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC2_UNORM)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC2_SRGB)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC3_UNORM)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC3_SRGB)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC4_UNORM)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC4_SNORM)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC5_UNORM)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC5_SNORM)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC6H_UFLOAT)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC6H_SFLOAT)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC7_UNORM)] = bcFlags;
        fs[static_cast<uint32_t>(Format::BC7_SRGB)] = bcFlags;

        // ASTC — requires "texture-compression-astc" feature
        bool hasAstcCompression = wgpuDeviceHasFeature(device_, WGPUFeatureName_TextureCompressionASTC);
        FormatFeatureFlags astcFlags = hasAstcCompression ? SF : F::None;
        fs[static_cast<uint32_t>(Format::ASTC_4x4_UNORM)] = astcFlags;
        fs[static_cast<uint32_t>(Format::ASTC_4x4_SRGB)] = astcFlags;
    }

    // =========================================================================
    // Push constant emulation
    // =========================================================================

    void WebGPUDevice::CreatePushConstantResources() {
        // 256-byte uniform buffer at group(0), binding(0)
        WGPUBufferDescriptor bufDesc{};
        bufDesc.label = {.data = "miki_push_constants", .length = WGPU_STRLEN};
        bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        bufDesc.size = 256;
        bufDesc.mappedAtCreation = false;
        pushConstantBuffer_ = wgpuDeviceCreateBuffer(device_, &bufDesc);

        // Bind group layout: single uniform buffer at binding 0
        WGPUBindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.hasDynamicOffset = false;
        entry.buffer.minBindingSize = 256;

        WGPUBindGroupLayoutDescriptor bglDesc{};
        bglDesc.label = {.data = "miki_push_constant_layout", .length = WGPU_STRLEN};
        bglDesc.entryCount = 1;
        bglDesc.entries = &entry;
        pushConstantBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bglDesc);

        // Bind group
        WGPUBindGroupEntry bgEntry{};
        bgEntry.binding = 0;
        bgEntry.buffer = pushConstantBuffer_;
        bgEntry.offset = 0;
        bgEntry.size = 256;

        WGPUBindGroupDescriptor bgDesc{};
        bgDesc.label = {.data = "miki_push_constant_bind_group", .length = WGPU_STRLEN};
        bgDesc.layout = pushConstantBindGroupLayout_;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        pushConstantBindGroup_ = wgpuDeviceCreateBindGroup(device_, &bgDesc);
    }

    // =========================================================================
    // Synchronization
    // =========================================================================

    auto WebGPUDevice::CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle> {
        auto [handle, data] = fences_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->signaled = signaled;
        data->submittedSerial = 0;
        return handle;
    }

    void WebGPUDevice::DestroyFenceImpl(FenceHandle h) {
        fences_.Free(h);
    }

    void WebGPUDevice::WaitFenceImpl(FenceHandle h, [[maybe_unused]] uint64_t timeout) {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return;
        }
        // Spin-poll device tick until fence is signaled
        while (!data->signaled) {
#ifndef EMSCRIPTEN
            wgpuDeviceTick(device_);
#endif
        }
    }

    void WebGPUDevice::ResetFenceImpl(FenceHandle h) {
        auto* data = fences_.Lookup(h);
        if (data) {
            data->signaled = false;
        }
    }

    auto WebGPUDevice::GetFenceStatusImpl(FenceHandle h) -> bool {
        auto* data = fences_.Lookup(h);
        return data ? data->signaled : false;
    }

    auto WebGPUDevice::CreateSemaphoreImpl(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle> {
        auto [handle, data] = semaphores_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->type = desc.type;
        data->value = desc.initialValue;
        return handle;
    }

    void WebGPUDevice::DestroySemaphoreImpl(SemaphoreHandle h) {
        semaphores_.Free(h);
    }

    void WebGPUDevice::SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value) {
        auto* data = semaphores_.Lookup(h);
        if (data) {
            data->value = value;
        }
    }

    void WebGPUDevice::WaitSemaphoreImpl(
        SemaphoreHandle h, [[maybe_unused]] uint64_t value, [[maybe_unused]] uint64_t timeout
    ) {
        // WebGPU single queue — no cross-queue sync needed
        (void)h;
    }

    auto WebGPUDevice::GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t {
        auto* data = semaphores_.Lookup(h);
        return data ? data->value : 0;
    }

    void WebGPUDevice::WaitIdleImpl() {
        // Tick device until all work completes
        if (!device_) {
            return;
        }
#ifndef EMSCRIPTEN
        // Dawn-specific: poll until idle
        bool workDone = false;
        WGPUQueueWorkDoneCallbackInfo workDoneCbInfo{};
        workDoneCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        workDoneCbInfo.callback = [](WGPUQueueWorkDoneStatus status, void* userdata1, void*) {
            (void)status;
            *static_cast<bool*>(userdata1) = true;
        };
        workDoneCbInfo.userdata1 = &workDone;
        wgpuQueueOnSubmittedWorkDone(queue_, workDoneCbInfo);
        while (!workDone) {
            wgpuDeviceTick(device_);
        }
#endif
    }

    // =========================================================================
    // Submission
    // =========================================================================

    void WebGPUDevice::SubmitImpl([[maybe_unused]] QueueType queue, const SubmitDesc& desc) {
        // Collect finished command buffers stored by WebGPUCommandBuffer::EndImpl
        std::vector<WGPUCommandBuffer> cmdBufs;
        cmdBufs.reserve(desc.commandBuffers.size());

        for (auto cbHandle : desc.commandBuffers) {
            auto* cbData = commandBuffers_.Lookup(cbHandle);
            if (!cbData) {
                continue;
            }

            if (cbData->finishedBuffer) {
                cmdBufs.push_back(cbData->finishedBuffer);
                cbData->finishedBuffer = nullptr;  // Ownership transferred to submit
            }
        }

        if (!cmdBufs.empty()) {
            wgpuQueueSubmit(queue_, static_cast<uint32_t>(cmdBufs.size()), cmdBufs.data());

            for (auto cb : cmdBufs) {
                wgpuCommandBufferRelease(cb);
            }
        }

        // Signal fence via onSubmittedWorkDone callback
        ++submittedSerial_;
        if (desc.signalFence.IsValid()) {
            auto* fenceData = fences_.Lookup(desc.signalFence);
            if (fenceData) {
                fenceData->submittedSerial = submittedSerial_;
                WGPUQueueWorkDoneCallbackInfo fenceCbInfo{};
                fenceCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
                fenceCbInfo.callback = [](WGPUQueueWorkDoneStatus status, void* userdata1, void*) {
                    (void)status;
                    auto* fd = static_cast<WGPUFenceData*>(userdata1);
                    fd->signaled = true;
                };
                fenceCbInfo.userdata1 = fenceData;
                wgpuQueueOnSubmittedWorkDone(queue_, fenceCbInfo);
            }
        }

        // Signal semaphores (no-op on single-queue WebGPU, just update value)
        for (const auto& sig : desc.signalSemaphores) {
            auto* semData = semaphores_.Lookup(sig.semaphore);
            if (semData) {
                semData->value = sig.value;
            }
        }
    }

    // =========================================================================
    // Memory stats (WebGPU has no memory introspection)
    // =========================================================================

    auto WebGPUDevice::GetMemoryStatsImpl() const -> MemoryStats {
        return MemoryStats{
            .totalAllocationCount = totalAllocationCount_,
            .totalAllocatedBytes = totalAllocatedBytes_,
            .totalUsedBytes = totalAllocatedBytes_,
            .heapCount = 0,
        };
    }

    auto WebGPUDevice::GetMemoryHeapBudgetsImpl([[maybe_unused]] std::span<MemoryHeapBudget> out) const -> uint32_t {
        return 0;  // No heap budget info available
    }

    // =========================================================================
    // Unsupported T3 stubs
    // =========================================================================

    auto WebGPUDevice::GetSparsePageSizeImpl() const -> SparsePageSize {
        return {};
    }

    void WebGPUDevice::SubmitSparseBindsImpl(
        [[maybe_unused]] QueueType queue, [[maybe_unused]] const SparseBindDesc& binds,
        [[maybe_unused]] std::span<const SemaphoreSubmitInfo> wait,
        [[maybe_unused]] std::span<const SemaphoreSubmitInfo> signal
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: sparse binding not supported");
    }

    auto WebGPUDevice::GetBLASBuildSizesImpl([[maybe_unused]] const BLASDesc& desc) -> AccelStructBuildSizes {
        return {};
    }

    auto WebGPUDevice::GetTLASBuildSizesImpl([[maybe_unused]] const TLASDesc& desc) -> AccelStructBuildSizes {
        return {};
    }

    auto WebGPUDevice::CreateBLASImpl([[maybe_unused]] const BLASDesc& desc) -> RhiResult<AccelStructHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    auto WebGPUDevice::CreateTLASImpl([[maybe_unused]] const TLASDesc& desc) -> RhiResult<AccelStructHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    void WebGPUDevice::DestroyAccelStructImpl([[maybe_unused]] AccelStructHandle h) {}

}  // namespace miki::rhi
