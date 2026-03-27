# T3a.7.5 — DrawListBuilder Sort Key

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 7 — 5-Backend Sync + Demo + Phase 2 Debt Cleanup
**Roadmap Ref**: `roadmap.md` — Phase 2 rendering debt cleanup
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.7.2 | ForwardPass → RG-native pass | Complete | `ForwardPass::Execute()` — contains duplicated sort logic to extract |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/render/DrawListBuilder.h` | **public** | **M** | `BuildSortKey()`, `SortDrawList()` — shared draw sorting utility |
| `src/miki/render/ForwardPass.cpp` | internal | L | Refactored to use `SortDrawList()` |
| `src/miki/render/GBufferPass.cpp` | internal | L | Refactored to use `SortDrawList()` |
| `tests/unit/test_draw_list_builder.cpp` | internal | L | Unit tests |

**Sort key layout (64-bit)**:

| Bits | Field | Width | Purpose |
|------|-------|-------|---------|
| 63-56 | pipeline | 8 | Group by pipeline state (minimize PSO switches) |
| 55-40 | material | 16 | Group by material (minimize descriptor set switches) |
| 39-24 | mesh | 16 | Group by mesh (minimize VB/IB rebinds) |
| 23-0 | depth | 24 | Front-to-back for opaque (minimize overdraw) |

- **Error model**: no errors — pure CPU utility, infallible
- **Thread safety**: stateless free functions, inherently thread-safe
- **Key insight**: sort by `sortKey` is a single `uint64_t` comparison, much faster than multi-field comparisons. Pipeline sort is MSB = highest priority.

### Downstream Consumers

- `ForwardPass::Execute()` — replaces inline sort-by-material
- `GBufferPass::Execute()` — replaces inline sort-by-material
- Phase 3b+ transparent pass — will use depth bits for back-to-front
- Phase 7a edge pass — will use same sort infrastructure

### Upstream Contracts
- `DrawCall` from `ForwardPass.h` — already has `sortKey:uint64_t`, `material:MaterialId`, `pipeline:PipelineHandle`

### Technical Direction
- **Header-only**: pure constexpr/inline functions, no .cpp needed
- **`std::ranges::sort`** over `std::sort` for C++23 compliance
- **`SortDrawList` returns sorted index array** — non-destructive, callers iterate by index (existing pattern)
- **`BuildSortKey` is a free function** — caller fills `DrawCall::sortKey` before sort. Depth quantization from camera distance (caller provides viewProj).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/render/DrawListBuilder.h` | **public** | **M** | BuildSortKey + SortDrawList |
| Modify | `src/miki/render/ForwardPass.cpp` | internal | L | Use SortDrawList |
| Modify | `src/miki/render/GBufferPass.cpp` | internal | L | Use SortDrawList |
| Create | `tests/unit/test_draw_list_builder.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Create DrawListBuilder.h — BuildSortKey + SortDrawList
      **Files**: `include/miki/render/DrawListBuilder.h` (**public** M)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Refactor ForwardPass::Execute — use SortDrawList
      **Files**: `src/miki/render/ForwardPass.cpp` (internal L)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 3**: Refactor GBufferPass::Execute — use SortDrawList
      **Files**: `src/miki/render/GBufferPass.cpp` (internal L)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 4**: Write unit tests
      **Files**: `tests/unit/test_draw_list_builder.cpp`, `tests/unit/CMakeLists.txt`
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(DrawListBuilder, SortKeyPipelineMSB)` | Positive | Pipeline bits are highest priority | 4 |
| `TEST(DrawListBuilder, SortKeyMaterialGrouping)` | Positive | Same material groups together | 4 |
| `TEST(DrawListBuilder, SortKeyDepthOrdering)` | Positive | Front-to-back within same material | 4 |
| `TEST(DrawListBuilder, SortDrawListReturnsCorrectOrder)` | Positive | Sorted index array matches expected | 4 |
| `TEST(DrawListBuilder, SortDrawListEmptyInput)` | Boundary | Empty span returns empty vector | 4 |
| `TEST(DrawListBuilder, SortDrawListSingleElement)` | Boundary | Single element returns [0] | 4 |
| `TEST(DrawListBuilder, SortKeyStability)` | Positive | Equal keys preserve original order | 4 |
| `TEST(DrawListBuilder, BuildSortKeyDepthQuantization)` | Positive | Depth maps to 24-bit range correctly | 4 |

## Design Decisions

- **`SortDrawList(span<const uint64_t>)` over `span<const DrawCall>`**: avoids circular header dependency (DrawCall defined in ForwardPass.h which would include DrawListBuilder.h). Sort keys are pre-computed separately, then passed as uint64_t array.
- **`BuildSortKeyByMaterial(uint32_t)` over `MaterialId`**: uses raw uint32_t to avoid MaterialRegistry.h dependency. DrawListBuilder.h has zero project-internal includes.
- **`std::stable_sort` over `std::ranges::stable_sort`**: COCA toolchain lacks full `<ranges>` support. `std::stable_sort` provides same stability guarantee.
- **24-bit depth field**: 16M discrete depth values sufficient for front-to-back ordering. Phase 3b+ will quantize camera-space Z into this field.

## Implementation Notes

- Contract check: PASS — `BuildSortKey`/`SortDrawList`/Extract functions match spec. 64-bit layout verified by 15 unit tests.
- 639/639 tests pass (excluding 4 pre-existing GL/WebGPU failures), zero regression.
- 15/15 DrawListBuilder-specific tests pass.
- Removed duplicated sort logic from both ForwardPass::Execute and GBufferPass::Execute — DRY violation eliminated.
