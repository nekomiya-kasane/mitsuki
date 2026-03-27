# T1a.3.3 — GpuCapabilityProfile + CapabilityTier

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: RHI Core — Injection Architecture
**Roadmap Ref**: `roadmap.md` L372 — `GpuCapabilityProfile` feature detection, `CapabilityTier` enum
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.1 | RhiTypes + Handle + ExternalContext + Format | Not Started | `Format`, `BackendType` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/GpuCapabilityProfile.h` | **public** | **M** | `GpuCapabilityProfile` struct + `CapabilityTier` enum. Feature detection for mesh shader, RT, VRS, 64-bit atomics, descriptor buffer, cooperative matrix. |
| `tests/unit/test_capabilities.cpp` | internal | L | Tier detection, feature query tests |

- **Error model**: No errors — `GpuCapabilityProfile` is populated during device init, immutable after.
- **Thread safety**: Immutable after construction — thread-safe.
- **GPU constraints**: N/A (CPU-side metadata)
- **Invariants**: `CapabilityTier` is determined once at device creation and never changes. D3D12 maps to `Tier1_Full`. Vulkan 1.4 with mesh shader maps to `Tier1_Full`. Vulkan 1.1 without mesh shader maps to `Tier2_Compat`.

### Downstream Consumers

- `GpuCapabilityProfile.h` (**public**, heat **M**):
  - T1a.5.1 (same Phase): `VulkanDevice` populates `GpuCapabilityProfile` from `vkGetPhysicalDeviceFeatures2`
  - T1a.6.1 (same Phase): `D3D12Device` populates from `D3D12_FEATURE_DATA_D3D12_OPTIONS7`
  - T1a.4.1 (same Phase): `IPipelineFactory` dispatches Main vs Compat based on `CapabilityTier`
  - Phase 1b: GL/WebGPU devices populate their tier profiles
  - Phase 3b: VRS pass checks `hasVariableRateShading`
  - Phase 6a: GPU culling checks `hasMeshShader`

### Upstream Contracts

- T1a.3.1: `BackendType` enum, `Format` enum

### Technical Direction

- **Tier system**: 4 tiers — `Tier1_Full` (Vulkan 1.4 + mesh shader + RT), `Tier2_Compat` (Vulkan 1.1, no mesh/RT), `Tier3_WebGPU`, `Tier4_OpenGL`. D3D12 maps to `Tier1_Full`.
- **Feature booleans**: Individual capability flags for fine-grained queries (`hasMeshShader`, `hasRayTracing`, `hasVRS`, etc.) in addition to the tier enum.
- **Immutable after init**: Profile populated during `IDevice` creation, exposed via `IDevice::GetCapabilities()`, never mutated.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rhi/GpuCapabilityProfile.h` | **public** | **M** | Feature detection + tier enum |
| Create | `tests/unit/test_capabilities.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define `CapabilityTier` + `GpuCapabilityProfile`
      **Files**: `GpuCapabilityProfile.h` (**public** M)

      **Signatures**:

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `CapabilityTier` | `enum class : uint8_t { Tier1_Full, Tier2_Compat, Tier3_WebGPU, Tier4_OpenGL }` | — |
      | `GpuCapabilityProfile` | `{ tier:CapabilityTier, backendType:BackendType, hasMeshShader:bool, hasRayTracing:bool, hasVariableRateShading:bool, has64BitAtomics:bool, hasDescriptorBuffer:bool, hasCooperativeMatrix:bool, hasTimelineSemaphore:bool, hasDynamicRendering:bool, hasPushDescriptors:bool, maxPushConstantSize:u32, maxComputeWorkGroupSize[3]:u32, maxBoundDescriptorSets:u32, deviceName:string, driverVersion:string, vendorId:u32, deviceId:u32 }` | Immutable after init |
      | `GpuCapabilityProfile::SupportsTier` | `(CapabilityTier) const noexcept -> bool` | `[[nodiscard]]` — true if device tier >= requested |
      | `GpuCapabilityProfile::SupportsFormat` | `(Format) const noexcept -> bool` | `[[nodiscard]]` |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::rhi` |
      | Immutability | Constructed once, no setters |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Unit tests
      **Files**: `tests/unit/test_capabilities.cpp` (internal L)
      Cover: tier comparison, SupportsTier transitivity, default construction, feature flag combinations.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(Capabilities, Tier1SupportsTier2)` | Unit | Step 1 — tier ordering | 1 |
| `TEST(Capabilities, Tier4DoesNotSupportTier1)` | Unit | Step 1 — tier ordering | 1 |
| `TEST(Capabilities, DefaultConstruction)` | Unit | Step 1 — zero-initialized | 1 |
| `TEST(Capabilities, FeatureFlags)` | Unit | Step 1 — individual flag queries | 1 |

## Design Decisions

- **Tier ordering by enum value**: Lower `uint8_t` value = higher capability. `SupportsTier` uses `<=` comparison, so Tier1 supports all tiers, Tier4 supports only itself.
- **Default tier = Tier4_OpenGL**: Safest default — assumes lowest capability. Backends upgrade during init.
- **Default backend = Mock**: Safe default for testing. Real backends override.
- **SupportsFormat default = true for non-Undefined**: Simple baseline. Backends can extend with a format support table if needed.
- **std::array for maxComputeWorkGroupSize**: Fixed-size, value-semantic, constexpr-friendly.
- **IDevice.h now includes GpuCapabilityProfile.h**: Replaced forward declaration with full include since `GetCapabilities()` returns `const&`.

## Implementation Notes

- Contract check: PASS (23/23 items)
- Build: 0 errors, 7 targets rebuilt
- Tests: 54/54 pass (7 Capabilities + 11 IDevice + 16 RhiTypes + 17 Foundation + 3 Toolchain)
- No TODO/STUB/FIXME in task files
- This is the last task in Component 3 (RHI Core — Injection Architecture)
