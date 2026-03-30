/** @file BackendStub.h
 *  @brief Macro-generated DeviceBase stub for backends not yet implemented.
 *
 *  Each backend class inherits DeviceBase<Self> and must provide all *Impl methods.
 *  This header provides a macro MIKI_DEVICE_STUB_IMPL(ClassName) that expands to
 *  all required *Impl methods returning RhiError::NotImplemented or asserting.
 *
 *  These are NOT mocks — they are real backend skeletons. Each stub method will be
 *  replaced with real backend code as backends are implemented.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <cassert>
#include <span>
#include <vector>

#include "miki/rhi/Device.h"
#include "miki/rhi/GpuCapabilityProfile.h"

// clang-format off

// ---------------------------------------------------------------------------
// MIKI_DEVICE_STUB_RESOURCE_IMPL — resource/pipeline/query/accel/memory stubs
//
// Used by backends that implement swapchain + sync + submit themselves
// (e.g. VulkanDevice) but haven't yet implemented resource subsystems.
// ---------------------------------------------------------------------------
#define MIKI_DEVICE_STUB_RESOURCE_IMPL                                                                                 \
    /* --- Resource creation --- */                                                                                     \
    auto CreateBufferImpl(const BufferDesc&) -> RhiResult<BufferHandle> {                                              \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyBufferImpl(BufferHandle) {}                                                                            \
    auto MapBufferImpl(BufferHandle) -> RhiResult<void*> {                                                             \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void UnmapBufferImpl(BufferHandle) {}                                                                              \
    void FlushMappedRangeImpl(BufferHandle, uint64_t, uint64_t) {}                                                     \
    void InvalidateMappedRangeImpl(BufferHandle, uint64_t, uint64_t) {}                                                \
    auto GetBufferDeviceAddressImpl(BufferHandle) -> uint64_t { return 0; }                                            \
                                                                                                                       \
    auto CreateTextureImpl(const TextureDesc&) -> RhiResult<TextureHandle> {                                           \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto CreateTextureViewImpl(const TextureViewDesc&) -> RhiResult<TextureViewHandle> {                               \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyTextureViewImpl(TextureViewHandle) {}                                                                  \
    void DestroyTextureImpl(TextureHandle) {}                                                                          \
                                                                                                                       \
    auto CreateSamplerImpl(const SamplerDesc&) -> RhiResult<SamplerHandle> {                                           \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroySamplerImpl(SamplerHandle) {}                                                                          \
                                                                                                                       \
    /* --- Memory aliasing --- */                                                                                      \
    auto CreateMemoryHeapImpl(const MemoryHeapDesc&) -> RhiResult<DeviceMemoryHandle> {                                \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyMemoryHeapImpl(DeviceMemoryHandle) {}                                                                  \
    void AliasBufferMemoryImpl(BufferHandle, DeviceMemoryHandle, uint64_t) {}                                          \
    void AliasTextureMemoryImpl(TextureHandle, DeviceMemoryHandle, uint64_t) {}                                        \
    auto GetBufferMemoryRequirementsImpl(BufferHandle) -> MemoryRequirements { return {}; }                             \
    auto GetTextureMemoryRequirementsImpl(TextureHandle) -> MemoryRequirements { return {}; }                           \
                                                                                                                       \
    /* --- Sparse binding --- */                                                                                       \
    auto GetSparsePageSizeImpl() const -> SparsePageSize { return {}; }                                                \
    void SubmitSparseBindsImpl(QueueType, const SparseBindDesc&,                                                       \
        std::span<const SemaphoreSubmitInfo>, std::span<const SemaphoreSubmitInfo>) {}                                 \
                                                                                                                       \
    /* --- Shader --- */                                                                                               \
    auto CreateShaderModuleImpl(const ShaderModuleDesc&) -> RhiResult<ShaderModuleHandle> {                            \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyShaderModuleImpl(ShaderModuleHandle) {}                                                                \
                                                                                                                       \
    /* --- Descriptors --- */                                                                                          \
    auto CreateDescriptorLayoutImpl(const DescriptorLayoutDesc&) -> RhiResult<DescriptorLayoutHandle> {                \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyDescriptorLayoutImpl(DescriptorLayoutHandle) {}                                                        \
    auto CreatePipelineLayoutImpl(const PipelineLayoutDesc&) -> RhiResult<PipelineLayoutHandle> {                      \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyPipelineLayoutImpl(PipelineLayoutHandle) {}                                                            \
    auto CreateDescriptorSetImpl(const DescriptorSetDesc&) -> RhiResult<DescriptorSetHandle> {                         \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void UpdateDescriptorSetImpl(DescriptorSetHandle, std::span<const DescriptorWrite>) {}                             \
    void DestroyDescriptorSetImpl(DescriptorSetHandle) {}                                                              \
                                                                                                                       \
    /* --- Pipelines --- */                                                                                            \
    auto CreateGraphicsPipelineImpl(const GraphicsPipelineDesc&) -> RhiResult<PipelineHandle> {                        \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto CreateComputePipelineImpl(const ComputePipelineDesc&) -> RhiResult<PipelineHandle> {                          \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto CreateRayTracingPipelineImpl(const RayTracingPipelineDesc&) -> RhiResult<PipelineHandle> {                    \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyPipelineImpl(PipelineHandle) {}                                                                        \
                                                                                                                       \
    /* --- Pipeline cache --- */                                                                                       \
    auto CreatePipelineCacheImpl(std::span<const uint8_t>) -> RhiResult<PipelineCacheHandle> {                         \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto GetPipelineCacheDataImpl(PipelineCacheHandle) -> std::vector<uint8_t> { return {}; }                          \
    void DestroyPipelineCacheImpl(PipelineCacheHandle) {}                                                              \
                                                                                                                       \
    /* --- Pipeline library --- */                                                                                     \
    auto CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc&) -> RhiResult<PipelineLibraryPartHandle> {       \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto LinkGraphicsPipelineImpl(const LinkedPipelineDesc&) -> RhiResult<PipelineHandle> {                            \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
                                                                                                                       \
    /* --- Command buffers --- */                                                                                      \
    auto CreateCommandBufferImpl(const CommandBufferDesc&) -> RhiResult<CommandBufferHandle> {                         \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyCommandBufferImpl(CommandBufferHandle) {}                                                              \
    auto AcquireCommandListImpl(QueueType) -> RhiResult<CommandListAcquisition> {                                      \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void ReleaseCommandListImpl(const CommandListAcquisition&) {}                                                      \
                                                                                                                       \
    /* --- Command pools (§19) --- */                                                                                  \
    auto CreateCommandPoolImpl(const CommandPoolDesc&) -> RhiResult<CommandPoolHandle> {                               \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyCommandPoolImpl(CommandPoolHandle) {}                                                                  \
    void ResetCommandPoolImpl(CommandPoolHandle, CommandPoolResetFlags) {}                                             \
    auto AllocateFromPoolImpl(CommandPoolHandle, bool) -> RhiResult<CommandListAcquisition> {                          \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void FreeFromPoolImpl(CommandPoolHandle, const CommandListAcquisition&) {}                                                      \
                                                                                                                       \
    /* --- Query --- */                                                                                                \
    auto CreateQueryPoolImpl(const QueryPoolDesc&) -> RhiResult<QueryPoolHandle> {                                     \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyQueryPoolImpl(QueryPoolHandle) {}                                                                      \
    auto GetQueryResultsImpl(QueryPoolHandle, uint32_t, uint32_t, std::span<uint64_t>) -> RhiResult<void> {            \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto GetTimestampPeriodImpl() -> double { return 0.0; }                                                            \
                                                                                                                       \
    /* --- Acceleration structure --- */                                                                               \
    auto GetBLASBuildSizesImpl(const BLASDesc&) -> AccelStructBuildSizes { return {}; }                                \
    auto GetTLASBuildSizesImpl(const TLASDesc&) -> AccelStructBuildSizes { return {}; }                                \
    auto CreateBLASImpl(const BLASDesc&) -> RhiResult<AccelStructHandle> {                                             \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto CreateTLASImpl(const TLASDesc&) -> RhiResult<AccelStructHandle> {                                             \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyAccelStructImpl(AccelStructHandle) {}                                                                  \
                                                                                                                       \
    /* --- Memory stats --- */                                                                                         \
    auto GetMemoryStatsImpl() const -> MemoryStats { return {}; }                                                      \
    auto GetMemoryHeapBudgetsImpl(std::span<MemoryHeapBudget>) const -> uint32_t { return 0; }                         \
                                                                                                                       \
    /* --- Surface capabilities --- */                                                                                 \
    auto GetSurfaceCapabilitiesImpl(const NativeWindowHandle&) const -> RenderSurfaceCapabilities { return {}; }

// ---------------------------------------------------------------------------
// MIKI_DEVICE_STUB_IMPL — ALL methods stubbed (for backends not yet started)
// ---------------------------------------------------------------------------
#define MIKI_DEVICE_STUB_IMPL(ClassName)                                                                               \
    MIKI_DEVICE_STUB_RESOURCE_IMPL                                                                                     \
                                                                                                                       \
    /* --- Synchronization --- */                                                                                      \
    auto CreateFenceImpl(bool) -> RhiResult<FenceHandle> {                                                             \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroyFenceImpl(FenceHandle) {}                                                                              \
    void WaitFenceImpl(FenceHandle, uint64_t) {}                                                                       \
    void ResetFenceImpl(FenceHandle) {}                                                                                \
    auto GetFenceStatusImpl(FenceHandle) -> bool { return false; }                                                     \
                                                                                                                       \
    auto CreateSemaphoreImpl(const SemaphoreDesc&) -> RhiResult<SemaphoreHandle> {                                     \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroySemaphoreImpl(SemaphoreHandle) {}                                                                      \
    void SignalSemaphoreImpl(SemaphoreHandle, uint64_t) {}                                                             \
    void WaitSemaphoreImpl(SemaphoreHandle, uint64_t, uint64_t) {}                                                     \
    auto GetSemaphoreValueImpl(SemaphoreHandle) -> uint64_t { return 0; }                                              \
                                                                                                                       \
    /* --- Submission --- */                                                                                           \
    void SubmitImpl(QueueType, const SubmitDesc&) {}                                                                   \
    void WaitIdleImpl() {}                                                                                             \
                                                                                                                       \
    /* --- Swapchain --- */                                                                                            \
    auto CreateSwapchainImpl(const SwapchainDesc&) -> RhiResult<SwapchainHandle> {                                     \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    void DestroySwapchainImpl(SwapchainHandle) {}                                                                      \
    auto ResizeSwapchainImpl(SwapchainHandle, uint32_t, uint32_t) -> RhiResult<void> {                                 \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto AcquireNextImageImpl(SwapchainHandle, SemaphoreHandle, FenceHandle) -> RhiResult<uint32_t> {                  \
        return std::unexpected(RhiError::NotImplemented); }                                                            \
    auto GetSwapchainTextureImpl(SwapchainHandle, uint32_t) -> TextureHandle { return {}; }                            \
    void PresentImpl(SwapchainHandle, std::span<const SemaphoreHandle>) {}                                             \
                                                                                                                       \
    /* --- Capability --- */                                                                                           \
    auto GetCapabilitiesImpl() const -> const GpuCapabilityProfile& {                                                  \
        static GpuCapabilityProfile empty{};                                                                           \
        return empty;                                                                                                  \
    }                                                                                                                  \
    auto GetQueueTimelinesImpl() const -> QueueTimelines { return {}; }                                                \
    auto GetBackendTypeImpl() const -> BackendType
// clang-format on
