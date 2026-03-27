# T3b.4.1 — VSM Page Table Data Structures + Page Request Compute

**Phase**: 05-3b (Shadows, Post-Processing & Visual Regression)
**Component**: 4 — VSM Shadows (Page System)
**Roadmap Ref**: `roadmap.md` Phase 3b — VSM; `rendering-pipeline-architecture.md` S8 VSM page lifecycle
**Status**: Complete
**Current Step**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.1.2 | Tech Debt B (FrameResources) | Not Started | `FrameResources`, per-frame-in-flight pools |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `VsmPageTable.h` | **public** | **H** | `VsmPageTable` -- 16K virtual grid, page request compute, virtual-to-physical mapping |
| `VsmTypes.h` | **public** | **H** | `VsmConfig`, `VsmPageId`, `VsmPhysicalPage`, page constants |
| `VsmPageTable.cpp` | internal | L | CPU-side page table management |
| `vsm_page_request.slang` | internal | L | Compute shader: project visible geometry, mark needed pages |

- **Virtual grid**: 16384 / 128 = 128x128 virtual pages. Page table stored as R32_UINT 128x128 texture.
- **Page request compute**: per visible instance, project bounding sphere to shadow space, determine required virtual pages, mark in request buffer via atomicOr.
- **Page table format**: uint32 per page: bits [0:15] = physical page index, bit 16 = valid, bit 17 = dirty.
- **Error model**: `Result<VsmPageTable>` from `Create()`

### Downstream Consumers

- `VsmPageTable.h` (**public** H):
  - T3b.4.2: page pool reads request buffer to allocate/evict pages
  - T3b.5.1: shadow render reads dirty page list from page table
  - T3b.5.2: deferred resolve samples page table for shadow lookup
  - Phase 6a: virtual texture paging may reuse page table structure
  - Phase 7b: SDF trim texture virtual paging reuses page logic

### Upstream Contracts

- Phase 3a: `RenderGraphBuilder::AddComputePass()`, `ICommandBuffer::Dispatch()`
- Phase 2: `IDevice::CreateTexture`, `IDevice::CreateBuffer`

### Technical Direction

- **16K virtual shadow map**: 16384x16384 texels. 128x128 pages of 128x128 texels each.
- **Clipmap approach**: directional light uses concentric clipmap levels (4 levels, each 32x32 pages).
- **Page request**: GPU compute pass projects visible instance bounding spheres to light space, marks required pages.
- **Incremental**: only newly-requested pages trigger allocation.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/VsmTypes.h` | **public** | **H** | VSM constants + types |
| Create | `include/miki/rendergraph/VsmPageTable.h` | **public** | **H** | Page table interface |
| Create | `src/miki/rendergraph/VsmPageTable.cpp` | internal | L | CPU page table + compute dispatch |
| Create | `shaders/rendergraph/vsm_page_request.slang` | internal | L | Page request compute |
| Create | `tests/unit/test_vsm_page_table.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define `VsmTypes.h` constants and types
- [x] **Step 2**: Define `VsmPageTable` interface
- [x] **Step 3**: Implement page request compute shader
- [x] **Step 4**: Implement CPU-side page table management
- [x] **Step 5**: Tests

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(VsmPageTable, CreateReturnsValid)` | Positive | factory success | 2 |
| `TEST(VsmPageTable, PageGridSize)` | Positive | 128x128 grid | 1 |
| `TEST(VsmPageTable, MarkDirtyAndQuery)` | Positive | dirty page tracked | 4 |
| `TEST(VsmPageTable, UpdatePageTableSetsValid)` | Positive | new page valid bit set | 4 |
| `TEST(VsmPageTable, PageRequestCompute_KnownGeometry)` | Positive | known bounding spheres mark correct pages | 3 |
| `TEST(VsmPageTable, PageTableTexture_CorrectFormat)` | Positive | R32_UINT 128x128 | 2 |
| `TEST(VsmPageTable, ClipmapLevel_NearPagesHigherRes)` | Positive | near camera pages at level 0 | 3 |
| `TEST(VsmPageTable, IncrementalRequest_NoDuplicate)` | Boundary | already-valid page not re-requested | 4 |
| `TEST(VsmPageTable, ClearAllPages)` | Boundary | all pages invalidated | 4 |
| `TEST(VsmPageTable, EndToEnd_RequestAndAllocate)` | Integration | compute request + CPU allocate round-trip | 3-4 |
| `TEST(VsmPageTable, Create_InvalidConfig_Error)` | Error | zero virtualSize returns error | 2 |
| `TEST(VsmPageTable, MoveSemantics)` | State | move transfers page table texture, source empty | 2 |
| `TEST(VsmPageTable, Reset_InvalidatesAll)` | State | reset sets all pages invalid | 4 |
