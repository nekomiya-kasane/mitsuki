# T7b.1.4 — Mass Properties Compute (Area/Volume/Centroid/Inertia, Float64 Reduction)

**Phase**: 13-7b
**Component**: GPU Measurement
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.1.1 | PrecisionArithmetic.slang | Not Started |

## Scope

Per arch spec: "GPU parallel reduction on triangle mesh → area, volume, centroid, inertia tensor (float64 accumulation). Single dispatch per body, 1000-body assembly <10ms vs CPU >1s."

- **Surface area**: Sum of triangle areas. `area = 0.5 * length(cross(v1-v0, v2-v0))`.
- **Volume**: Signed volume via divergence theorem. Per-triangle: `volume += dot(v0, cross(v1, v2)) / 6.0`.
- **Centroid**: Volume-weighted centroid. Per-triangle contribution summed.
- **Inertia tensor**: 3×3 symmetric matrix. Per-triangle contribution via Mirtich 1996 fast inertia algorithm.
- **Precision**: All accumulation via `precision_float` (double or DSFloat). Final result: <0.01% error vs `IKernel::ExactMassProperties`.
- **Dispatch**: Single compute per body. Workgroup reduction → block sums → final reduction. 1000-body assembly: 1000 dispatches, <10ms total.

## Steps

- [ ] **Step 1**: Implement per-triangle area/volume/centroid/inertia contribution compute
      `[verify: compile]`
- [ ] **Step 2**: Implement GPU parallel reduction (workgroup → block → final)
      `[verify: compile]`
- [ ] **Step 3**: Tests (sphere analytical reference, cube, vs IKernel::ExactMassProperties <0.01%)
      `[verify: test]`
