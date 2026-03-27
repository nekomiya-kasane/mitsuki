# T6b.1.5 — Recursive DAG Construction

**Phase**: 09-6b
**Component**: ClusterDAG Builder
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.1.4 | Border-Locked QEM Simplify | Not Started | `ClusterSimplifier::SimplifyGroup()` |

## Context Anchor

### This Task's Contract

**Produces**: `ClusterDAG::Build()` full implementation — iterates levels: Level 0 = original meshlets → partition into groups → simplify each group → Level 1 meshlets → repeat → ... → Level N (single root cluster).

- **Termination**: Stop when (a) meshlet count <= groupSize (can't partition further), or (b) simplification produces 0 reduction, or (c) maxLodLevels reached.
- **Error metric**: Each level computes `ClusterNode.parentError = max(childError for all children in group) + simplificationError`. Monotonicity: parent error > all children errors.

### Downstream Consumers

- T6b.1.6 (Validation): validates error monotonicity across all levels.
- T6b.2.2 (GPU DAG Cut): reads complete ClusterDAG for per-frame LOD selection.

## Steps

- [ ] **Step 1**: Implement recursive Build() loop
      `[verify: compile]`
- [ ] **Step 2**: Tests (multi-level DAG, error monotonicity, level count)
      `[verify: test]`
