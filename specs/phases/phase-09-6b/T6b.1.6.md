# T6b.1.6 — Error Metric Monotonicity Validation + ClusterDAG Tests

**Phase**: 09-6b
**Component**: ClusterDAG Builder
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.1.5 | Recursive DAG Construction | Not Started | Complete `ClusterDAG` |

## Context Anchor

### This Task's Contract

**Produces**: Comprehensive test suite validating ClusterDAG correctness.

- **Monotonicity invariant**: For every ClusterNode at level L, `node.parentError >= max(child.childError)` across all children at level L-1. Violation → geometry pops during LOD transition.
- **Coverage**: Every triangle in the original mesh appears in exactly one meshlet at Level 0. Every Level 0 meshlet has a path to a root node.
- **Simplification ratio**: Each level has approximately `groupSize`× fewer meshlets than the previous level (±20% tolerance for boundary effects).

## Steps

- [ ] **Step 1**: Implement `ClusterDAG::Validate()` method + comprehensive tests
      `[verify: test]`

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(ClusterDAG, Build_SimpleMesh)` | Positive | 1K tri mesh → multi-level DAG |
| `TEST(ClusterDAG, ErrorMonotonicity)` | Positive | parent error > child error at all levels |
| `TEST(ClusterDAG, TriangleCoverage_Level0)` | Positive | all original triangles present at Level 0 |
| `TEST(ClusterDAG, LevelCount_Reasonable)` | Boundary | 10K tri → 4-6 levels |
| `TEST(ClusterDAG, SimplificationRatio)` | Positive | each level ~4× fewer meshlets |
| `TEST(ClusterDAG, EmptyMesh)` | Boundary | 0 triangles → empty DAG |
| `TEST(ClusterDAG, SingleMeshlet)` | Boundary | 1 meshlet → 1 level |
| `TEST(ClusterDAG, EndToEnd_10K)` | Integration | 10K tri sphere → full DAG build + validate |
