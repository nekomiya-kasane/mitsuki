# T7b.5.4 — Sketch Edit Mode (3D Fade, Grid, Camera Orient, 2D Snap)

**Phase**: 13-7b
**Component**: Sketch Renderer
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.5.3 | Sketch Dimension Rendering | Not Started |

## Scope

When user enters sketch editing: non-sketch 3D geometry fades to 30% opacity (LayerStack opacity override), sketch plane grid appears (reuse AdaptiveGrid projected to sketch plane local coords), camera auto-orients to sketch plane normal (smooth interpolation). `Sketch2DSnap`: project 3D cursor to sketch plane, snap to endpoint/midpoint/center/intersection/nearest-on-curve/tangent. Visual snap indicator (green dot + type label).

## Steps

- [ ] **Step 1**: Implement sketch edit mode transitions (fade, grid, camera orient)
      `[verify: compile]`
- [ ] **Step 2**: Implement Sketch2DSnap with snap candidates
      `[verify: compile]`
- [ ] **Step 3**: Tests (mode transitions, snap accuracy)
      `[verify: test]`
