# T6b.1.3 — Group Partition (Group-of-4 Meshlets by Adjacency)

**Phase**: 09-6b
**Component**: ClusterDAG Builder
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.1.2 | Meshlet Adjacency Graph | Not Started | `MeshletAdjacencyGraph` |

## Context Anchor

### This Task's Contract

**Produces**: `GroupPartitioner::Partition(adjacencyGraph, groupSize=4) -> vector<MeshletGroup>`. Each `MeshletGroup` = list of meshlet indices that will be merged+simplified together.

- **Algorithm**: METIS-style graph partitioning on the adjacency graph. For simplicity in Phase 6b: greedy BFS from seed meshlet, add most-connected neighbor until group is full. Balanced partition (each group has exactly `groupSize` meshlets, last group may have fewer).
- Per meshoptimizer research: group-of-4 is the standard for Nanite-grade LOD. Groups must be spatially coherent (adjacent meshlets) to minimize boundary after simplification.

### Downstream Consumers

- T6b.1.4 (Border-Locked QEM): receives `MeshletGroup` list, merges each group's triangles.
- T6b.8.2 (QEM Integration): same partition used for per-group simplification.

## Steps

- [ ] **Step 1**: Implement GroupPartitioner
      `[verify: compile]`
- [ ] **Step 2**: Tests (balanced groups, adjacency preserved)
      `[verify: test]`
