# T6b.5.1 — PersistentDispatch (Workgroup-Persistent Compute)

**Phase**: 09-6b
**Component**: Persistent Compute
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/PersistentDispatch.h` | **public** | **H** | `PersistentDispatch::Create()`, `Submit()` |
| `shaders/vgeo/persistent_loop.slang` | **shared** | **M** | Workgroup loop on atomic work queue |

- **Pattern**: N workgroups launched once, each loops: atomicAdd on global counter to claim next work item → process → repeat until counter >= totalItems. Avoids dispatch overhead for many small tasks.
- **Use cases**: BVH refit (1K dirty nodes, <0.1ms), streaming decompress (≥2GB/s), incremental TLAS rebuild.
- **API**: `Submit(cmd, workItemBuffer, workItemCount)` — records a single dispatch of N persistent workgroups.

### Downstream Consumers

- T6b.5.2 (BVH Refit): uses PersistentDispatch for incremental refit.
- T6b.6.2 (ChunkLoader): uses PersistentDispatch for stream decompression.
- Phase 14: extends for OneSweep radix sort work stealing.

## Steps

- [ ] **Step 1**: Define PersistentDispatch.h + persistent_loop.slang
      `[verify: compile]`
- [ ] **Step 2**: Tests (work queue completeness, no items missed/duplicated)
      `[verify: test]`
