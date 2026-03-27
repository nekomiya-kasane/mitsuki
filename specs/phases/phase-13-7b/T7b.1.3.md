# T7b.1.3 — Body-to-Body Min Distance (BVH Pair-Traversal Compute)

**Phase**: 13-7b
**Component**: GPU Measurement
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.1.2 | Point/Face/Distance/Angle/Radius | Not Started |

## Scope

Per arch spec: "BVH pair-traversal compute (reuse Phase 7a-2 BLAS) → per-triangle-pair closest-point, double-precision, <2ms for 100K tri pairs."

- **Algorithm**: Dual-BVH traversal. Start from root pair (BLAS_A root, BLAS_B root). Expand node pair with smallest AABB distance. Leaf-leaf: compute closest point between two triangles (Ericson 2004 closest-point-on-triangle). Track global minimum via atomic min on precision_float.
- **Performance**: <2ms for 100K triangle pairs. Pruning via AABB distance lower bound eliminates >99% of pairs.
- **Output**: `{float64 distance, float64x3 pointA, float64x3 pointB}` — readback to CPU for display.

## Steps

- [ ] **Step 1**: Implement dual-BVH traversal compute shader
      `[verify: compile]`
- [ ] **Step 2**: Implement closest-point-on-triangle (Ericson algorithm)
      `[verify: compile]`
- [ ] **Step 3**: Tests (known geometry pairs, vs CPU brute-force reference)
      `[verify: test]`
