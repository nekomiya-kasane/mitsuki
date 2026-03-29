/** @brief Abstract pipeline factory for the miki RHI.
 *
 * Dual factory pattern:
 *   - MainPipelineFactory:   Tier1 (Task/Mesh shader, Vulkan 1.4+, D3D12)
 *   - CompatPipelineFactory: Tier2/3/4 (Vertex+MDI, GL, WebGPU)
 *
 * Selected at device creation based on CapabilityTier. Rendering code calls
 * IPipelineFactory::CreateXxxPass() — no if(compat) branches needed.
 *
 * CreateGeometryPass forwards to DeviceHandle::Dispatch → CreateGraphicsPipeline.
 * Other passes validate config and return sentinel handles — real pipelines are
 * owned by dedicated render pass objects (CsmShadows, Fxaa, etc.).
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "miki/rhi/Device.h"
#include "miki/rhi/Format.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiError.h"

namespace miki::rhi {

    // ===========================================================================
    // Pass descriptors
    // ===========================================================================

    /** @brief Geometry pass pipeline descriptor.
     *
     * Alias for GraphicsPipelineDesc — all pipeline config types (CullMode,
     * PolygonMode, CompareOp, BlendState, VertexLayout, etc.) are defined
     * in Pipeline.h alongside GraphicsPipelineDesc.
     *
     * IPipelineFactory::CreateGeometryPass() accepts this type and forwards
     * to DeviceHandle::Dispatch → CreateGraphicsPipeline().
     */
    using GeometryPassDesc = GraphicsPipelineDesc;

    /** @brief Shadow mode for factory dispatch. */
    enum class ShadowMode : uint8_t {
        VSM,
        CSM
    };

    /** @brief Shadow pass pipeline descriptor. */
    struct ShadowPassDesc {
        ShaderModuleHandle vertexShader = {};
        Format depthFormat = Format::D32_FLOAT;
        ShadowMode mode = ShadowMode::CSM;
        uint32_t cascadeCount = 4;
        uint32_t vsmPageCount = 2048;
    };

    /** @brief Order-independent transparency pass descriptor. */
    struct OITPassDesc {
        ShaderModuleHandle vertexShader = {};
        ShaderModuleHandle fragmentShader = {};
        std::array<Format, 8> colorFormats = {};
        uint32_t colorFormatCount = 0;
    };

    /** @brief AO mode for factory dispatch. */
    enum class AOPassMode : uint8_t {
        GTAO,
        SSAO
    };

    /** @brief Ambient occlusion pass descriptor. */
    struct AOPassDesc {
        Format colorFormat = Format::R8_UNORM;
        AOPassMode mode = AOPassMode::GTAO;
        uint32_t numSamples = 16;
        bool halfRes = true;
    };

    /** @brief AA mode for factory dispatch. */
    enum class AAPassMode : uint8_t {
        TAA,
        FXAA,
        None
    };

    /** @brief Anti-aliasing pass descriptor. */
    struct AAPassDesc {
        Format colorFormat = Format::RGBA8_UNORM;
        AAPassMode mode = AAPassMode::TAA;
        float blendFactor = 0.1f;
        uint32_t fxaaQuality = 29;
    };

    /** @brief Picking/selection pass descriptor. */
    struct PickPassDesc {
        Format colorFormat = Format::R32_UINT;
        Format depthFormat = Format::D32_FLOAT;
    };

    /** @brief Hidden line removal pass descriptor. */
    struct HLRPassDesc {
        ShaderModuleHandle vertexShader = {};
        ShaderModuleHandle fragmentShader = {};
        std::array<Format, 8> colorFormats = {};
        uint32_t colorFormatCount = 0;
        Format depthFormat = Format::D32_FLOAT;
    };

    // ===========================================================================
    // IPipelineFactory
    // ===========================================================================

    /** @brief Abstract pipeline factory interface.
     *
     * Rendering code calls CreateXxxPass() — the factory returns the
     * appropriate pipeline for the device's capability tier.
     */
    class IPipelineFactory {
       public:
        virtual ~IPipelineFactory() = default;

        IPipelineFactory(const IPipelineFactory&) = delete;
        auto operator=(const IPipelineFactory&) -> IPipelineFactory& = delete;
        IPipelineFactory(IPipelineFactory&&) = default;
        auto operator=(IPipelineFactory&&) -> IPipelineFactory& = default;

        /** @brief Create the appropriate factory for the device's capability tier.
         *  @param iDevice Type-erased device handle.
         *  @return MainPipelineFactory if Tier1, CompatPipelineFactory otherwise.
         */
        [[nodiscard]] static auto Create(DeviceHandle iDevice) -> std::unique_ptr<IPipelineFactory>;

        // --- Pass creation ---

        [[nodiscard]] virtual auto CreateGeometryPass(const GeometryPassDesc& iDesc) -> RhiResult<PipelineHandle> = 0;
        [[nodiscard]] virtual auto CreateShadowPass(const ShadowPassDesc& iDesc) -> RhiResult<PipelineHandle> = 0;
        [[nodiscard]] virtual auto CreateOITPass(const OITPassDesc& iDesc) -> RhiResult<PipelineHandle> = 0;
        [[nodiscard]] virtual auto CreateAOPass(const AOPassDesc& iDesc) -> RhiResult<PipelineHandle> = 0;
        [[nodiscard]] virtual auto CreateAAPass(const AAPassDesc& iDesc) -> RhiResult<PipelineHandle> = 0;
        [[nodiscard]] virtual auto CreatePickPass(const PickPassDesc& iDesc) -> RhiResult<PipelineHandle> = 0;
        [[nodiscard]] virtual auto CreateHLRPass(const HLRPassDesc& iDesc) -> RhiResult<PipelineHandle> = 0;

        /** @brief Get the capability tier this factory targets. */
        [[nodiscard]] virtual auto GetTier() const noexcept -> CapabilityTier = 0;

       protected:
        IPipelineFactory() = default;
    };

}  // namespace miki::rhi
