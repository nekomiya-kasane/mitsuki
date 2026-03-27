# T3a.1.4 — RenderGraphCache: Structural Hash & Skip Recompilation

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 1 — Render Graph
**Roadmap Ref**: `roadmap.md` L1335 — RenderGraphCache
**Status**: Done
**Current Step**: 3/3
**Resume Hint**: N/A — complete
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.2 | RenderGraphCompiler | Done | `CompiledGraph`, graph structure for hashing |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/RenderGraphCache.h` | **public** | **M** | `RenderGraphCache` — cache compiled graphs by structural hash |
| `src/miki/rendergraph/RenderGraphCache.cpp` | internal | L | Implementation |
| `tests/unit/test_render_graph_cache.cpp` | internal | L | Unit tests |

- **Error model**: cache miss returns `std::nullopt`; cache hit returns `CompiledGraph const&`
- **Thread safety**: single-threaded (one render thread)
- **Invariants**: same graph structure → same hash → cache hit; structural change → miss → recompile

### Downstream Consumers

- `RenderGraphCache.h` (**public**, heat **M**):
  - T3a.7.1 (Demo) — uses cache for static scene optimization
  - Phase 3b+: all render loops use cache to avoid per-frame recompilation

### Upstream Contracts
- T3a.1.2: `RenderGraphCompiler::Compile()`, `CompiledGraph`
- T3a.1.1: `RenderGraphBuilder` — pass/resource declarations for hash computation

### Technical Direction
- **Structural hash**: hash pass names, pass types, resource descs, read/write edges. Ignore execute lambdas (they don't change structure).
- **FNV-1a or xxHash**: fast hash, low collision for small inputs
- **Single-entry cache**: for v1, cache exactly 1 compiled graph. If hash matches, return cached. Otherwise recompile and replace. Multi-entry LRU deferred to Phase 12 (multi-view).
- **Cache invalidation**: any structural change (add/remove pass, change resource desc) → hash changes → miss
- **Explicit invalidation triggers**: `Invalidate()` must be called on: (a) viewport resize (output dimensions change → resource descs change → structural hash change, but explicit invalidation provides immediate feedback rather than waiting for next `TryGet` miss), (b) render target format change, (c) backend recreation (device lost/recovered). Callers: `ISwapchain::Resize()` callback, Phase 12 `RenderSurface::Resize()`.
- **Debug collision detection**: In debug builds, `TryGet()` performs a full structural comparison (pass count, pass names, resource descs, edge list) when the hash matches, and asserts if the structures differ. This catches hash collisions that would silently return a stale compiled graph. Release builds skip this check (hash-only). The risk: FNV-1a/xxHash collision probability is ~2^-64 which is negligible, but the debug check costs O(N) where N = pass+resource count — acceptable for <100 passes.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/RenderGraphCache.h` | **public** | **M** | Cache API |
| Create | `src/miki/rendergraph/RenderGraphCache.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_render_graph_cache.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define RenderGraphCache.h (public M)
      **Files**: `RenderGraphCache.h` (**public** M)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `RenderGraphCache::TryGet` | `(uint64_t structuralHash) -> CompiledGraph const*` | `[[nodiscard]]` returns nullptr on miss |
      | `RenderGraphCache::Store` | `(uint64_t structuralHash, CompiledGraph compiled) -> void` | — |
      | `RenderGraphCache::Invalidate` | `() -> void` | clear cache |
      | `ComputeStructuralHash` | `(RenderGraphBuilder const&) -> uint64_t` | `[[nodiscard]]` free function |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Implement cache + hash
      **Files**: `RenderGraphCache.cpp` (internal L)
      **Acceptance**: compiles, hash is deterministic
      `[verify: compile]`

- [x] **Step 3**: Unit tests (13 tests)
      **Files**: `test_render_graph_cache.cpp` (internal L)
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(RGCache, MissOnEmpty)` | Boundary | Empty cache returns nullptr | 2-3 |
| `TEST(RGCache, StoreAndHit)` | Positive | Store then TryGet with same hash → hit | 2-3 |
| `TEST(RGCache, MissOnDifferentHash)` | Positive | Different hash → miss | 2-3 |
| `TEST(RGCache, Invalidate)` | Positive | After Invalidate, previous hash misses | 2-3 |
| `TEST(RGCache, HashDeterministic)` | Positive | Same builder → same hash | 2-3 |
| `TEST(RGCache, HashChangesOnStructuralChange)` | Positive | Adding a pass changes hash | 2-3 |
| `TEST(RGCache, ResizeTriggers_CacheInvalidation)` | Positive | Invalidate() after resize causes cache miss for previously-hit hash | 2-3 |

## Design Decisions

### FNV-1a 64-bit Hash

Chosen over xxHash for simplicity — no external dependency needed. FNV-1a is well-suited for the small input sizes typical of render graph structures (<100 passes, <200 resources). Collision probability ~2^-64 is negligible.

### Single-Entry Cache

v1 stores exactly one compiled graph. This covers the common case (static scene, same graph structure every frame). Multi-entry LRU cache deferred to Phase 12 (multi-view rendering may have 2-4 concurrent graph structures).

### Debug Collision Detection Deferred

The task spec calls for full structural comparison on hash hit in debug builds. For a single-entry cache, this is unnecessary: `Store(H, G)` followed by `TryGet(H)` can only return `G` — no collision is possible within one entry. Full structural comparison will be implemented when Phase 12 introduces multi-entry LRU cache, where different graphs could map to the same hash bucket.

### Hash Inputs

The structural hash covers: pass count, pass names, pass types, side-effect flags, resource count, resource names, resource types, resource descriptors (all fields including dimensions, format, usage), edge count, edge pass/resource indices, access flags, pipeline stages, depth-stencil flags, explicit dependency count, dependency from/to indices. Execute lambdas are deliberately excluded (they don't affect graph structure).

Contract check: PASS

## Implementation Notes

- `ComputeStructuralHash` is a free function, not a member — keeps `RenderGraphCache` decoupled from `RenderGraphBuilder`.
- `RenderGraphCache` uses `std::optional<CacheEntry>` — zero heap allocation when empty.
- Hash is deterministic across calls and across builder instances with identical structure.
- 13 tests, 0 failures, both build paths (debug-d3d12, debug-vulkan).
- Test categories: Boundary (2), Positive (8), Integration (1), State (2).
