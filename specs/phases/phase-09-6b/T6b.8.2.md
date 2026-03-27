# T6b.8.2 — QEM Integration with ClusterDAG (Per-Group Simplification)

**Phase**: 09-6b
**Component**: GPU Mesh Simplification (QEM)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.8.1 | CPU QEM Simplifier | Not Started | `MeshSimplifier::Simplify()` |
| T6b.1.3 | Group Partition | Not Started | `MeshletGroup` list |

## Context Anchor

### This Task's Contract

**Produces**: Glue code that connects T6b.1.4 (ClusterSimplifier) with T6b.8.1 (MeshSimplifier). Per-group: extract boundary vertices → lock → simplify → split into meshlets.

This task is the integration point where QEM simplification is used within the ClusterDAG build loop. T6b.1.4 calls this code for each group at each LOD level.

## Steps

- [ ] **Step 1**: Wire MeshSimplifier into ClusterSimplifier::SimplifyGroup
      `[verify: compile]`
- [ ] **Step 2**: Tests (per-group simplification quality, integration with DAG build)
      `[verify: test]`
