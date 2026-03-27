# T3a.1.1 — RenderGraphBuilder: Pass & Resource Declaration API

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 1 — Render Graph
**Roadmap Ref**: `roadmap.md` L1335 — RenderGraphBuilder
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| (Phase 2) | GraphicsPipelineDesc, DescriptorSetLayout | Complete | Pipeline + descriptor types for pass setup |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/RenderGraphBuilder.h` | **public** | **H** | `RenderGraphBuilder` — declare passes, resources, read/write deps |
| `include/miki/rendergraph/RenderGraphTypes.h` | **public** | **H** | `RGHandle`, `RGPassHandle`, `RGResourceDesc`, `RGPassType`, `RGAccessFlags` |
| `src/miki/rendergraph/RenderGraphBuilder.cpp` | internal | L | Builder implementation |
| `tests/unit/test_render_graph_builder.cpp` | internal | L | Unit tests |

- **Error model**: `std::expected<T, ErrorCode>` for resource creation; builder methods are infallible (record declarations, validate at compile time)
- **Thread safety**: Builder is single-threaded (constructed, filled, then passed to compiler)
- **Invariants**: `AddGraphicsPass`/`AddComputePass` return valid `RGPassHandle`; `CreateTexture`/`CreateBuffer` return valid `RGHandle`; `Read`/`Write` register dependencies
- **ExecuteFn lifetime contract**: `SetupFn` and `ExecuteFn` are `std::function` stored by the builder. The builder takes **ownership** of these functors. **All references/pointers captured by the lambda must remain valid until `RenderGraphExecutor::Execute()` returns for the current frame.** The builder/compiler/executor do NOT manage the lifetime of objects captured by user lambdas. Callers must ensure captured data (e.g., material parameters, light arrays, camera matrices) outlives the frame's execute phase. This is the same contract as UE5 RDG and Filament FrameGraph. Phase 4+ may introduce a per-frame linear allocator to simplify capture lifetime management, but Phase 3a relies on caller discipline.

### Downstream Consumers

- `RenderGraphBuilder.h` (**public**, heat **H**):
  - T3a.1.2 (Compiler) — consumes built graph structure for topological sort + barrier insertion
  - T3a.2.1 (GBuffer) — calls `AddGraphicsPass()` to register GBuffer geometry pass
  - T3a.3.1 (Deferred) — calls `AddComputePass()` / `AddGraphicsPass()` for deferred resolve
  - T3a.4.1 (IBL) — calls `AddComputePass()` for cubemap conversion + specular pre-filter
  - T3a.5.1 (ToneMap) — calls `AddGraphicsPass()` for final blit
  - Phase 3b: VSM, TAA, GTAO, VRS all add passes via this API
  - Phase 4: transient aliasing reads resource lifetimes from builder
- `RenderGraphTypes.h` (**public**, heat **H**):
  - All render graph consumers use `RGHandle`, `RGPassHandle`, `RGResourceDesc`

### Upstream Contracts
- Phase 2: `Format` enum from `include/miki/rhi/Format.h`
- Phase 2: `TextureLayout`, `PipelineStage`, `AccessFlags` from `include/miki/rhi/RhiTypes.h`

### Technical Direction
- **Lambda-based pass setup/execute**: `AddGraphicsPass(name, [](PassBuilder& pb){ pb.Read(...); pb.Write(...); }, [](PassData&, RenderContext& ctx){ ctx.cmd.Draw(...); })` — Frostbite/UE5 FrameGraph pattern
- **Handle-based resources**: `RGHandle` is a lightweight index (32-bit) into builder's resource array. No raw texture/buffer handles exposed until execution.
- **Deferred validation**: builder does not validate; compiler detects cycles, unused resources, access conflicts
- **Backend-agnostic**: builder knows nothing about Vulkan/D3D12/GL/WebGPU — only abstract resource descs and access flags

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/RenderGraphBuilder.h` | **public** | **H** | Core builder API — 10+ consumers |
| Create | `include/miki/rendergraph/RenderGraphTypes.h` | **public** | **H** | Shared types for all RG modules |
| Create | `src/miki/rendergraph/RenderGraphBuilder.cpp` | internal | L | Implementation |
| Create | `src/miki/rendergraph/CMakeLists.txt` | internal | L | Build target `miki_rendergraph` |
| Create | `tests/unit/test_render_graph_builder.cpp` | internal | L | Unit tests |
| Modify | `src/CMakeLists.txt` | internal | L | Add rendergraph subdirectory |

## Steps

- [x] **Step 1**: Define RenderGraphTypes.h (public H)
      **Files**: `RenderGraphTypes.h` (**public** H)

      **Signatures** (`RenderGraphTypes.h`):

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `RGHandle` | `{ uint32_t index; }` | value type, default `{~0u}` = invalid |
      | `RGPassHandle` | `{ uint32_t index; }` | value type |
      | `RGPassType` | `enum class : uint8_t { Graphics, Compute, Transfer }` | — |
      | `RGAccessFlags` | `enum class : uint8_t { Read, Write, ReadWrite }` | — |
      | `RGTextureDesc` | `{ uint32_t width, height, depth; Format format; uint32_t mipLevels; uint32_t samples; }` | — |
      | `RGBufferDesc` | `{ uint64_t size; uint32_t usage; }` | — |
      | `RGResourceType` | `enum class : uint8_t { Texture, Buffer }` | — |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::rendergraph` |
      | `RGHandle::IsValid()` | `index != ~0u` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Define RenderGraphBuilder.h (public H)
      **Files**: `RenderGraphBuilder.h` (**public** H)

      **Signatures** (`RenderGraphBuilder.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `RenderGraphBuilder::RenderGraphBuilder` | `()` | default ctor |
      | `AddGraphicsPass` | `(std::string_view name, SetupFn setup, ExecuteFn execute) -> RGPassHandle` | `[[nodiscard]]` |
      | `AddComputePass` | `(std::string_view name, SetupFn setup, ExecuteFn execute) -> RGPassHandle` | `[[nodiscard]]` |
      | `CreateTexture` | `(std::string_view name, RGTextureDesc desc) -> RGHandle` | `[[nodiscard]]` |
      | `CreateBuffer` | `(std::string_view name, RGBufferDesc desc) -> RGHandle` | `[[nodiscard]]` |
      | `ImportTexture` | `(std::string_view name, TextureHandle ext) -> RGHandle` | `[[nodiscard]]` — for backbuffer/swapchain |
      | `ImportBuffer` | `(std::string_view name, BufferHandle ext) -> RGHandle` | `[[nodiscard]]` |
      | `GetPassCount` | `() const noexcept -> uint32_t` | `[[nodiscard]]` |
      | `GetResourceCount` | `() const noexcept -> uint32_t` | `[[nodiscard]]` |

      `SetupFn` = `std::function<void(PassBuilder&)>`
      `ExecuteFn` = `std::function<void(const PassData&, RenderContext&)>`

      **PassBuilder** (nested, used inside setup lambda):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `Read` | `(RGHandle, PipelineStage) -> RGHandle` | returns aliased handle for read |
      | `Write` | `(RGHandle, PipelineStage) -> RGHandle` | returns aliased handle for write |
      | `SetDepthStencil` | `(RGHandle, RGAccessFlags) -> void` | — |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 3**: Implement builder internals + CMake target
      **Files**: `RenderGraphBuilder.cpp` (internal L), `CMakeLists.txt` (internal L)
      Store passes and resources in `std::vector`. PassBuilder records read/write edges.
      Create `miki_rendergraph` STATIC library.
      **Acceptance**: compiles, builder can declare passes and resources
      `[verify: compile]`

- [x] **Step 4**: Unit tests
      **Files**: `test_render_graph_builder.cpp` (internal L)
      Cover: empty graph, single pass, multi-pass with dependencies, import external resource,
      pass count/resource count, invalid handle detection.
      **Acceptance**: all tests pass on both build paths
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(RenderGraphBuilder, EmptyGraph)` | Boundary | 0 passes, 0 resources | 3-4 |
| `TEST(RenderGraphBuilder, SingleGraphicsPass)` | Positive | Add 1 pass, verify count | 2-4 |
| `TEST(RenderGraphBuilder, SingleComputePass)` | Positive | Add 1 compute pass | 2-4 |
| `TEST(RenderGraphBuilder, MultiPassReadWrite)` | Positive | Pass A writes, Pass B reads | 2-4 |
| `TEST(RenderGraphBuilder, CreateTexture)` | Positive | Texture resource created | 1-4 |
| `TEST(RenderGraphBuilder, CreateBuffer)` | Positive | Buffer resource created | 1-4 |
| `TEST(RenderGraphBuilder, ImportTexture)` | Positive | External texture imported | 2-4 |
| `TEST(RenderGraphBuilder, PassBuilderReadWrite)` | Positive | Setup lambda read/write deps | 2-4 |
| `TEST(RenderGraphBuilder, RGHandleInvalid)` | Boundary | Default RGHandle is invalid | 1 |
| `TEST(RenderGraphBuilder, ManyPasses)` | Boundary | 100 passes, no crash | 2-4 |

## Design Decisions

- **`PassData&` non-const in ExecuteFn**: Spec says `const PassData&`, but T3a.1.3 (Executor) will extend `PassData` with resolved resource bindings that execute lambdas may mutate. Non-const avoids a breaking change.
- **`AddTransferPass` added**: Natural extension for `RGPassType::Transfer` — used by present/blit passes. Not in original spec but consistent with the type enum.
- **`SetSideEffect()` on PassBuilder**: Required by T3a.1.2 (Compiler) for dead-pass culling. Sentinel edge pattern used to avoid storing a raw pointer to `RGPass` (which would dangle on vector reallocation).
- **Public accessors (`GetPasses`, `GetResources`, `GetEdges`)**: Compiler needs full access to builder internals. Public getters chosen over `friend` for simplicity and testability.

## Implementation Notes

- Contract check: **PASS** (all 24 items verified, 1 intentional deviation on `const PassData&` documented above)
- 14 tests, 0 failures, debug-d3d12 build path verified
- `miki_rendergraph` STATIC library created, linked against `miki::rhi`
- Namespace: `miki::rendergraph`
