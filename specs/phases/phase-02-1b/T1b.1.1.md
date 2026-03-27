# T1b.1.1 — Vulkan Compat Tier Detection + Feature Gating

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: Vulkan Tier2 Compat Backend
**Roadmap Ref**: `roadmap.md` L1145 — Same `VulkanDevice` with `Tier2_Compat` profile
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.5.1 | VulkanDevice | Complete | `VulkanDevice`, `VulkanExternalContext`, `GpuCapabilityProfile` |
| T1a.5.2 | VulkanCommandBuffer | Complete | `VulkanCommandBuffer` — draw path used for compat validation |
| T1a.3.3 | GpuCapabilityProfile | Complete | `CapabilityTier::Tier2_Compat`, feature flags |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/vulkan/VulkanDevice.cpp` | internal | **M** | Compat tier detection logic in `VulkanDevice::CreateOwned()` / `CreateFromExisting()` |
| `tests/unit/test_vulkan_compat.cpp` | internal | L | Compat tier detection tests |

- **Error model**: `std::expected<T, ErrorCode>` — same as VulkanDevice
- **Thread safety**: same as VulkanDevice (single-owner)
- **Invariants**: when device is created with `DeviceConfig{.preferredBackend = BackendType::Vulkan}` on hardware lacking mesh shader / RT / VRS / descriptor buffer / 64-bit atomics, `GetCapabilities().tier` must return `Tier2_Compat`; when all Tier1 features present, returns `Tier1_Full`.

### Downstream Consumers

- `src/miki/rhi/vulkan/VulkanDevice.cpp` (internal, heat **M**):
  - T1b.7.1 (same Phase): triangle demo with `--backend compat` forces Tier2_Compat
  - Phase 11b: compat pipeline hardening + SwiftShader CI relies on correct tier detection
  - Phase 2: `IPipelineFactory::Create(IDevice&)` dispatches Main vs Compat based on `GetCapabilities().tier`

### Upstream Contracts

- T1a.5.1: `VulkanDevice::CreateOwned(DeviceConfig) -> Result<unique_ptr<IDevice>>`
  - Source: `src/miki/rhi/vulkan/VulkanDevice.cpp`
- T1a.3.3: `GpuCapabilityProfile` struct with `tier`, `hasMeshShader`, `hasRayTracing`, etc.
  - Source: `include/miki/rhi/GpuCapabilityProfile.h`

### Technical Direction

- **No new class**: Vulkan Compat is NOT a separate device class. Same `VulkanDevice` with different `GpuCapabilityProfile` values. Feature gating via capability flags, not inheritance.
- **Tier detection algorithm**: query `VkPhysicalDeviceFeatures2` chain for mesh shader, RT pipeline, VRS, descriptor buffer, 64-bit atomics. If ANY Tier1 feature is missing → `Tier2_Compat`.
- **Force-compat mode**: `DeviceConfig` may need a `forceCompat` flag (or `--backend compat` CLI) to test compat path on Tier1 hardware. This is critical for development — most dev machines have Tier1 GPUs.
- **Vulkan 1.1 validation**: compat path must NOT use any Vulkan 1.2+ features. Dynamic rendering requires VK_KHR_dynamic_rendering on 1.1 — if not available, fallback to VkRenderPass (Phase 3a concern, not Phase 1b).
- **DrawIndexed path**: compat uses `vkCmdDrawIndexed` (vertex shader), not `vkCmdDrawMeshTasksEXT`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `src/miki/rhi/vulkan/VulkanDevice.cpp` | internal | **M** | Add compat tier detection + force-compat mode |
| Modify | `include/miki/rhi/RhiTypes.h` | **public** | **H** | Add `forceCompatTier` to `DeviceConfig` (or use backend string) |
| Create | `tests/unit/test_vulkan_compat.cpp` | internal | L | Tier detection tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Add `forceCompatTier` to `DeviceConfig`
      **Files**: `RhiTypes.h` (**public** H)

      **Signatures** (`RhiTypes.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `DeviceConfig::forceCompatTier` | `bool forceCompatTier = false` | — |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Implement Vulkan compat tier detection
      **Files**: `VulkanDevice.cpp` (internal)
      Query physical device features. If mesh shader / RT / VRS / descriptor buffer / 64-bit atomics missing → `Tier2_Compat`. If `forceCompatTier` is set → force `Tier2_Compat` regardless.
      **Acceptance**: device creation succeeds, tier is correct
      `[verify: compile]`

- [x] **Step 3**: Write compat tier detection tests
      **Files**: `test_vulkan_compat.cpp` (internal L)
      Tests: tier detection with mock features, force-compat flag, capability profile fields.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(VulkanCompat, ForceCompatReturnsTier2)` | Positive | force-compat flag → Tier2_Compat | 2-3 |
| `TEST(VulkanCompat, FullFeaturesReturnsTier1)` | Positive | all features present → Tier1_Full | 2-3 |
| `TEST(VulkanCompat, MeshShaderDisabledInCompat)` | State | `hasMeshShader == false` in compat profile | 2-3 |
| `TEST(VulkanCompat, RTDisabledInCompat)` | State | `hasRayTracing == false` in compat profile | 2-3 |
| `TEST(VulkanCompat, CompatDeviceCreatesSuccessfully)` | Positive | `CreateOwned` with force-compat succeeds | 2-3 |
| `TEST(VulkanCompat, InvalidBackendTypeRejected)` | Error | non-Vulkan backend with force-compat → error | 2-3 |
| `TEST(VulkanCompat, ForceCompatOnAlreadyCompat)` | Boundary | force-compat on Tier2 hardware is idempotent | 2-3 |
| `TEST(VulkanCompat, EndToEnd_CompatDrawPath)` | **Integration** | create device (compat) → create cmd buf → record Draw → submit → WaitIdle | 1-3 |

## Design Decisions

- **No new class**: Vulkan Compat is the same `VulkanDevice` with `forceCompatTier_` flag. `PopulateCapabilities()` uses this flag + hardware feature query to determine tier. No inheritance, no separate code path for device creation.
- **Tier1 feature set**: mesh shader, ray tracing, VRS, descriptor buffer, 64-bit atomics. ALL must be present for Tier1_Full. If ANY missing → Tier2_Compat.
- **Feature masking in compat**: when tier = Tier2_Compat, `hasMeshShader`, `hasRayTracing`, `hasVariableRateShading`, `hasDescriptorBuffer` are forcibly set to `false`. This ensures downstream code that checks individual feature flags never accidentally uses unavailable paths.
- **`forceCompatTier` in `DeviceConfig`**: public API field (`bool forceCompatTier = false`). Set by `--backend compat` CLI or programmatically. Critical for testing compat path on Tier1 hardware during development.
- **`CreateFromExisting` does NOT set `forceCompatTier_`**: external context injection doesn't pass a `DeviceConfig`. If needed in future, the host app should configure the external device's capabilities itself.

## Implementation Notes

- Contract check: PASS (8/8 items match)
- Test count: 8 (3 Positive, 1 Error, 1 Boundary, 2 State, 1 Integration)
- Build: 0 errors on both `debug` and `debug-vulkan` presets
- No regressions: 22/22 Vulkan tests pass (14 existing + 8 new)
- Total test count after this task: 244 (236 + 8, on `debug-vulkan` preset)
- Component 1 (Vulkan Tier2 Compat Backend): DONE — single task, complete
