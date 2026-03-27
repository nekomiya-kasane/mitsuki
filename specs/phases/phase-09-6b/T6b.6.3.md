# T6b.6.3 — OctreeResidency + LRU Eviction

**Phase**: 09-6b
**Component**: Cluster Streaming
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.6.2 | ChunkLoader | Not Started | `ChunkLoader::RequestLoad()`, `IsResident()` |

## Context Anchor

### This Task's Contract

**Produces**: `OctreeResidency` — tracks which clusters are GPU-resident, manages LRU eviction under memory budget.

- **Residency tracking**: Per-cluster state: `{NotResident, Loading, Resident, Evicting}`. Octree spatial index for efficient range queries (which clusters are needed for current view).
- **LRU eviction**: When memory budget exceeded, evict least-recently-used clusters. Integrates with Phase 4 `MemoryBudget` pressure states.
- **Missing cluster → coarser ancestor**: When a cluster at target LOD is not resident, render its coarser ancestor (always resident at some level). Zero visual holes.

### Downstream Consumers

- T6b.6.4 (Progressive Rendering): uses residency to determine first-frame coarse LOD.
- Phase 8: CadScene uses OctreeResidency for per-body streaming management.

## Steps

- [ ] **Step 1**: Implement OctreeResidency + LRU eviction
      `[verify: compile]`
- [ ] **Step 2**: Tests (residency state machine, LRU ordering, memory budget integration)
      `[verify: test]`
