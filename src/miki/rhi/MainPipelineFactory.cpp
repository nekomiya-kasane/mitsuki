/** @brief MainPipelineFactory — Tier1 pipeline factory.
 *
 * Uses Task/Mesh shader path. CreateGeometryPass forwards to
 * DeviceHandle::Dispatch -> CreateGraphicsPipeline. Other passes validate
 * config and return sentinel handles (real pipelines are owned by dedicated
 * render pass objects like VsmShadowRender, Taa, etc.).
 */

#include "miki/rhi/IPipelineFactory.h"

#include "miki/rhi/backend/AllBackends.h"

namespace miki::rhi {

    class MainPipelineFactory final : public IPipelineFactory {
       public:
        explicit MainPipelineFactory(DeviceHandle iDevice) : device_(iDevice) {}

        [[nodiscard]] auto CreateGeometryPass(const GeometryPassDesc& iDesc) -> RhiResult<PipelineHandle> override {
            return device_.Dispatch([&](auto& dev) { return dev.CreateGraphicsPipeline(iDesc); });
        }

        [[nodiscard]] auto CreateShadowPass(const ShadowPassDesc& iDesc) -> RhiResult<PipelineHandle> override {
            if (iDesc.mode == ShadowMode::VSM && iDesc.vsmPageCount == 0) {
                return std::unexpected(RhiError::InvalidParameter);
            }
            if (iDesc.mode == ShadowMode::CSM && iDesc.cascadeCount == 0) {
                return std::unexpected(RhiError::InvalidParameter);
            }
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        [[nodiscard]] auto CreateOITPass(const OITPassDesc& iDesc) -> RhiResult<PipelineHandle> override {
            if (iDesc.colorFormatCount == 0) {
                return std::unexpected(RhiError::InvalidParameter);
            }
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        [[nodiscard]] auto CreateAOPass(const AOPassDesc& iDesc) -> RhiResult<PipelineHandle> override {
            (void)iDesc;
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        [[nodiscard]] auto CreateAAPass(const AAPassDesc& iDesc) -> RhiResult<PipelineHandle> override {
            if (iDesc.mode == AAPassMode::None) {
                return PipelineHandle{};
            }
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        [[nodiscard]] auto CreatePickPass(const PickPassDesc& iDesc) -> RhiResult<PipelineHandle> override {
            (void)iDesc;
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        [[nodiscard]] auto CreateHLRPass(const HLRPassDesc& iDesc) -> RhiResult<PipelineHandle> override {
            if (iDesc.colorFormatCount == 0) {
                return std::unexpected(RhiError::InvalidParameter);
            }
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        [[nodiscard]] auto GetTier() const noexcept -> CapabilityTier override {
            return device_.Dispatch([](const auto& dev) { return dev.GetCapabilities().tier; });
        }

       private:
        DeviceHandle device_;
    };

    // ---------------------------------------------------------------------------
    // Forward declaration — defined in CompatPipelineFactory.cpp
    // ---------------------------------------------------------------------------
    auto CreateCompatPipelineFactory(DeviceHandle iDevice) -> std::unique_ptr<IPipelineFactory>;

    auto IPipelineFactory::Create(DeviceHandle iDevice) -> std::unique_ptr<IPipelineFactory> {
        auto tier = iDevice.Dispatch([](const auto& dev) { return dev.GetCapabilities().tier; });
        if (tier == CapabilityTier::Tier1_Vulkan || tier == CapabilityTier::Tier1_D3D12) {
            return std::make_unique<MainPipelineFactory>(iDevice);
        }
        return CreateCompatPipelineFactory(iDevice);
    }

}  // namespace miki::rhi
