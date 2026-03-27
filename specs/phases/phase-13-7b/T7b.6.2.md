# T7b.6.2 — Adaptive Subdivision (Screen-Space Curvature Metric, LOD)

**Phase**: 13-7b
**Component**: Parametric Tessellation
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.6.1 | GPU NURBS Surface Evaluation | Not Started |

## Scope

Screen-space error metric: subdivide until pixel deviation < 1px. LOD: coarser tessellation for distant surfaces. Adaptive grid refinement: start with coarse N×N grid, subdivide cells where curvature exceeds threshold. Output: non-uniform grid with variable density.

## Steps

- [ ] **Step 1**: Implement adaptive subdivision with screen-space error metric
      `[verify: compile]`
- [ ] **Step 2**: Tests (uniform sphere → uniform grid, high-curvature region → denser grid)
      `[verify: test]`
