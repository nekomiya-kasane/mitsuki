/** @brief Backend-agnostic device feature identifiers and feature set.
 *
 * DeviceFeature enumerates semantic GPU/platform capabilities that callers
 * can request via DeviceConfig. Each backend internally maps these to
 * native extensions/features (Vulkan extensions, D3D12 feature levels, etc.).
 *
 * DeviceFeatureSet provides O(1) lookup via std::bitset, zero heap allocation.
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <bitset>
#include <cstdint>
#include <initializer_list>

namespace miki::rhi {

    /** @brief Backend-agnostic device feature identifiers.
     *
     * Grouped by category for readability; the enum itself is flat.
     * New features are appended before Count_ — no ABI break.
     *
     * Backend availability key per feature (Doxygen comments):
     *   - Vk     = Vulkan extension or core version
     *   - D3D12  = D3D12 feature level / tier
     *   - GL     = OpenGL extension or core version
     *   - WebGPU = WebGPU native feature or N/A
     */
    enum class DeviceFeature : uint8_t {
        // =====================================================================
        // Platform / Presentation
        // =====================================================================

        /** @brief Windowed present (swapchain / surface).
         *  Vk: VK_KHR_swapchain + VK_KHR_surface | D3D12: DXGI swap chain |
         *  GL: platform swap (WGL/GLX/EGL) | WebGPU: GPUCanvasContext */
        Present,

        // =====================================================================
        // Core rendering
        // =====================================================================

        /** @brief Render-pass-less rendering.
         *  Vk: core 1.3 (VK_KHR_dynamic_rendering) | D3D12: always (no render pass object) |
         *  GL: always (FBO) | WebGPU: always (GPURenderPassEncoder) */
        DynamicRendering,

        /** @brief Fine-grained pipeline barrier API.
         *  Vk: core 1.3 (VK_KHR_synchronization2) | D3D12: always (ID3D12GraphicsCommandList::ResourceBarrier) |
         *  GL: N/A (driver-managed) | WebGPU: N/A (driver-managed) */
        Synchronization2,

        /** @brief Timeline (monotonic uint64) semaphores.
         *  Vk: core 1.2 (VK_KHR_timeline_semaphore) | D3D12: ID3D12Fence (always) |
         *  GL: N/A | WebGPU: N/A */
        TimelineSemaphore,

        // =====================================================================
        // Geometry
        // =====================================================================

        /** @brief Mesh + task (amplification) shader pipeline.
         *  Vk: VK_EXT_mesh_shader | D3D12: Mesh Shader Tier 1 (SM 6.5+) |
         *  GL: NV_mesh_shader (NVIDIA only) | WebGPU: N/A */
        MeshShader,

        /** @brief Legacy geometry shader.
         *  Vk: core 1.0 (geometryShader feature) | D3D12: always (SM 4.0+) |
         *  GL: core 3.2+ | WebGPU: N/A */
        GeometryShader,

        /** @brief Multi-draw indirect (batch draw calls with GPU-side parameters).
         *  Vk: core 1.0 (multiDrawIndirect feature) | D3D12: always (ExecuteIndirect) |
         *  GL: ARB_multi_draw_indirect | WebGPU: N/A */
        MultiDrawIndirect,

        /** @brief Multi-draw indirect count (GPU-driven draw count).
         *  Vk: VK_KHR_draw_indirect_count (core 1.2) | D3D12: ExecuteIndirect with count buffer |
         *  GL: ARB_indirect_parameters | WebGPU: N/A */
        MultiDrawIndirectCount,

        // =====================================================================
        // Ray tracing
        // =====================================================================

        /** @brief Hardware ray tracing pipeline (TraceRay shader stage).
         *  Vk: VK_KHR_ray_tracing_pipeline | D3D12: DXR 1.0+ (D3D12_RAYTRACING_TIER_1_0) |
         *  GL: N/A | WebGPU: N/A */
        RayTracingPipeline,

        /** @brief Inline ray query from any shader stage.
         *  Vk: VK_KHR_ray_query | D3D12: DXR 1.1 inline (D3D12_RAYTRACING_TIER_1_1) |
         *  GL: N/A | WebGPU: N/A */
        RayQuery,

        /** @brief Bottom/top-level acceleration structure management.
         *  Vk: VK_KHR_acceleration_structure | D3D12: DXR 1.0+ |
         *  GL: N/A | WebGPU: N/A */
        AccelerationStructure,

        // =====================================================================
        // Shading
        // =====================================================================

        /** @brief Variable-rate shading (coarse pixel shading).
         *  Vk: VK_KHR_fragment_shading_rate | D3D12: D3D12_VARIABLE_SHADING_RATE_TIER_1/2 |
         *  GL: NV_shading_rate_image (NVIDIA only) | WebGPU: N/A */
        VariableRateShading,

        // =====================================================================
        // Descriptor / Memory
        // =====================================================================

        /** @brief GPU-side descriptor buffer (replacement for descriptor sets/heaps).
         *  Vk: VK_EXT_descriptor_buffer | D3D12: N/A (descriptor heaps are the native model) |
         *  GL: N/A | WebGPU: N/A */
        DescriptorBuffer,

        /** @brief 64-bit GPU virtual address for buffer access.
         *  Vk: core 1.2 (VK_KHR_buffer_device_address) | D3D12: always (ID3D12Resource::GetGPUVirtualAddress) |
         *  GL: NV_shader_buffer_load / ARB_buffer_storage + manual | WebGPU: N/A */
        BufferDeviceAddress,

        /** @brief Push descriptors into command buffer without set allocation.
         *  Vk: core 1.4 (VK_KHR_push_descriptor) | D3D12: SetGraphicsRootDescriptorTable (inline root descriptors) |
         *  GL: N/A (bind model) | WebGPU: N/A */
        PushDescriptors,

        /** @brief Bindless / update-after-bind descriptor indexing.
         *  Vk: core 1.2 (VK_EXT_descriptor_indexing) | D3D12: always (unbounded descriptor tables, SM 6.6
         * ResourceDescriptorHeap) | GL: ARB_bindless_texture | WebGPU: N/A */
        DescriptorIndexing,

        /** @brief Sparse binding (virtual memory / tiled resources).
         *  Vk: sparseBinding feature | D3D12: Tiled Resources Tier 1+ |
         *  GL: ARB_sparse_texture | WebGPU: N/A */
        SparseBinding,

        /** @brief Sparse residency (page-granularity memory management).
         *  Vk: sparseResidencyBuffer + sparseResidencyImage2D | D3D12: Tiled Resources Tier 2+ |
         *  GL: ARB_sparse_texture2 | WebGPU: N/A */
        SparseResidency,

        /** @brief Resizable BAR
         *  Vk: VK_EXT_memory_budget + large HOST_VISIBLE heap | D3D12: D3D12_FEATURE_DATA_ARCHITECTURE1 |
         *  GL: N/A | WebGPU: N/A */
        ResizableBAR,

        /** @brief Memory budget query (real-time VRAM usage tracking).
         *  Vk: VK_EXT_memory_budget | D3D12: IDXGIAdapter3::QueryVideoMemoryInfo |
         *  GL: N/A | WebGPU: N/A */
        MemoryBudgetQuery,

        /** @brief Hardware decompression (GDeflate).
         *  Vk: VK_NV_memory_decompression | D3D12: DirectStorage 1.1+ |
         *  GL: N/A | WebGPU: N/A */
        HardwareDecompression,

        // =====================================================================
        // Compute
        // =====================================================================

        /** @brief Cooperative matrix (tensor / WMMA) operations.
         *  Vk: VK_KHR_cooperative_matrix | D3D12: SM 6.8 WaveMatrix (preview) |
         *  GL: N/A | WebGPU: N/A */
        CooperativeMatrix,

        /** @brief 64-bit integer atomics in shaders.
         *  Vk: core 1.2 (VK_KHR_shader_atomic_int64) | D3D12: SM 6.6 (64-bit atomics) |
         *  GL: ARB_shader_atomic_counter_ops + NV_shader_atomic_int64 | WebGPU: N/A */
        Int64Atomics,

        /** @brief 64-bit floating point operations in shaders.
         *  Vk: shaderFloat64 feature | D3D12: D3D_SHADER_MODEL_6_2+ |
         *  GL: ARB_gpu_shader_fp64 | WebGPU: N/A */
        Float64,

        /** @brief Subgroup (wave) operations.
         *  Vk: core 1.1 (subgroup ops) | D3D12: SM 6.0+ (wave intrinsics) |
         *  GL: ARB_shader_ballot + ARB_shader_group_vote | WebGPU: subgroups (experimental) */
        SubgroupOps,

        /** @brief Async compute queue support.
         *  Vk: multiple queue families | D3D12: D3D12_COMMAND_LIST_TYPE_COMPUTE |
         *  GL: N/A | WebGPU: N/A */
        AsyncCompute,

        /** @brief Work graphs (GPU-driven task scheduling).
         *  Vk: N/A | D3D12: SM 6.8 DispatchGraph |
         *  GL: N/A | WebGPU: N/A */
        WorkGraphs,

        // =====================================================================
        // Texture compression
        // =====================================================================

        /** @brief BC1-BC7 block compression.
         *  Vk: textureCompressionBC feature | D3D12: always (desktop) |
         *  GL: EXT_texture_compression_s3tc + ARB_texture_compression_bptc | WebGPU: texture-compression-bc */
        TextureCompressionBC,

        /** @brief ASTC block compression.
         *  Vk: textureCompressionASTC_LDR feature | D3D12: N/A (no HW decode on desktop) |
         *  GL: KHR_texture_compression_astc_ldr | WebGPU: texture-compression-astc */
        TextureCompressionASTC,

        /** @brief ASTC HDR (float) block compression.
         *  Vk: core 1.3 (VK_EXT_texture_compression_astc_hdr) | D3D12: N/A |
         *  GL: KHR_texture_compression_astc_hdr | WebGPU: N/A */
        TextureCompressionASTC_HDR,

        /** @brief ETC2 block compression.
         *  Vk: textureCompressionETC2 feature | D3D12: N/A (no HW decode on desktop) |
         *  GL: core 4.3+ / OES_compressed_ETC2_RGB8_texture | WebGPU: texture-compression-etc2 */
        TextureCompressionETC2,

        // =====================================================================
        // Debug / Profiling
        // =====================================================================

        /** @brief Debug markers / labels for command streams.
         *  Vk: VK_EXT_debug_utils | D3D12: PIX markers (WinPixEventRuntime) |
         *  GL: KHR_debug / EXT_debug_marker | WebGPU: pushDebugGroup / popDebugGroup */
        DebugMarkers,

        /** @brief Pipeline statistics queries.
         *  Vk: pipelineStatisticsQuery feature | D3D12: D3D12_QUERY_TYPE_PIPELINE_STATISTICS |
         *  GL: ARB_pipeline_statistics_query | WebGPU: pipeline-statistics-query (experimental) */
        PipelineStatistics,

        /** @brief Host-domain calibrated GPU timestamps.
         *  Vk: VK_EXT_calibrated_timestamps | D3D12: ID3D12CommandQueue::GetClockCalibration |
         *  GL: N/A | WebGPU: N/A */
        CalibratedTimestamps,

        // =====================================================================
        // GPU crash diagnostics
        // =====================================================================

        /** @brief Command stream checkpoints for post-crash analysis (NVIDIA).
         *  Vk: VK_NV_device_diagnostic_checkpoints | D3D12: DRED (Device Removed Extended Data) |
         *  GL: N/A | WebGPU: N/A */
        DiagnosticCheckpoints,

        /** @brief Device diagnostics config for crash dump generation (NVIDIA Aftermath).
         *  Vk: VK_NV_device_diagnostics_config | D3D12: Nsight Aftermath SDK (native D3D12 support) |
         *  GL: N/A | WebGPU: N/A */
        DiagnosticConfig,

        /** @brief GPU-side buffer marker writes for post-crash analysis (AMD).
         *  Vk: VK_AMD_buffer_marker | D3D12: ID3D12GraphicsCommandList2::WriteBufferImmediate |
         *  GL: N/A | WebGPU: N/A */
        BufferMarker,

        Count_,  ///< Sentinel — not a feature
    };

    /// Feature set with O(1) lookup via std::bitset. Zero heap allocation.
    class DeviceFeatureSet {
       public:
        DeviceFeatureSet() = default;

        /// Construct from initializer list: DeviceFeatureSet{Feature::A, Feature::B}
        DeviceFeatureSet(std::initializer_list<DeviceFeature> iFeatures) {
            for (auto f : iFeatures) {
                Add(f);
            }
        }

        void Add(DeviceFeature f) noexcept { bits_.set(static_cast<size_t>(f)); }

        void Remove(DeviceFeature f) noexcept { bits_.reset(static_cast<size_t>(f)); }

        [[nodiscard]] bool Has(DeviceFeature f) const noexcept { return bits_.test(static_cast<size_t>(f)); }

        [[nodiscard]] bool ContainsAll(const DeviceFeatureSet& iOther) const noexcept {
            return (bits_ & iOther.bits_) == iOther.bits_;
        }

        [[nodiscard]] DeviceFeatureSet Intersection(const DeviceFeatureSet& iOther) const noexcept {
            DeviceFeatureSet result;
            result.bits_ = bits_ & iOther.bits_;
            return result;
        }

        [[nodiscard]] DeviceFeatureSet Union(const DeviceFeatureSet& iOther) const noexcept {
            DeviceFeatureSet result;
            result.bits_ = bits_ | iOther.bits_;
            return result;
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return bits_.none(); }

        [[nodiscard]] size_t Count() const noexcept { return bits_.count(); }

        /// Iterate over all set features. Fn signature: void(DeviceFeature).
        template <typename Fn>
        void ForEach(Fn&& fn) const {
            for (size_t i = 0; i < kSize; ++i) {
                if (bits_.test(i)) {
                    fn(static_cast<DeviceFeature>(i));
                }
            }
        }

        bool operator==(const DeviceFeatureSet&) const = default;

       private:
        static constexpr size_t kSize = static_cast<size_t>(DeviceFeature::Count_);
        std::bitset<kSize> bits_;
    };

}  // namespace miki::rhi
