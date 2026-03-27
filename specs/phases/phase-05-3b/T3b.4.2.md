# T3b.4.2 — VSM Physical Page Pool + LRU Allocator + Dirty Page Tracking

**Phase**: 05-3b (Shadows, Post-Processing & Visual Regression)
**Component**: 4 — VSM Shadows (Page System)
**Roadmap Ref**: `roadmap.md` Phase 3b — VSM; `rendering-pipeline-architecture.md` S8 VSM page lifecycle
**Status**: Complete
**Current Step**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.4.1 | VSM Page Table | Not Started | `VsmPageTable`, `VsmTypes`, page request buffer |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `VsmPagePool.h` | **shared** | **M** | `VsmPagePool` -- physical page allocation, LRU eviction, dirty tracking |
| `VsmPagePool.cpp` | internal | L | LRU implementation, physical texture array management |

- **Physical page pool**: array texture D32_SFLOAT, N physical pages of 128x128 texels. N = min(1024, VRAM budget / pageSize). Default ~512 pages = 512 * 128 * 128 * 4B = 32MB.
- **LRU eviction**: each page tracks last-sampled frame. When pool full, evict least-recently-sampled page.
- **Dirty tracking**: pages marked dirty on scene geometry change (transform/add/remove). Only dirty pages re-rendered.
- **Allocation**: `Allocate(VsmPageId) -> optional<uint16_t physicalIndex>`. Returns nullopt only on catastrophic OOM.
- **Error model**: allocation failure returns nullopt; pool Create returns `Result<VsmPagePool>`

### Downstream Consumers

- `VsmPagePool.h` (**shared** M):
  - T3b.5.1: reads dirty page list to determine which pages to render
  - T3b.5.2: reads physical page texture for shadow sampling

### Upstream Contracts

- T3b.4.1: `VsmPageTable::GetRequestBuffer()` provides page request data
- Phase 2: `IDevice::CreateTexture` for physical page array texture

### Technical Direction

- **LRU data structure**: `std::list<VsmPageId>` + `std::unordered_map<VsmPageId, list::iterator>` for O(1) access/evict.
- **Frame-based aging**: each `Allocate` call updates page's last-used frame. `GetEvictionCandidate` returns page with oldest frame.
- **Dirty invalidation**: `InvalidateRegion(AABB worldBounds)` marks all pages overlapping bounds as dirty.
- **Statistics**: track hit rate, eviction count, dirty page count per frame for ImGui debug panel.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/VsmPagePool.h` | **shared** | **M** | Pool interface |
| Create | `src/miki/rendergraph/VsmPagePool.cpp` | internal | L | LRU + allocation |
| Modify | `src/miki/rendergraph/CMakeLists.txt` | internal | L | Add source |
| Create | `tests/unit/test_vsm_page_pool.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define `VsmPagePool` interface
- [x] **Step 2**: Implement LRU allocation + eviction
- [x] **Step 3**: Implement dirty page tracking + invalidation
- [x] **Step 4**: Create physical page array texture
- [x] **Step 5**: Tests

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(VsmPagePool, CreateReturnsValid)` | Positive | factory success | 1 |
| `TEST(VsmPagePool, AllocateReturnsPhysicalIndex)` | Positive | valid index returned | 2 |
| `TEST(VsmPagePool, AllocateAllPages)` | Positive | pool fills without error | 2 |
| `TEST(VsmPagePool, EvictsLRUWhenFull)` | Positive | oldest page evicted when full | 2 |
| `TEST(VsmPagePool, DirtyPageTracking)` | Positive | marked dirty pages appear in dirty list | 3 |
| `TEST(VsmPagePool, InvalidateRegionMarksDirty)` | Positive | AABB overlap marks correct pages | 3 |
| `TEST(VsmPagePool, CleanPageNotInDirtyList)` | Boundary | rendered page removed from dirty list | 3 |
| `TEST(VsmPagePool, LRUOrderCorrect)` | Positive | access updates LRU position | 2 |
| `TEST(VsmPagePool, StatisticsTracked)` | Positive | hit/eviction/dirty counts accurate | 2-3 |
| `TEST(VsmPagePool, EndToEnd_AllocEvictReallocate)` | Integration | full lifecycle: alloc all, evict, re-alloc | 2-3 |
| `TEST(VsmPagePool, ZeroPoolSize_Error)` | Error | pool size 0 returns error on Create | 1 |
| `TEST(VsmPagePool, PoolSize1_EvictImmediately)` | Boundary | single-page pool evicts on second allocate | 2 |
| `TEST(VsmPagePool, MoveSemantics)` | State | move transfers pool, source empty | 1 |
| `TEST(VsmPagePool, DoubleFree_Safe)` | Boundary | freeing same page twice is no-op, no crash | 2 |
