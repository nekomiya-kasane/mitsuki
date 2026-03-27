# T1a.5.1 — VulkanDevice (CreateFromExisting + CreateOwned + VMA)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: Vulkan Tier1 Backend
**Roadmap Ref**: `roadmap.md` L328 — `VulkanDevice` wrapping injected Vulkan context, VMA, Vulkan 1.4 core features
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.2 | IDevice + ICommandBuffer | Complete | `IDevice` interface, `Result<T>`, `TextureHandle`, `BufferHandle` |
| T1a.3.3 | GpuCapabilityProfile | Complete | `GpuCapabilityProfile`, `CapabilityTier` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/vulkan/VulkanDevice.h` | shared | **M** | `VulkanDevice` — `IDevice` implementation for Vulkan 1.4 Tier1. CreateFromExisting + CreateOwned. VMA allocator. |
| `src/miki/rhi/vulkan/VulkanDevice.cpp` | internal | L | Implementation: instance/device creation, VMA init, resource creation, handle management |
| `src/miki/rhi/vulkan/VulkanHandlePool.h` | shared | **M** | `VulkanHandlePool` — maps `Handle<Tag>` to native `VkImage`/`VkBuffer` etc. Generation tracking. |
| `tests/unit/test_vulkan_device.cpp` | internal | L | VulkanDevice creation, resource lifecycle (GTEST_SKIP on no-GPU) |

- **Error model**: `Result<T>` for all fallible operations. Vulkan errors mapped to `ErrorCode`.
- **Thread safety**: `VulkanDevice` is single-owner. Internal VMA allocator is thread-safe.
- **GPU constraints**: VMA creates own pools on injected device. Own descriptor pool. Own pipeline cache.
- **Invariants**: `CreateFromExisting` does NOT own the `VkDevice` — `Destroy()` releases only miki resources. `CreateOwned` owns and destroys the device. Feature validation: minimum feature set checked at creation.

### Downstream Consumers

- `VulkanDevice.h` (shared, heat **M**):
  - T1a.5.2 (same Phase): `VulkanCommandBuffer` uses `VulkanDevice` for native handle access
  - T1a.7.1 (same Phase): `OffscreenTarget` Vulkan path uses `VulkanDevice::CreateTexture`
  - T1a.9.2 (same Phase): `GlfwBootstrap` creates `VulkanDevice` via `CreateFromExisting`
  - Phase 1b: `Tier2_Compat` reuses `VulkanDevice` with restricted feature set

### Upstream Contracts

- T1a.3.2: `IDevice` (pure virtual interface), `IDevice::CreateFromExisting`, `IDevice::CreateOwned`, `TextureDesc`, `BufferDesc`, `NativeImageHandle`
- T1a.3.3: `GpuCapabilityProfile` (populated from `vkGetPhysicalDeviceFeatures2`), `CapabilityTier`

### Technical Direction

- **Vulkan 1.4 core features**: `synchronization2`, `dynamicRendering`, `maintenance4/5/6`, `pipelineRobustness`, `pushDescriptor`, subgroup clustered+rotate, 256B push constants, streaming transfers
- **VMA on injected device**: `vmaCreateAllocator` with externally provided `VkDevice` — separate pools, separate budget tracking
- **Shared device isolation**: Own `VmaAllocator`, own `VkDescriptorPool`, own `VkPipelineCache`. No contention with host.
- **Queue dedication**: `ExternalContext` includes queue family + index. If host can't dedicate, use timeline semaphore sync.
- **Handle pool**: `VulkanHandlePool<Tag, NativeType>` — generational pool mapping `Handle<Tag>` to `VkImage`/`VkBuffer`. O(1) alloc/free/lookup.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/vulkan/VulkanDevice.h` | shared | **M** | IDevice impl for Vulkan |
| Create | `src/miki/rhi/vulkan/VulkanDevice.cpp` | internal | L | Implementation |
| Create | `src/miki/rhi/vulkan/VulkanHandlePool.h` | shared | **M** | Handle<Tag> -> VkObject mapping |
| Create | `src/miki/rhi/vulkan/VulkanUtils.h` | internal | L | Vulkan error mapping, format conversion |
| Create | `tests/unit/test_vulkan_device.cpp` | internal | L | Tests (GTEST_SKIP on no-GPU) |

## Steps

- [x] **Step 1**: VulkanDevice class + CreateFromExisting + CreateOwned
      **Files**: `VulkanDevice.h` (shared M), `VulkanDevice.cpp` (internal L)
      Implement `IDevice` interface. VMA init. Feature validation from `vkGetPhysicalDeviceFeatures2`. Populate `GpuCapabilityProfile`. Queue selection.
      **Acceptance**: compiles; `CreateOwned` succeeds on Vulkan-capable machine
      `[verify: compile]`

- [x] **Step 2**: VulkanHandlePool + resource creation
      **Files**: `VulkanHandlePool.h` (shared M), `VulkanDevice.cpp` (internal L — extend)
      Implement `CreateTexture`, `CreateBuffer`, `DestroyTexture`, `DestroyBuffer`, `ImportSwapchainImage`. Handle generation tracking.
      **Acceptance**: resource create/destroy lifecycle correct
      `[verify: compile]`

- [x] **Step 3**: Unit tests
      **Files**: `tests/unit/test_vulkan_device.cpp` (internal L)
      Cover: CreateOwned success, CreateFromExisting with valid context, capability profile populated, CreateTexture/DestroyTexture lifecycle, ImportSwapchainImage, handle generation increment. All GTEST_SKIP on no-GPU.
      **Acceptance**: tests pass on Vulkan-capable machine, skip on no-GPU
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(VulkanDevice, CreateOwnedSucceeds)` | Unit | Step 1 — device creation | 1 |
| `TEST(VulkanDevice, CapabilityProfilePopulated)` | Unit | Step 1 — features detected | 1 |
| `TEST(VulkanDevice, CreateTextureLifecycle)` | Unit | Step 2 — create + destroy | 2 |
| `TEST(VulkanDevice, HandleGenerationIncrement)` | Unit | Step 2 — generation tracking | 2 |
| `TEST(VulkanDevice, ImportSwapchainImage)` | Unit | Step 2 — external image import | 2 |

## Design Decisions

- **VulkanHandlePool generational design**: Flat array with generation counters. O(1) alloc/free/lookup. Stale handle access returns `nullopt`. Template `VulkanHandlePool<Tag, NativeType>` for type safety.
- **VulkanDeviceFactory**: Static helper class `VulkanDeviceFactory` encapsulates instance/device creation logic for `CreateOwned`. Separated from `VulkanDevice` to keep the class focused on `IDevice` interface implementation.
- **VMA on injected device**: `VmaAllocator` created with the device's `VkPhysicalDevice` and `VkDevice`, using `VMA_DYNAMIC_VULKAN_FUNCTIONS`. Separate from any host allocator.
- **VulkanUtils.h**: Vulkan error code to `ErrorCode` mapping, format conversion between `miki::rhi::Format` and `VkFormat`.
- **GTEST_SKIP on no-GPU**: All tests that require Vulkan hardware use `GTEST_SKIP()` when `VulkanDevice::CreateOwned` fails.
- **CreateFromExisting with null handles**: Returns `ErrorCode::InvalidArgument` (not crash). Tested.

## Implementation Notes

- Contract check: PASS. All `IDevice` virtual methods implemented.
- 9 tests total, all passing on Vulkan-capable machine.
- Additional files not in spec: `VulkanDeviceFactory.h` (factory helper), `VulkanUtils.h` (error mapping + format conversion).
- `StubMethodsReturnNotImplemented` test verifies that unimplemented pipeline/sampler methods return `NotImplemented`.
