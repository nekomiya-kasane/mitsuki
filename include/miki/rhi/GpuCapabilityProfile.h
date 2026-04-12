/** @file GpuCapabilityProfile.h
 *  @brief GPU capability detection for the miki RHI.
 *
 *  GpuCapabilityProfile is populated once during IDevice creation and is
 *  immutable afterward. Thread-safe by immutability.
 *
 *  CapabilityTier provides coarse classification:
 *    - Tier1_Vulkan: Vulkan 1.4 + mesh shader + ray query + descriptor buffer/heap + timeline semaphore
 *    - Tier1_D3D12:  D3D12 + SM 6.5+ + mesh shader + DXR 1.1
 *    - Tier2_Compat: Vulkan 1.1 + select extensions, no mesh/RT
 *    - Tier3_WebGPU: WebGPU backend (Dawn/wgpu)
 *    - Tier4_OpenGL: OpenGL 4.3+ fallback
 *
 *  Individual feature booleans provide fine-grained queries beyond the tier.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "miki/rhi/DeviceFeature.h"
#include "miki/rhi/Format.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

    /** @brief Coarse GPU capability classification.
     *
     *  Tier detection logic:
     *  - Vulkan 1.4 + mesh_shader + ray_query + (descriptor_heap || descriptor_buffer) + timeline_semaphore →
     * Tier1_Vulkan
     *  - D3D12 + SM 6.5+ + mesh_shader + DXR 1.1 → Tier1_D3D12
     *  - Vulkan 1.1 + ¬mesh_shader → Tier2_Compat
     *  - WebGPU (Dawn/wgpu) → Tier3_WebGPU
     *  - OpenGL 4.3+ → Tier4_OpenGL
     */
    enum class CapabilityTier : uint8_t {
        Tier1_Vulkan,  ///< Vulkan 1.4 full feature set
        Tier1_D3D12,   ///< D3D12 full feature set
        Tier2_Compat,  ///< Vulkan 1.1 compatibility mode
        Tier3_WebGPU,  ///< WebGPU backend
        Tier4_OpenGL,  ///< OpenGL 4.3+ fallback
    };

    // =========================================================================
    // Push constant size limits (compile-time)
    // =========================================================================

    /// Cross-platform guaranteed minimum push constant size (bytes).
    /// This is the intersection of all backend limits:
    ///   Vulkan ≥128B (spec minimum), D3D12 = 256B, WebGPU = 256B (UBO), OpenGL = 128B (UBO).
    static constexpr uint32_t kMinPushConstantSize = 128;

    /// Maximum push constant size supported by any single backend (bytes).
    /// D3D12 Root Constants = 256B, WebGPU UBO = 256B, Vulkan typically 256B+.
    static constexpr uint32_t kMaxPushConstantSize = 256;

    /// Compile-time check: does a push-constant struct fit within the cross-platform minimum?
    /// Use this when you need the struct to work on ALL backends without runtime checks.
    /// @tparam T  The push-constant struct type (must be trivially copyable).
    /// @return true if sizeof(T) <= kMinPushConstantSize.
    template <typename T>
    consteval auto PushConstantFitsAllBackends() -> bool {
        static_assert(std::is_trivially_copyable_v<T>, "Push constant struct must be trivially copyable");
        return sizeof(T) <= kMinPushConstantSize;
    }

    /// Compile-time check: does a push-constant struct fit within a specific byte limit?
    /// Use this for backend-specific validation (e.g. 256B for D3D12/WebGPU).
    /// @tparam T     The push-constant struct type.
    /// @tparam Limit Maximum allowed size in bytes.
    /// @return true if sizeof(T) <= Limit.
    template <typename T, uint32_t Limit>
    consteval auto PushConstantFits() -> bool {
        static_assert(std::is_trivially_copyable_v<T>, "Push constant struct must be trivially copyable");
        return sizeof(T) <= Limit;
    }

    /** @brief GPU capability profile — immutable after device creation.
     *
     *  Populated by backend-specific device init code. Exposed via
     *  IDevice::GetCapabilities(). Never mutated after construction.
     *
     *  enabledFeatures is the single source of truth for feature queries.
     *  The Has*() convenience accessors delegate to enabledFeatures.
     *
     *  Aligned with spec §4.2.
     */
    struct GpuCapabilityProfile {
        // =====================================================================
        // Tier & Backend
        // =====================================================================
        CapabilityTier tier = CapabilityTier::Tier4_OpenGL;
        BackendType backendType = BackendType::Mock;
        DeviceFeatureSet enabledFeatures;  ///< Actual enabled features (single source of truth)

        // =====================================================================
        // Geometry
        // =====================================================================
        bool hasMeshShader = false;
        bool hasTaskShader = false;
        bool hasMultiDrawIndirect = false;
        bool hasMultiDrawIndirectCount = false;
        uint32_t maxDrawIndirectCount = 0;

        // =====================================================================
        // Descriptors
        // =====================================================================
        DescriptorModel descriptorModel = DescriptorModel::DescriptorSet;  ///< DescriptorHeap, DescriptorBuffer,
                                                                           ///< DescriptorSet, BindGroup, DirectBind
        bool hasBindless = false;
        bool hasPushDescriptors = false;
        uint32_t maxPushConstantSize = kMinPushConstantSize;  ///< 128-256 bytes
        uint32_t maxBoundDescriptorSets = 4;
        uint64_t maxStorageBufferSize = 128ULL * 1024 * 1024;  ///< 128MB (WebGPU) to unlimited

        // =====================================================================
        // Compute
        // =====================================================================
        bool hasAsyncCompute = false;
        bool hasAsyncTransfer = false;  ///< Dedicated transfer/DMA queue available
        bool hasTimelineSemaphore = false;
        bool hasGlobalPriority = false;        ///< VK_EXT_global_priority / D3D12 queue priority
        uint32_t computeQueueFamilyCount = 0;  ///< Number of distinct compute queue families (Vulkan)
        std::array<uint32_t, 3> maxComputeWorkGroupCount = {65535, 65535, 65535};
        std::array<uint32_t, 3> maxComputeWorkGroupSize = {1024, 1024, 64};

        // =====================================================================
        // Synchronization (D3D12 Enhanced/Fence Barriers)
        // =====================================================================
        bool hasEnhancedBarriers = false;  ///< D3D12 Enhanced Barriers (Agility SDK 1.7+)

        /// @brief D3D12 Fence Barrier tier (Agility SDK 1.719+).
        /// Tier1: command-list-scoped signal/wait (replaces split barriers).
        /// Tier2 (future): cross-queue WaitBarrier + CPU fence signaling.
        enum class FenceBarrierTier : uint8_t {
            None = 0,
            Tier1 = 1,
            Tier2 = 2
        };
        FenceBarrierTier fenceBarrierTier = FenceBarrierTier::None;

        // =====================================================================
        // Ray Tracing
        // =====================================================================
        bool hasRayQuery = false;
        bool hasRayTracingPipeline = false;
        bool hasAccelerationStructure = false;

        // =====================================================================
        // Shading
        // =====================================================================
        bool hasSpirvShaders = false;  ///< GL_ARB_gl_spirv / GL 4.6 (OpenGL only; always true for Vulkan/D3D12)
        bool hasVariableRateShading = false;
        bool hasFloat64 = false;
        bool hasInt64Atomics = false;
        bool hasSubgroupOps = false;
        uint32_t subgroupSize = 32;  ///< 32 (NVIDIA/Intel) or 64 (AMD)

        // =====================================================================
        // Memory
        // =====================================================================
        uint64_t deviceLocalMemoryBytes = 0;
        uint64_t hostVisibleMemoryBytes = 0;
        bool hasResizableBAR = false;
        bool hasSparseBinding = false;
        bool hasSparseResidencyBuffer = false;     ///< Sparse residency for buffers (page-granularity bind/unbind)
        bool hasSparseResidencyImage2D = false;    ///< Sparse residency for 2D images
        bool hasSparseResidencyImage3D = false;    ///< Sparse residency for 3D images
        bool hasStandardSparseBlockShape = false;  ///< residencyStandard2DBlockShape (standard tile sizes per format)
        bool hasTextureCompressionASTC_HDR = false; ///< ASTC HDR (SFLOAT) texture format support
        bool hasHardwareDecompression = false;  ///< GDeflate HW decode (VK_NV_memory_decompression / DirectStorage)
        bool hasMemoryBudgetQuery = false;

        /// @brief D3D12 ResourceHeapTier (§5.6.2). Tier2 = split heaps (all modern GPUs).
        /// Vulkan/GL/WebGPU always act as Tier2 (split by memoryType). Mock defaults to Tier2.
        enum class ResourceHeapTier : uint8_t {
            Tier1 = 1,
            Tier2 = 2
        };
        ResourceHeapTier resourceHeapTier = ResourceHeapTier::Tier2;
        bool hasGraphicsPipelineLibrary = false;  ///< VK_EXT_graphics_pipeline_library / D3D12 ID3D12PipelineLibrary
        bool hasWorkGraphs = false;         ///< D3D12 SM 6.8 DispatchGraph
        bool hasCooperativeMatrix = false;  ///< VK_KHR_cooperative_matrix / SM 6.9 wave matrix

        // =====================================================================
        // Limits
        // =====================================================================
        uint32_t maxColorAttachments = 8;   ///< 4-8
        uint32_t maxTextureSize2D = 16384;  ///< 4096-16384
        uint32_t maxTextureSizeCube = 16384;
        uint32_t maxFramebufferWidth = 16384;
        uint32_t maxFramebufferHeight = 16384;
        uint32_t maxViewports = 16;
        uint32_t maxClipDistances = 8;

        // =====================================================================
        // Mesh Shader Limits (Tier1 only, 0 = not supported)
        // =====================================================================
        std::array<uint32_t, 3> maxMeshWorkGroupSize = {0, 0, 0};
        uint32_t maxMeshWorkGroupInvocations = 0;
        std::array<uint32_t, 3> maxTaskWorkGroupSize = {0, 0, 0};
        uint32_t maxTaskWorkGroupInvocations = 0;
        uint32_t maxMeshOutputVertices = 0;
        uint32_t maxMeshOutputPrimitives = 0;
        uint32_t maxTaskPayloadSize = 0;
        uint32_t maxMeshSharedMemorySize = 0;
        uint32_t maxMeshPayloadAndSharedMemorySize = 0;

        // =====================================================================
        // GPU Architecture
        // =====================================================================
        bool isTileBasedGpu = false;  ///< Tile-based deferred rendering (Apple, Qualcomm, ARM Mali, Intel iGPU)

        // =====================================================================
        // Device Info
        // =====================================================================
        std::string deviceName;
        std::string driverVersion;
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;

        // =====================================================================
        // Convenience Accessors (delegate to enabledFeatures)
        // =====================================================================
        [[nodiscard]] bool HasPresent() const noexcept { return enabledFeatures.Has(DeviceFeature::Present); }
        [[nodiscard]] bool HasMeshShader() const noexcept { return enabledFeatures.Has(DeviceFeature::MeshShader); }
        [[nodiscard]] bool HasRayTracing() const noexcept {
            return enabledFeatures.Has(DeviceFeature::RayTracingPipeline);
        }
        [[nodiscard]] bool HasVariableRateShading() const noexcept {
            return enabledFeatures.Has(DeviceFeature::VariableRateShading);
        }
        [[nodiscard]] bool Has64BitAtomics() const noexcept { return enabledFeatures.Has(DeviceFeature::Int64Atomics); }
        [[nodiscard]] bool HasDescriptorBuffer() const noexcept {
            return enabledFeatures.Has(DeviceFeature::DescriptorBuffer);
        }
        [[nodiscard]] bool HasCooperativeMatrix() const noexcept {
            return enabledFeatures.Has(DeviceFeature::CooperativeMatrix);
        }
        [[nodiscard]] bool HasTimelineSemaphore() const noexcept {
            return enabledFeatures.Has(DeviceFeature::TimelineSemaphore);
        }
        [[nodiscard]] bool HasDynamicRendering() const noexcept {
            return enabledFeatures.Has(DeviceFeature::DynamicRendering);
        }
        [[nodiscard]] bool HasPushDescriptors() const noexcept {
            return enabledFeatures.Has(DeviceFeature::PushDescriptors);
        }
        [[nodiscard]] bool HasSparseBinding() const noexcept {
            return enabledFeatures.Has(DeviceFeature::SparseBinding);
        }
        [[nodiscard]] bool HasSparseResidency() const noexcept {
            return enabledFeatures.Has(DeviceFeature::SparseResidency);
        }
        [[nodiscard]] bool HasTextureCompressionASTC_HDR() const noexcept {
            return enabledFeatures.Has(DeviceFeature::TextureCompressionASTC_HDR);
        }
        [[nodiscard]] bool HasAsyncCompute() const noexcept { return enabledFeatures.Has(DeviceFeature::AsyncCompute); }
        [[nodiscard]] bool HasWorkGraphs() const noexcept { return enabledFeatures.Has(DeviceFeature::WorkGraphs); }
        [[nodiscard]] bool HasGraphicsPipelineLibrary() const noexcept {
            return enabledFeatures.Has(DeviceFeature::GraphicsPipelineLibrary);
        }

        // =====================================================================
        // Format Support Table
        // =====================================================================
        static constexpr uint32_t kFormatCount = static_cast<uint32_t>(Format::Count_);

        /** @brief Per-format feature support flags.
         *  Indexed by static_cast<uint32_t>(Format). Populated by backend PopulateCapabilities().
         *  Initialized to conservative defaults via BuildDefaultFormatSupport().
         */
        std::array<FormatFeatureFlags, kFormatCount> formatSupport = BuildDefaultFormatSupport();

        /** @brief Build a conservative default format support table from format properties.
         *  - Color formats get Sampled|Filter|ColorAttachment|BlendSrc.
         *  - Integer formats get Sampled|ColorAttachment (no Filter/Blend).
         *  - Depth/stencil formats get Sampled|Filter|DepthStencil.
         *  - Compressed formats get Sampled|Filter.
         *  - Storage requires explicit backend opt-in (only R32/RG32/RGBA32 float/uint/sint are universal).
         */
        [[nodiscard]] static constexpr auto BuildDefaultFormatSupport()
            -> std::array<FormatFeatureFlags, kFormatCount> {
            std::array<FormatFeatureFlags, kFormatCount> table{};
            for (uint32_t i = 0; i < kFormatCount; ++i) {
                auto fmt = static_cast<Format>(i);
                if (fmt == Format::Undefined) {
                    continue;
                }
                auto info = FormatInfo(fmt);
                if (info.isDepth || info.isStencil) {
                    table[i]
                        = FormatFeatureFlags::Sampled | FormatFeatureFlags::Filter | FormatFeatureFlags::DepthStencil;
                } else if (info.isCompressed) {
                    table[i] = FormatFeatureFlags::Sampled | FormatFeatureFlags::Filter;
                } else {
                    // Uncompressed color: check if integer (no filter/blend)
                    bool isInteger = false;
                    switch (fmt) {
                        case Format::R8_UINT:
                        case Format::R8_SINT:
                        case Format::RG8_UINT:
                        case Format::RG8_SINT:
                        case Format::RGBA8_UINT:
                        case Format::RGBA8_SINT:
                        case Format::R16_UINT:
                        case Format::R16_SINT:
                        case Format::RG16_UINT:
                        case Format::RG16_SINT:
                        case Format::RGBA16_UINT:
                        case Format::RGBA16_SINT:
                        case Format::R32_UINT:
                        case Format::R32_SINT:
                        case Format::RG32_UINT:
                        case Format::RG32_SINT:
                        case Format::RGB32_UINT:
                        case Format::RGB32_SINT:
                        case Format::RGBA32_UINT:
                        case Format::RGBA32_SINT: isInteger = true; break;
                        default: break;
                    }
                    if (isInteger) {
                        table[i] = FormatFeatureFlags::Sampled | FormatFeatureFlags::ColorAttachment;
                    } else {
                        table[i] = FormatFeatureFlags::Sampled | FormatFeatureFlags::Filter
                                   | FormatFeatureFlags::ColorAttachment | FormatFeatureFlags::BlendSrc;
                    }
                    // Universal storage formats (Vulkan/D3D12/GL/WebGPU all guarantee these)
                    switch (fmt) {
                        case Format::R32_FLOAT:
                        case Format::R32_UINT:
                        case Format::R32_SINT:
                        case Format::RG32_FLOAT:
                        case Format::RG32_UINT:
                        case Format::RG32_SINT:
                        case Format::RGBA32_FLOAT:
                        case Format::RGBA32_UINT:
                        case Format::RGBA32_SINT:
                        case Format::RGBA16_FLOAT:
                        case Format::RGBA8_UNORM:
                        case Format::RGBA8_SNORM:
                        case Format::RGBA8_UINT:
                        case Format::RGBA8_SINT: table[i] = table[i] | FormatFeatureFlags::Storage; break;
                        default: break;
                    }
                }
            }
            return table;
        }

        // =====================================================================
        // Tier & Format Queries
        // =====================================================================

        /** @brief Check if this device's tier is at least as capable as iRequested.
         *
         *  Tier ordering: Tier1_Vulkan/Tier1_D3D12 (0-1) > Tier2_Compat (2) > Tier3_WebGPU (3) > Tier4_OpenGL (4).
         *  Lower enum value = higher capability.
         *
         *  @param iRequested The minimum tier to check against.
         *  @return True if this device's tier meets or exceeds the requested tier.
         */
        [[nodiscard]] constexpr auto SupportsTier(CapabilityTier iRequested) const noexcept -> bool {
            return static_cast<uint8_t>(tier) <= static_cast<uint8_t>(iRequested);
        }

        /** @brief Check if this device is a Tier1 backend (Vulkan 1.4 or D3D12).
         *  @return True if tier is Tier1_Vulkan or Tier1_D3D12.
         */
        [[nodiscard]] constexpr auto IsTier1() const noexcept -> bool {
            return tier == CapabilityTier::Tier1_Vulkan || tier == CapabilityTier::Tier1_D3D12;
        }

        /** @brief Check if this device supports a given format with specified features.
         *  O(1) lookup into the flat formatSupport table.
         *  @param iFormat The format to query.
         *  @param iFeatures Required format features (bitwise AND check).
         *  @return True if all requested features are supported for this format.
         */
        [[nodiscard]] constexpr auto IsFormatSupported(
            Format iFormat, FormatFeatureFlags iFeatures = FormatFeatureFlags::All
        ) const noexcept -> bool {
            auto idx = static_cast<uint32_t>(iFormat);
            if (idx == 0 || idx >= kFormatCount) {
                return false;
            }
            return (formatSupport[idx] & iFeatures) == iFeatures;
        }
    };

}  // namespace miki::rhi
