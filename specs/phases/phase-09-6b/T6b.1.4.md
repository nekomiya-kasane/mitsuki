# T6b.1.4 — Border-Locked QEM Simplify

**Phase**: 09-6b
**Component**: ClusterDAG Builder
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.1.3 | Group Partition | Not Started | `MeshletGroup` list |

## Context Anchor

### This Task's Contract

**Produces**: `ClusterSimplifier::SimplifyGroup(MeshletGroup, meshlets, vertices, indices, config) -> MeshletBuildResult`

- **Algorithm**: (1) Merge all triangles from group's meshlets into one mega-mesh. (2) Lock boundary vertices (vertices shared with meshlets outside the group). (3) QEM simplify to `targetRatio` (default 50%). (4) Split simplified mesh back into meshlets via `MeshletGenerator::Build()`.
- **Border locking**: Vertices on the group boundary get infinite quadric error weight → never collapsed. This prevents cracks between adjacent LOD groups (critical for seamless LOD transitions).
- **Error computation**: Track max simplification error across all collapses → stored as `ClusterNode.childError` for this level.
- Per meshoptimizer: `meshopt_SimplifyLockBorder` flag achieves the same effect. We use the same approach.

### Downstream Consumers

- T6b.1.5 (Recursive DAG Construction): receives simplified meshlets + error metric per group.

### Technical Direction

- Uses T6b.8.1 CPU QEM simplifier (or meshoptimizer-compatible internal implementation).
- Boundary vertex detection: vertex is boundary if it appears in meshlets outside the current group.
- Quality target: Garland-Heckbert 1997 quality (< 0.1% max screen-space error at intended LOD distance).

## Steps

- [ ] **Step 1**: Implement ClusterSimplifier with border detection + locked QEM
      `[verify: compile]`
- [ ] **Step 2**: Tests (simplification ratio, boundary preservation, error monotonicity)
      `[verify: test]`
