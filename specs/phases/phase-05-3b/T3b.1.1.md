# T3b.1.1 — Phase 3a Tech Debt A: CompiledGraph Immutability + EnvironmentMap RAII + isCubemap Flag

**Phase**: 05-3b (Shadows, Post-Processing & Visual Regression)
**Component**: 1 — Phase 3a Tech Debt Resolution
**Roadmap Ref**: `roadmap.md` — Phase 3b tech debt; `phase-04-3a.md` D2/D4/D6
**Status**: Complete
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | Phase 3a Components 1-7 | Complete | `CompiledGraph`, `EnvironmentMap`, `VulkanDevice` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `CompiledGraph.h` | **public** | **H** | Immutable `CompiledGraph` — transient handles moved to `TransientResourceSet` returned by Executor |
| `EnvironmentMap.cpp` | internal | L | RAII guard pattern for multi-resource creation chain |
| `VulkanDevice.cpp` | internal | L | `TextureDesc.isCubemap` flag replaces `arrayLayers==6` heuristic |
| `RhiDescriptors.h` | **public** | **M** | `TextureDesc::isCubemap` bool field (backward-compatible addition) |

- **Error model**: no new error paths; structural refactor only
- **Thread safety**: unchanged (single-threaded render graph execution)
- **Invariants**: `CompiledGraph` is const after `Compile()` returns; `EnvironmentMap::CreatePreset` either succeeds fully or cleans up all intermediate resources

### Downstream Consumers

- `CompiledGraph.h` (**public** H):
  - Phase 3b: `RenderGraphExecutor` returns `TransientResourceSet` instead of mutating `CompiledGraph`
  - Phase 4: `RenderGraphCompiler::GetTransientAliasing()` reads immutable compiled data
- `RhiDescriptors.h` (**public** M):
  - Phase 3b+: all `CreateTexture` calls for cubemaps use `isCubemap = true`
  - Phase 6a: GPU-driven pipeline cubemap detection uses flag

### Upstream Contracts

- Phase 3a: `CompiledGraph` from `RenderGraphCompiler::Compile()`
- Phase 3a: `EnvironmentMap::CreatePreset()` multi-step creation
- Phase 3a: `VulkanDevice::CreateTexture()` cubemap path

### Technical Direction

- **D2 (CompiledGraph)**: extract `transientHandles_` vector from `CompiledGraph` into a separate `TransientResourceSet` struct returned by `Execute()`. `CompiledGraph` becomes fully const after compilation.
- **D4 (EnvironmentMap RAII)**: wrap intermediate resources (cubemap, prefiltered mips, SH buffer) in a local `ScopeGuard` or `std::unique_ptr` with custom deleter. On success, release ownership to the `EnvironmentMap` object.
- **D6 (isCubemap)**: add `bool isCubemap = false` to `TextureDesc`. Vulkan backend uses this instead of `arrayLayers == 6`. All existing `CreateTexture` calls for cubemaps updated.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/rendergraph/CompiledGraph.h` | **public** | **H** | Remove mutable transient data; add TransientResourceSet |
| Modify | `include/miki/rhi/RhiDescriptors.h` | **public** | **M** | Add `TextureDesc::isCubemap` |
| Modify | `src/miki/rendergraph/RenderGraphExecutor.cpp` | internal | L | Return TransientResourceSet |
| Modify | `src/miki/rendergraph/EnvironmentMap.cpp` | internal | L | RAII guards |
| Modify | `src/miki/rhi/vulkan/VulkanDevice.cpp` | internal | L | Use isCubemap flag |
| Modify | `tests/unit/test_render_graph_executor.cpp` | internal | L | Verify CompiledGraph constness |

## Steps

- [x] **Step 1**: Add `isCubemap` to `TextureDesc` in `RhiDescriptors.h`
      **Files**: `RhiDescriptors.h` (**public** M)

      **Signatures** (`RhiDescriptors.h` addition):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `TextureDesc::isCubemap` | `bool isCubemap = false` | backward-compatible default |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Update VulkanDevice to use `isCubemap` flag
      **Files**: `VulkanDevice.cpp` (internal L)
      Replace `desc.arrayLayers == 6` check with `desc.isCubemap`. Update all cubemap creation sites (EnvironmentMap, test code) to set `isCubemap = true`.
      **Acceptance**: existing cubemap tests pass
      `[verify: test]`

- [x] **Step 3**: Extract `TransientResourceSet` from `CompiledGraph`
      **Files**: `CompiledGraph.h` (**public** H), `RenderGraphExecutor.cpp` (internal L)

      **Signatures** (`CompiledGraph.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `TransientResourceSet` | `{ std::vector<TextureHandle> textures; std::vector<BufferHandle> buffers; }` | value type |

      Remove `transientTextures_` / `transientBuffers_` from `CompiledGraph`. `Execute()` returns `TransientResourceSet` as out-parameter or return value.
      **Acceptance**: render graph tests pass; `CompiledGraph` has no mutable state after `Compile()`
      `[verify: test]`

- [x] **Step 4**: Add RAII guards to `EnvironmentMap::CreatePreset`
      **Files**: `EnvironmentMap.cpp` (internal L)
      Wrap each intermediate resource in a scope guard. On success, release all guards. On failure, guards auto-destroy.
      **Acceptance**: existing EnvironmentMap tests pass; error injection test verifies no leaks
      `[verify: test]`

- [x] **Step 5**: Update tests
      **Files**: `test_render_graph_executor.cpp`, `test_vulkan_device.cpp` (internal L)
      Add: `CompiledGraph_IsImmutableAfterCompile`, `EnvironmentMap_ErrorPathCleansUp`, `CreateTexture_CubemapFlag`.
      **Acceptance**: all new + existing tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(CompiledGraph, IsImmutableAfterCompile)` | State | no mutable members after Compile | 3 |
| `TEST(RenderGraphExecutor, ReturnsTransientResourceSet)` | Positive | Execute returns transient handles | 3 |
| `TEST(EnvironmentMap, ErrorPathCleansUp)` | Error | error injection → no resource leaks | 4 |
| `TEST(VulkanDevice, CreateTexture_CubemapFlag)` | Positive | isCubemap=true creates cubemap | 2 |
| `TEST(VulkanDevice, CreateTexture_ArrayLayers6_NotCubemap)` | Boundary | arrayLayers=6 without isCubemap is array, not cube | 2 |
| `TEST(TextureDesc, IsCubemapDefaultFalse)` | Positive | default value is false | 1 |
| `TEST(TransientResourceSet, MoveSemantics)` | State | move transfers ownership, source empty | 3 |
| `TEST(CompiledGraph, DoubleCompile_ReturnsError)` | Error | calling Compile twice on same builder returns error | 3 |
| `TEST(EnvironmentMap, PartialFailure_IntermediatesCleaned)` | Error | fail at mip 3 cleans cubemap + mips 0-2 | 4 |
| `TEST(TechDebtA, EndToEnd_CompileExecuteWithTransients)` | Integration | full RG cycle with immutable CompiledGraph + returned TransientResourceSet | 3-5 |

## Design Decisions

- D6: `isCubemap` added as backward-compatible bool field with default false. Vulkan backend reads this instead of `arrayLayers==6` heuristic. 6-layer non-cubemap arrays (e.g., CSM cascade atlas) now correctly treated as arrays.
- D2: `TransientResourceSet` extracted from `CompiledGraph`. `Execute()` signature changed from `CompiledGraph&` to `const CompiledGraph&`, returns `expected<TransientResourceSet, ErrorCode>`. All 7 call sites updated (2 demos, 3 integration tests, 1 unit test helper, 1 impl).
- D4: `TempResources` RAII struct in `EnvironmentMap::CreatePreset` auto-destroys all registered temp resources on scope exit. Error paths reduced from 12 manual chain-destroys to simple `return result;`. Success path calls `release()` + explicit `cleanup()`.

## Implementation Notes

Contract check: PASS (10/10 items verified). All downstream consumers satisfied.

No pitfalls encountered — no entry needed in `.windsurf/pitfalls.md`.
