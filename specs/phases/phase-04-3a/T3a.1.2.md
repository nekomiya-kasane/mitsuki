# T3a.1.2 — RenderGraphCompiler: Kahn Sort, Barriers, Transient Aliasing

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 1 — Render Graph
**Roadmap Ref**: `roadmap.md` L1335 — RenderGraphCompiler
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: XH (4-8h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.1 | RenderGraphBuilder | Complete | `RenderGraphBuilder`, `RGHandle`, `RGPassHandle`, pass/resource arrays |
| T3a.1.1-fixup | RenderGraphBuilder & Types hardening | Complete | Variant `RGResource`, typed usage flags, generation handles, `Reset()`, extended `RGTextureDesc` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/RenderGraphCompiler.h` | **public** | **H** | `RenderGraphCompiler` — compile graph to execution order + barriers |
| `include/miki/rendergraph/CompiledGraph.h` | **public** | **H** | `CompiledGraph` — sorted pass list, barrier commands, transient aliasing info |
| `src/miki/rendergraph/RenderGraphCompiler.cpp` | internal | L | Compiler implementation |
| `tests/unit/test_render_graph_compiler.cpp` | internal | L | Unit tests |

- **Error model**: `Compile()` returns `std::expected<CompiledGraph, ErrorCode>` — error on cycle, unresolved resource
- **Thread safety**: Compiler is single-use (takes builder, produces compiled graph, discarded)
- **Invariants**: output pass order is topologically valid; every resource transition has a matching barrier; transient resources with non-overlapping lifetimes may alias the same memory

### Downstream Consumers

- `RenderGraphCompiler.h` (**public**, heat **H**):
  - T3a.1.3 (Executor) — consumes `CompiledGraph` to emit GPU commands
  - T3a.1.4 (Cache) — hashes graph structure to skip recompilation
  - Phase 4: reads transient aliasing info for memory budget
- `CompiledGraph.h` (**public**, heat **H**):
  - T3a.1.3, T3a.1.4, Phase 3b, Phase 4

### Upstream Contracts
- T3a.1.1: `RenderGraphBuilder` — pass array, resource array, read/write edges
- Phase 2: `PipelineStage`, `AccessFlags`, `TextureLayout` from `RhiTypes.h`

### Technical Direction
- **Kahn's algorithm**: O(V+E) topological sort. Detect cycles via remaining-in-degree check.
- **Barrier insertion**: for each pass, compute required transitions (layout, access, stage) from predecessor's output state to this pass's input state. Group barriers per-pass.
- **Transient aliasing**: compute resource lifetime [firstUse, lastUse]. Non-overlapping lifetimes can share the same physical allocation (Phase 4 `ResourceManager` handles actual aliasing; compiler just produces the lifetime data).
- **Dead pass culling (mandatory)**: reverse reachability BFS from passes marked `writes_to_external` (present target, readback). Passes not reachable from any external output are removed from the sorted list. Debug builds log culled pass names. Side-effect passes (e.g., GPU timestamp, debug markers) must be explicitly marked `side_effect = true` to survive culling. This matches UE5 RDG and Frostbite FrameGraph behavior — culling is not optional.
- **Reference**: Frostbite FrameGraph (GDC 2017), UE5 RDG

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/RenderGraphCompiler.h` | **public** | **H** | Compiler API |
| Create | `include/miki/rendergraph/CompiledGraph.h` | **public** | **H** | Compiled output structure |
| Create | `src/miki/rendergraph/RenderGraphCompiler.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_render_graph_compiler.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define CompiledGraph.h (public H)
      **Files**: `CompiledGraph.h` (**public** H)

      **Signatures**:

      | Symbol | Fields / Signature | Attrs |
      |--------|-------------------|-------|
      | `BarrierCommand` | `{ RGHandle resource; PipelineStage srcStage, dstStage; AccessFlags srcAccess, dstAccess; TextureLayout oldLayout, newLayout; }` | — |
      | `CompiledPass` | `{ RGPassHandle handle; std::string_view name; RGPassType type; std::vector<BarrierCommand> barriers; ExecuteFn execute; }` | — |
      | `ResourceLifetime` | `{ RGHandle handle; uint32_t firstPass, lastPass; }` | for transient aliasing |
      | `CompiledGraph` | `{ std::vector<CompiledPass> passes; std::vector<ResourceLifetime> lifetimes; }` | — |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Define RenderGraphCompiler.h (public H)
      **Files**: `RenderGraphCompiler.h` (**public** H)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `RenderGraphCompiler::Compile` | `(RenderGraphBuilder&& builder) -> expected<CompiledGraph, ErrorCode>` | `[[nodiscard]]` static |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 3**: Implement Kahn sort + barrier insertion + lifetime analysis
      **Files**: `RenderGraphCompiler.cpp` (internal L)
      1. Build adjacency list from read/write edges.
      2. Kahn sort — detect cycles (return error if cycle found).
      3. For each pass in sorted order, compute required barriers from predecessor state.
      4. Compute resource lifetimes [firstPass, lastPass].
      5. Dead pass culling: reverse BFS from external outputs, remove unreachable passes (log in debug).
      6. Validate side-effect passes survive culling.
      **Acceptance**: compiles, simple graph compiles correctly
      `[verify: compile]`

- [x] **Step 4**: Unit tests
      **Files**: `test_render_graph_compiler.cpp` (internal L)
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(RGCompiler, LinearChain)` | Positive | A→B→C sorted correctly | 3-4 |
| `TEST(RGCompiler, DiamondGraph)` | Positive | A→{B,C}→D sorted, both valid orderings accepted | 3-4 |
| `TEST(RGCompiler, CycleDetected)` | Error | A→B→A returns error | 3-4 |
| `TEST(RGCompiler, UnusedPass_Culled)` | Positive | Pass writing never-read resource is culled from compiled output | 3-4 |
| `TEST(RGCompiler, SideEffectPass_Retained)` | Positive | Pass marked `side_effect=true` survives culling even with no readers | 3-4 |
| `TEST(RGCompiler, ExternalOutput_NotCulled)` | Positive | Pass writing to external (present) output is never culled | 3-4 |
| `TEST(RGCompiler, BarrierInserted)` | Positive | Read-after-write produces correct barrier | 3-4 |
| `TEST(RGCompiler, WriteAfterRead_Barrier)` | Positive | WAR barrier has correct stages | 3-4 |
| `TEST(RGCompiler, ResourceLifetime)` | Positive | firstPass/lastPass correct for each resource | 3-4 |
| `TEST(RGCompiler, TransientAliasOpportunity)` | Positive | Non-overlapping lifetimes detected | 3-4 |
| `TEST(RGCompiler, ImportedResourceNoLifetime)` | Boundary | Imported (external) resources have no transient aliasing | 3-4 |
| `TEST(RGCompiler, EmptyGraph)` | Boundary | Empty graph compiles to empty CompiledGraph | 3-4 |
| `TEST(RGCompiler, SinglePassNoBarrier)` | Boundary | Single pass with no deps = no barriers | 3-4 |
| `TEST(RGCompiler, ManyPassesPerformance)` | Boundary | 100 passes compile in < 10ms | 3-4 |

## Design Decisions

1. **Declaration-order streaming dependency model**: Dependencies are built by processing resource accesses in pass declaration order. For each resource, a streaming scan maintains `lastWriter` and `accumulatedReaders`. RAW edges go from lastWriter→reader, WAR edges from accumulatedReaders→newWriter, WAW edges between consecutive writers. This guarantees resource-derived edges always form a DAG (edges point from lower to higher declaration index), matching the Frostbite FrameGraph model.

2. **Explicit dependency edges via `AddDependency(from, to)`**: Since the streaming model cannot produce cycles from resource edges alone, `AddDependency` was added to `RenderGraphBuilder` to support explicit ordering constraints (debug markers, GPU timestamps) and to enable cycle detection testing. These edges CAN create back-edges and thus cycles.

3. **`CompiledGraph.resources` field**: Added beyond the spec's minimum (`passes` + `lifetimes`) to carry the full resource array forward to the executor, avoiding a second lookup. Resources are moved (not copied) from the builder via `MoveResources() &&`.

4. **Barrier inference from `RGPassType` + `RGAccessFlags`**: Layout and access flags are inferred from pass type and edge access flags rather than requiring explicit specification. Compute Write → `ShaderWrite` + `General` layout; Graphics Write → `ColorAttachmentWrite` + `ColorAttachment` layout.

5. **`CompiledPass.name` owns a `std::string`** (not `string_view`): The compiled graph outlives the builder, so names must be owned copies.

6. **Per-pass per-resource edge merging**: Multiple edges from the same pass to the same resource (e.g., separate `Read` + `Write` calls) are merged into a single `MergedEdge` before barrier insertion. This prevents duplicate barriers.

7. **RAR (read-after-read) skips barrier**: `needBarrier` uses `HasWriteBit()` on both src and dst access flags. If neither side has a write bit, no barrier is emitted. Layout transitions still trigger barriers regardless.

8. **Generation validation**: All edge handles and explicit dependency handles are validated against the builder's current generation. Stale handles (from before `Reset()`) produce `UnresolvedResource` error.

## Implementation Notes

- Kahn sort operates only on live (non-culled) passes. Dead pass culling runs first via reverse BFS from external outputs and side-effect passes.
- Barrier insertion tracks per-resource "last state" (stage, access, layout) across the sorted pass order and emits a `BarrierCommand` only when a data hazard exists (at least one write involved) or a texture layout transition is needed.
- Resource lifetimes are computed as [firstPass, lastPass] indices into the sorted pass array. Only transient resources get lifetime entries; imported resources are excluded.
- Performance: 100-pass graph compiles in <1ms (spec requires <10ms).
- Both build paths (debug-d3d12, debug-vulkan) verified: 0 errors, 19/19 tests pass, 0 regressions on RenderGraphBuilder tests (26/26).

### T3a.1.2-fixup (post-review)

| Fix | Severity | Description |
|-----|----------|-------------|
| `string_view` → `std::string` | P0 | `CompiledPass.name` was dangling after builder destruction |
| `needBarrier` RAR | P0 | RAR incorrectly triggered barrier; now uses `HasWriteBit()` |
| `InferAccessFlags` Compute | P0 | Compute Write returned `ColorAttachmentWrite`; now returns `ShaderWrite` |
| `InferLayout` Compute | P0 | Compute Write returned `ColorAttachment`; now returns `General` |
| Generation validation | P1 | Edge/dependency handles now checked against builder generation |
| Output handle generation | P1 | `CompiledPass.handle` and `ResourceLifetime.handle` use actual generation |
| Edge merge | P1 | Per-pass per-resource edge merging prevents duplicate barriers |
| `MoveResources()` | P2 | Resources moved from builder instead of deep-copied |
| Comment trim | P2 | 40-line dependency analysis comment reduced to 4-line summary |
| `accReaders` reuse | P2 | Vector reused across resource loop iterations |
| 5 new regression tests | — | RAR, ComputeShaderWrite, StaleGeneration, NameLifetime, EdgeMerge |
