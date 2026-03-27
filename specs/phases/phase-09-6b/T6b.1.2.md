# T6b.1.2 — Meshlet Adjacency Graph

**Phase**: 09-6b
**Component**: ClusterDAG Builder
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.1.1 | ClusterDAG Types | Not Started | `ClusterLevel`, `ClusterNode` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/MeshletAdjacency.h` | **shared** | **M** | `MeshletAdjacencyGraph::Build()`, adjacency edge list |

- **Error model**: `Build()` → `Result<MeshletAdjacencyGraph>`.
- **Algorithm**: For each pair of meshlets, check if they share boundary edges. Boundary edge = edge whose triangle-index appears in exactly one meshlet's local index buffer. Two meshlets are adjacent if they share ≥1 boundary edge.
- **Output**: adjacency list per meshlet — `vector<vector<uint32_t>>` where `adj[i]` = list of meshlet indices adjacent to meshlet `i`.

### Downstream Consumers

- T6b.1.3 (Group Partition): reads adjacency graph to partition meshlets into groups of 4.

### Technical Direction

- **O(M×E) algorithm**: For each meshlet, extract boundary edges (edge = sorted pair of global vertex indices). Hash-map lookup finds shared edges between meshlets. Expected O(M × avgBoundaryEdges) where M = meshlet count, avgBoundaryEdges ≈ 10-20.
- Per meshoptimizer: boundary edges are those where the local triangle index's opposite halfedge doesn't exist within the same meshlet.

## Steps

- [ ] **Step 1**: Define MeshletAdjacencyGraph.h + Build implementation
      `[verify: compile]`
- [ ] **Step 2**: Adjacency tests (known mesh topologies)
      `[verify: test]`
