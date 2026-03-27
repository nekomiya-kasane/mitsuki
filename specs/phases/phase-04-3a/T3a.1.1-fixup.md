# T3a.1.1-fixup — RenderGraphBuilder & Types Hardening

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 1 — Render Graph
**Roadmap Ref**: Review findings from T3a.1.1
**Status**: Complete
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Rationale

T3a.1.1 (RenderGraphBuilder) is marked Complete, but architectural review identified 7 gaps that must be fixed **before** T3a.1.2 (Compiler) begins, because the compiler directly consumes builder data structures. Building the compiler on a flawed foundation violates the Phase Gate principle.

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.1 | RenderGraphBuilder | Complete | All files modified by this fixup |

## Blocked By This Fixup

| Task ID | Name | Why It Needs This |
|---------|------|-------------------|
| T3a.1.2 | RenderGraphCompiler | Consumes `RGResource`, `RGEdge`, `RGTextureDesc`, `RGBufferDesc`; barrier insertion needs `PipelineStage` OR mask and usage flags |

## Files

| Action | Path | Notes |
|--------|------|-------|
| Modify | `include/miki/rendergraph/RenderGraphTypes.h` | Handle generation, TextureDesc fields, BufferUsageFlags |
| Modify | `include/miki/rendergraph/RenderGraphBuilder.h` | RGResource variant, Reset(), sentinel removal |
| Modify | `src/miki/rendergraph/RenderGraphBuilder.cpp` | Implementation updates |
| Modify | `tests/unit/test_render_graph_builder.cpp` | Error path tests, regression tests for fixes |

## Steps

- [x] **Step 1**: Fix `SetSideEffect` sentinel mechanism (severity: HIGH)

      **Problem**: `SetSideEffect` uses a sentinel edge with swap-with-back deletion inside range-for. Fragile, reorders edges.

      **Fix**: Add `sideEffectRequested_` bool to `PassBuilder`. After setup lambda returns, `AddPass` reads the flag directly. Remove sentinel edge pattern entirely.

      **Requires**: Add `bool sideEffectRequested_ = false;` to `PassBuilder`. Pass a `bool&` or make `AddPass` read it after `iSetup(pb)` returns.

      **Acceptance**: `SideEffectPass` test still passes. No sentinel edges in `edges_` vector.
      `[verify: test]`

- [x] **Step 2**: Introduce `std::variant` for `RGResource` descriptors (severity: HIGH)

      **Problem**: `RGResource` has flat `textureDesc` + `bufferDesc` + `externalTexture` + `externalBuffer` — wastes memory, no type safety.

      **Fix**: Replace with:
      ```cpp
      using RGResourceData = std::variant<
          RGTransientTexture,   // { RGTextureDesc desc; }
          RGTransientBuffer,    // { RGBufferDesc desc; }
          RGImportedTexture,    // { rhi::TextureHandle handle; }
          RGImportedBuffer      // { rhi::BufferHandle handle; }
      >;
      struct RGResource {
          std::string      name;
          RGResourceType   type;
          RGResourceData   data;
      };
      ```

      **Acceptance**: All existing tests pass. Compiler (T3a.1.2) can `std::get<>` safely.
      `[verify: test]`

- [x] **Step 3**: Extend `RGTextureDesc` with missing fields (severity: MEDIUM)

      **Problem**: Missing `usageFlags`, `arrayLayers`, `dimension`. Compiler needs usage to determine layout transitions; GBuffer (T3a.2.1) needs these fields.

      **Fix**: Add:
      ```cpp
      enum class RGTextureUsage : uint32_t {
          Sampled         = 1 << 0,
          Storage         = 1 << 1,
          ColorAttachment = 1 << 2,
          DepthStencil    = 1 << 3,
          TransferSrc     = 1 << 4,
          TransferDst     = 1 << 5,
      };
      enum class RGTextureDimension : uint8_t { Tex2D, Tex3D, TexCube };

      struct RGTextureDesc {
          uint32_t           width       = 1;
          uint32_t           height      = 1;
          uint32_t           depth       = 1;
          rhi::Format        format      = rhi::Format::RGBA8_UNORM;
          uint32_t           mipLevels   = 1;
          uint32_t           samples     = 1;
          uint32_t           arrayLayers = 1;
          RGTextureDimension dimension   = RGTextureDimension::Tex2D;
          RGTextureUsage     usage       = RGTextureUsage::Sampled;
      };
      ```

      **Acceptance**: Existing tests still pass (new fields have defaults).
      `[verify: test]`

- [x] **Step 4**: Replace `RGBufferDesc.usage` with typed enum (severity: MEDIUM)

      **Problem**: `uint32_t usage` has no type safety.

      **Fix**:
      ```cpp
      enum class RGBufferUsage : uint32_t {
          Uniform     = 1 << 0,
          Storage     = 1 << 1,
          Index       = 1 << 2,
          Vertex      = 1 << 3,
          Indirect    = 1 << 4,
          TransferSrc = 1 << 5,
          TransferDst = 1 << 6,
      };
      struct RGBufferDesc {
          uint64_t       size  = 0;
          RGBufferUsage  usage = RGBufferUsage::Storage;
      };
      ```

      **Acceptance**: Existing buffer tests updated for new type.
      `[verify: test]`

- [x] **Step 5**: Support `PipelineStage` OR mask in `PassBuilder::Read/Write` (severity: MEDIUM)

      **Problem**: `RGEdge.stage` stores a single `PipelineStage` value, but the enum is a bitmask. Multi-stage reads are unrepresentable.

      **Fix**: Ensure `PipelineStage` has `operator|` and `operator&` (may already exist or need adding). Document that `RGEdge.stage` can hold OR'd values. No structural change needed if the underlying type is already `uint32_t`.

      **Acceptance**: Add test: pass reads a resource at `FragmentShader | ComputeShader`.
      `[verify: test]`

- [x] **Step 6**: Add `RGHandle` / `RGPassHandle` generation field (severity: MEDIUM)

      **Problem**: Raw index handles become stale across builder reset/rebuild without detection.

      **Fix**:
      ```cpp
      struct RGHandle {
          uint32_t index = ~0u;
          uint16_t generation = 0;
          // ...
      };
      ```
      Builder gets `uint16_t generation_ = 0;` incremented on `Reset()`. All `Create*`/`Import*`/`AddPass` embed current generation. Validation in compiler compares handle generation vs builder generation.

      **Acceptance**: Test: create handle, reset builder, old handle detected as stale.
      `[verify: test]`

- [x] **Step 7**: Add `RenderGraphBuilder::Reset()` (severity: LOW)

      **Problem**: No way to reuse builder across frames without reallocating.

      **Fix**: `Reset()` clears all vectors, bumps `generation_`. Preserves vector capacity.

      **Acceptance**: Test: build graph, reset, build new graph, verify old handles invalid.
      `[verify: test]`

- [x] **Step 8**: Add error path tests (severity: LOW)

      **Tests to add**:

      | Test Name | Category | Validates |
      |-----------|----------|-----------|
      | `TEST(RenderGraphBuilder, ReadInvalidHandle)` | Error | Read with default RGHandle records edge but compiler can catch it |
      | `TEST(RenderGraphBuilder, DuplicateReadSamePass)` | Boundary | Same pass reads same resource twice — produces 2 edges (valid, compiler deduplicates) |
      | `TEST(RenderGraphBuilder, NullSetupLambda)` | Boundary | nullptr setup is safe (no edges) |
      | `TEST(RenderGraphBuilder, NullExecuteLambda)` | Boundary | nullptr execute stored without crash |
      | `TEST(RenderGraphBuilder, ResetAndRebuild)` | Positive | Reset clears state, generation bumps, old handles stale |

      **Acceptance**: All new + existing tests pass.
      `[verify: test]`

## Tests (New)

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(RenderGraphBuilder, SideEffectNoSentinel)` | Regression | SetSideEffect uses flag, no sentinel edge | 1, 8 |
| `TEST(RenderGraphBuilder, ResourceVariantTexture)` | Positive | RGResource data is RGTransientTexture variant | 2, 8 |
| `TEST(RenderGraphBuilder, ResourceVariantImported)` | Positive | Imported resource uses correct variant | 2, 8 |
| `TEST(RenderGraphBuilder, TextureDescExtendedFields)` | Positive | arrayLayers, dimension, usage stored | 3, 8 |
| `TEST(RenderGraphBuilder, BufferUsageTyped)` | Positive | RGBufferUsage enum stored correctly | 4, 8 |
| `TEST(RenderGraphBuilder, MultiStagePipelineRead)` | Positive | OR'd PipelineStage stored in edge | 5, 8 |
| `TEST(RenderGraphBuilder, HandleGeneration)` | Positive | Handle embeds builder generation | 6, 8 |
| `TEST(RenderGraphBuilder, ResetAndRebuild)` | Positive | Reset clears + bumps generation | 7, 8 |
| `TEST(RenderGraphBuilder, ReadInvalidHandle)` | Error | Invalid handle edge recorded | 8 |
| `TEST(RenderGraphBuilder, DuplicateReadSamePass)` | Boundary | Duplicate edges are valid | 8 |
| `TEST(RenderGraphBuilder, NullSetupLambda)` | Boundary | nullptr setup safe | 8 |
| `TEST(RenderGraphBuilder, NullExecuteLambda)` | Boundary | nullptr execute safe | 8 |

## Design Decisions

1. **Sentinel removal → bool& out-param**: `PassBuilder` constructor takes `bool& oSideEffect` referencing `RGPass::sideEffect` directly. No sentinel edges needed. Simpler, zero-overhead, no edge vector reordering.
2. **Variant vs flat struct**: `RGResourceData = std::variant<RGTransientTexture, RGTransientBuffer, RGImportedTexture, RGImportedBuffer>`. Type-safe, no wasted memory, `std::get<>` enforces correct access in compiler.
3. **Generation on handles, not in pool**: `RGHandle.generation` and `RGPassHandle.generation` are lightweight (uint16_t). Builder bumps `generation_` on `Reset()`. Compiler can detect stale handles by comparing against builder generation.
4. **PipelineStage bitwise ops in RhiTypes.h**: Added `operator|`, `operator&`, `operator|=` for `PipelineStage`. Same pattern applied to `RGTextureUsage` and `RGBufferUsage` in `RenderGraphTypes.h`.

## Implementation Notes

- **Files modified**: `RhiTypes.h` (PipelineStage ops), `RenderGraphTypes.h` (usage enums, dimension, generation), `RenderGraphBuilder.h` (variant, PassBuilder bool&, Reset, GetGeneration), `RenderGraphBuilder.cpp` (all implementations), `test_render_graph_builder.cpp` (12 new tests + 6 updated existing tests).
- **Test count**: 14 original + 12 new = 26 total (was 14).
- **Build verification**: debug-d3d12 + debug-vulkan both compile with 0 errors. 26/26 render graph tests pass on both paths. No regressions in full ctest suite.
