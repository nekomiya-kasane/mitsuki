# T7b.5.2 — Constraint Icons (MSDF Atlas) + Color Coding

**Phase**: 13-7b
**Component**: Sketch Renderer
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.5.1 | Sketch Entity Types + SDF Render | Not Started |

## Scope

MSDF icon atlas (32×32 px per icon) for sketch constraints: Fixed (lock), Coincident (dot), Parallel, Perpendicular, Tangent, Equal (=), Symmetric, Horizontal (H), Vertical (V), Concentric. Icons positioned at constraint midpoint, billboarded. Color coding: under-constrained=blue, fully-constrained=green, over-constrained=red. Color per-entity from `IKernel::GetSketchDOF(sketchId)`.

## Steps

- [ ] **Step 1**: Create constraint icon MSDF atlas + instanced icon render
      `[verify: compile]`
- [ ] **Step 2**: Implement DOF-based color coding
      `[verify: compile]`
- [ ] **Step 3**: Tests (icon placement, color coding correctness)
      `[verify: test]`
