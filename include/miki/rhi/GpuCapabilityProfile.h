/** @brief GPU capability detection for the miki RHI.
 *
 * GpuCapabilityProfile is populated once during IDevice creation and is
 * immutable afterward. Thread-safe by immutability.
 *
 * CapabilityTier provides coarse classification:
 *   - Tier1_Full:   Vulkan 1.4 + mesh shader + RT, or D3D12
 *   - Tier2_Compat: Vulkan 1.1, no mesh/RT
 *   - Tier3_WebGPU: WebGPU backend
 *   - Tier4_OpenGL: OpenGL fallback
 *
 * Individual feature booleans provide fine-grained queries beyond the tier.
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "miki/rhi/Format.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

    /** @brief Coarse GPU capability classification. */
    enum class CapabilityTier : uint8_t {
        Tier1_Full,
        Tier2_Compat,
        Tier3_WebGPU,
        Tier4_OpenGL,
    };

    /** @brief GPU capability profile — immutable after device creation.
     *
     * Populated by backend-specific device init code. Exposed via
     * IDevice::GetCapabilities(). Never mutated after construction.
     *
     * enabledFeatures is the single source of truth for feature queries.
     * The has*() convenience accessors delegate to enabledFeatures.
     */
    struct GpuCapabilityProfile {
        // --- Tier & backend ---
        CapabilityTier tier = CapabilityTier::Tier4_OpenGL;
        BackendType backendType = BackendType::Mock;
        DeviceFeatureSet enabledFeatures;  ///< Actual enabled features (single source of truth)

        // --- Convenience accessors (delegate to enabledFeatures) ---
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

        // --- General limits ---
        uint32_t maxPushConstantSize = 128;
        std::array<uint32_t, 3> maxComputeWorkGroupSize = {128, 128, 64};
        uint32_t maxBoundDescriptorSets = 4;

        // --- Mesh shader limits (Tier1 only, 0 = not supported) ---
        std::array<uint32_t, 3> maxMeshWorkGroupSize = {0, 0, 0};
        uint32_t maxMeshWorkGroupInvocations = 0;
        std::array<uint32_t, 3> maxTaskWorkGroupSize = {0, 0, 0};
        uint32_t maxTaskWorkGroupInvocations = 0;
        uint32_t maxMeshOutputVertices = 0;
        uint32_t maxMeshOutputPrimitives = 0;
        uint32_t maxTaskPayloadSize = 0;
        uint32_t maxMeshSharedMemorySize = 0;
        uint32_t maxMeshPayloadAndSharedMemorySize = 0;

        // --- Device info ---
        std::string deviceName = {};
        std::string driverVersion = {};
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;

        /** @brief Check if this device's tier is at least as capable as iRequested.
         *
         * Tier ordering: Tier1_Full (0) > Tier2_Compat (1) > Tier3_WebGPU (2) > Tier4_OpenGL (3).
         * Lower enum value = higher capability.
         *
         *  @param iRequested The minimum tier to check against.
         *  @return True if this device's tier meets or exceeds the requested tier.
         */
        [[nodiscard]] constexpr auto SupportsTier(CapabilityTier iRequested) const noexcept -> bool {
            return static_cast<uint8_t>(tier) <= static_cast<uint8_t>(iRequested);
        }

        /** @brief Check if this device supports a given format.
         *
         * Default implementation returns true for all non-Undefined formats.
         * Backends may override this via a populated format support table.
         *
         *  @param iFormat The format to query.
         *  @return True if the format is supported.
         */
        [[nodiscard]] constexpr auto SupportsFormat(Format iFormat) const noexcept -> bool {
            return iFormat != Format::Undefined;
        }
    };

}  // namespace miki::rhi
