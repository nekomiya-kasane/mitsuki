# T7b.3.1 — GPU Draft Angle Analysis (Per-Face Dot Product + Color Map Overlay)

**Phase**: 13-7b
**Component**: GPU Draft Angle Analysis
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

Per arch spec §3 Pass #35 and roadmap: "Per-face dot(normal, pullDirection) compute → draft angle map. Configurable pull direction (mold axis). Color map overlay (green=OK, red=insufficient, yellow=marginal). Threshold-based pass/fail per face. Single compute dispatch, <1ms for 1M triangles."

- **Algorithm**: Per-triangle compute: `angle = acos(abs(dot(faceNormal, pullDirection)))`. Map to color via configurable thresholds: green (>= minAngle), yellow (borderline), red (< minAngle).
- **Output**: Per-vertex color SSBO → Analysis Overlay pass (#46) reads and composites.
- **Push constants**: `{float3 pullDirection, float minAngle, float warningAngle}`.
- **Use case**: DFM validation — injection molding, casting, forging die design.

## Steps

- [ ] **Step 1**: Implement draft angle compute shader + overlay integration
      `[verify: compile]`
- [ ] **Step 2**: Tests (known geometry angles vs analytical, threshold color correctness)
      `[verify: test]`
