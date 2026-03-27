# T6b.6.4 — Progressive Rendering (Coarse LOD < 100ms First Frame)

**Phase**: 09-6b
**Component**: Cluster Streaming
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.6.3 | OctreeResidency + LRU | Not Started | Residency state tracking |
| T6b.2.2 | GPU DAG Cut Optimizer | Not Started | LOD selection for partial-resident scene |

## Context Anchor

### This Task's Contract

**Produces**: Progressive rendering path — first frame renders coarsest resident LOD level within 100ms of file open. Full detail streams in over subsequent frames.

- **First-frame path**: On scene open, only root-level clusters are guaranteed resident (small data, loaded with header). Render root level immediately → user sees coarse geometry within 100ms. Background streaming fills finer LODs.
- **Progressive refinement**: Each frame, DAG cut optimizer selects best available LOD considering residency. As finer clusters arrive, LOD improves seamlessly.
- Per arch spec §5.6: "first frame renders coarse LOD within 100ms of file open".

## Steps

- [ ] **Step 1**: Implement progressive render path with residency-aware LOD selection
      `[verify: compile]`
- [ ] **Step 2**: Tests (first-frame latency < 100ms, progressive quality improvement)
      `[verify: test]`
