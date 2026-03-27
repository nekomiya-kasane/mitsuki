# T7b.1.5 — OBB Compute (PCA on Vertex Positions)

**Phase**: 13-7b
**Component**: GPU Measurement
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.1.1 | PrecisionArithmetic.slang | Not Started |

## Scope

Per arch spec: "GPU compute minimum-volume OBB via PCA on vertex positions (covariance matrix → eigenvalue decomposition → oriented axes → project extremes). Single dispatch per body."

- **Algorithm**: (1) Compute centroid (mean of all vertices). (2) Compute 3×3 covariance matrix (parallel reduction). (3) Eigenvalue decomposition → 3 oriented axes. (4) Project all vertices onto axes → min/max extents → OBB half-extents.
- **Eigenvalue**: Jacobi iteration (4-5 sweeps) on GPU — 3×3 matrix is tiny, done in-thread.
- **Visualization**: Wireframe OBB overlay with dimensions.
- **Use cases**: Precision assembly gap measurement (OBB gap < AABB gap), packaging volume estimation, collision bounding.

## Steps

- [ ] **Step 1**: Implement OBB compute shader (centroid + covariance + PCA + project)
      `[verify: compile]`
- [ ] **Step 2**: Tests (cube OBB == AABB, rotated box OBB tighter than AABB)
      `[verify: test]`
