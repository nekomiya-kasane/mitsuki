# T7b.1.6 — Measurement Tests (vs Analytical + vs IKernel::ExactMassProperties)

**Phase**: 13-7b
**Component**: GPU Measurement
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.1.4 | Mass Properties Compute | Not Started |

## Scope

Comprehensive measurement validation: distance accuracy (sphere pair at known separation), angle accuracy (two planes at known angle < 0.001°), radius (cylinder fit), mass properties vs `IKernel::ExactMassProperties` (< 0.01% error for sphere volume/area/centroid/inertia), OBB vs AABB tightness, body-body distance vs CPU brute-force, DSFloat vs double path comparison (< 1e-9 relative error), RTE coordinate precision at 10km distance.

## Steps

- [ ] **Step 1**: Write all measurement accuracy + DS validation tests
      `[verify: test]`
