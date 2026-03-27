# T6b.8.3 — GPU QEM Accelerator (Optional, Parallel Edge Collapse)

**Phase**: 09-6b
**Component**: GPU Mesh Simplification (QEM)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.8.1 | CPU QEM Simplifier | Not Started | Reference algorithm for validation |

## Context Anchor

### This Task's Contract

**Produces**: GPU compute QEM simplifier — parallel edge collapse with independent set selection.

- **Algorithm**: (1) Compute per-vertex quadric (compute, 1 thread/vertex). (2) Compute per-edge collapse cost (compute, 1 thread/edge). (3) Independent set selection via graph coloring (no adjacent edges collapse simultaneously). (4) Parallel collapse + compact. (5) Iterate until target count.
- **Independent set**: 2-coloring heuristic — mark edges whose lower vertex index is even as candidates. Check both endpoints not already collapsed this iteration. ~50% parallelism per iteration.
- **Performance target**: 1M → 100K in < 50ms (10× faster than CPU QEM for interactive use).
- **Validation**: GPU result must match CPU QEM within tolerance (max Hausdorff distance < 0.1%).
- **Stretch goal**: This task is optional for Phase 6b. CPU QEM (T6b.8.1) is sufficient for all import-time LOD generation. GPU QEM enables interactive simplification preview.

## Steps

- [ ] **Step 1**: Implement GPU quadric computation + edge cost shaders
      `[verify: compile]`
- [ ] **Step 2**: Implement independent set selection + parallel collapse
      `[verify: compile]`
- [ ] **Step 3**: Tests (GPU vs CPU quality comparison, performance benchmark)
      `[verify: test]`
