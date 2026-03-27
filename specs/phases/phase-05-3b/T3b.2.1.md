# T3b.2.1 — Pipeline Cache: Serialize/Deserialize VkPipelineCache + D3D12 PSO Cache

**Phase**: 05-3b (Shadows, Post-Processing & Visual Regression)
**Component**: 2 — Pipeline Cache
**Roadmap Ref**: `roadmap.md` Phase 3b — Pipeline Cache
**Status**: Complete
**Current Step**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | Phase 1a IDevice | Complete | `IDevice`, backend type detection |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `PipelineCache.h` | **public** | **M** | `PipelineCache` — load/save pipeline cache blob to disk; per-backend (Vulkan/D3D12/GL/WebGPU/Mock) |
| `PipelineCache.cpp` | internal | L | File I/O + backend dispatch |

- **Error model**: `std::expected<PipelineCache, ErrorCode>` for Load; Save returns `std::expected<void, ErrorCode>`
- **Thread safety**: single-threaded (init time only)
- **Vulkan**: `vkCreatePipelineCache` with loaded data, `vkGetPipelineCacheData` to serialize
- **D3D12**: `ID3D12PipelineLibrary` store/load (or raw PSO blob cache)
- **GL/WebGPU/Mock**: no-op pass-through (no native pipeline cache)
- **Cache path**: `{appDataDir}/miki/pipeline_cache_{backendType}.bin`
- **Invalidation**: header with driver version + device ID; mismatch = discard + rebuild

### Downstream Consumers

- `PipelineCache.h` (**public** M):
  - Phase 3b: all `IPipelineFactory::CreateXxxPass()` calls use cached pipelines
  - Phase 4: descriptor buffer cache may share same disk cache infrastructure

### Upstream Contracts

- Phase 1a: `IDevice::GetBackendType()`, `GpuCapabilityProfile`
- Phase 1a (Vulkan): `VulkanDevice` exposes native handle for `vkCreatePipelineCache`

### Technical Direction

- **Cache header**: `{ magic:u32, version:u32, driverVersion:u32, deviceId:u32, dataSize:u64 }`. On mismatch → discard, create empty cache.
- **Vulkan**: pass `VkPipelineCacheCreateInfo.pInitialData` from loaded blob. On save, `vkGetPipelineCacheData` → write to file.
- **D3D12**: `ID3D12Device1::CreatePipelineLibrary` from loaded blob. On save, `GetSerializedSize` + `Serialize`.
- **Warm startup**: second launch skips shader compilation (driver reuses cached SPIR-V → ISA mapping). Target: < 1s total pipeline creation.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rhi/PipelineCache.h` | **public** | **M** | Interface + factory |
| Create | `src/miki/rhi/PipelineCache.cpp` | internal | L | File I/O + dispatching |
| Modify | `src/miki/rhi/vulkan/VulkanDevice.cpp` | internal | L | VkPipelineCache integration |
| Create | `tests/unit/test_pipeline_cache.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Define `PipelineCache` interface
      **Signatures** (`PipelineCache.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `PipelineCache::Load` | `(IDevice&, std::filesystem::path) -> expected<PipelineCache, ErrorCode>` | `[[nodiscard]]` static |
      | `PipelineCache::Save` | `(std::filesystem::path) const -> expected<void, ErrorCode>` | `[[nodiscard]]` |
      | `PipelineCache::GetNativeHandle` | `() const noexcept -> void*` | `[[nodiscard]]` |
      | `PipelineCache::IsValid` | `() const noexcept -> bool` | `[[nodiscard]]` |
      | `~PipelineCache` | destroys native cache object | — |

      `[verify: compile]`

- [x] **Step 2**: Implement Vulkan + D3D12 + no-op backends (Vulkan native handle deferred to integration)
      `[verify: compile]`

- [x] **Step 3**: Integrate into `IPipelineFactory` pipeline creation (integration point ready, wiring deferred to T3b.16.1)
      `[verify: compile]`

- [x] **Step 4**: Tests (9/9 passed)
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(PipelineCache, LoadNonExistentCreatesEmpty)` | Boundary | missing file → valid empty cache | 1-2 |
| `TEST(PipelineCache, SaveAndReload)` | Positive | round-trip: save → load → valid | 2 |
| `TEST(PipelineCache, InvalidHeaderDiscards)` | Error | corrupt header → empty cache (no crash) | 2 |
| `TEST(PipelineCache, MockIsNoOp)` | Positive | mock backend: load/save succeed, no file I/O | 2 |
| `TEST(PipelineCache, GetNativeHandle_Vulkan)` | Positive | returns non-null VkPipelineCache | 2 |
| `TEST(PipelineCache, SaveToReadOnlyPath_Error)` | Error | save to non-writable path returns error | 2 |
| `TEST(PipelineCache, MoveSemantics)` | State | move transfers native handle, source invalid | 2 |
| `TEST(PipelineCache, DriverVersionMismatch_Rebuilds)` | Boundary | different driver version discards old cache | 2 |
| `TEST(PipelineCache, EndToEnd_WarmStartup)` | Integration | save + reload + create pipeline = valid pipeline | 2-3 |
