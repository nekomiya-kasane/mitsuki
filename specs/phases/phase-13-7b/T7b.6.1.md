# T7b.6.1 — GPU NURBS Surface Evaluation Compute

**Phase**: 13-7b
**Component**: Parametric Tessellation
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

None (L0 task).

## Scope

Per arch spec Parametric Tessellation Details Phase 1: GPU NURBS/BSpline evaluation compute shader. Control point buffer (SSBO), knot vector buffer, order/degree uniforms. Compute shader evaluates surface points on adaptive grid → outputs position + normal + UV. Uses De Boor's algorithm for B-spline basis evaluation.

- **Input**: control points SSBO (float3[]), knot vectors U/V SSBO (float[]), degree u/v (push constants).
- **Output**: evaluated position + normal + UV per grid point → staging buffer → GPU vertex buffer.
- **Performance**: ≥10× faster than kernel CPU tessellation for untrimmed surfaces.

## Steps

- [ ] **Step 1**: Implement De Boor B-spline basis evaluation in Slang compute shader
      `[verify: compile]`
- [ ] **Step 2**: Implement surface point + normal evaluation (u,v) → (pos, normal, uv)
      `[verify: compile]`
- [ ] **Step 3**: Tests (known NURBS surface vs analytical, Bézier patch special case)
      `[verify: test]`
