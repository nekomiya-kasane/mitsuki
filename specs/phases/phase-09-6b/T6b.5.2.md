# T6b.5.2 — Incremental BVH Refit via PersistentDispatch

**Phase**: 09-6b
**Component**: Persistent Compute
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.5.1 | PersistentDispatch | Not Started | `PersistentDispatch::Submit()` |

## Context Anchor

### This Task's Contract

**Produces**: `BvhRefitter::Refit(cmd, dirtyNodeBuffer, dirtyCount)` — refits BVH AABBs for moved instances using persistent compute. < 0.1ms for 1K dirty nodes.

- **Algorithm**: Bottom-up AABB refit. Each dirty leaf → walk parent chain → update AABB = union of children. Persistent workgroup claims dirty leaves from atomic queue.
- **Integration**: Reuses Phase 5 `BVH` data structure. Only modifies AABB nodes, not tree topology.

## Steps

- [ ] **Step 1**: Implement BvhRefitter + refit shader
      `[verify: compile]`
- [ ] **Step 2**: Tests (refit correctness, <0.1ms budget)
      `[verify: test]`
