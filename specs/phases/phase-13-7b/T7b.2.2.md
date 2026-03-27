# T7b.2.2 — CSG Resolve Compute (Per-Pixel Boolean on Depth Intervals)

**Phase**: 13-7b
**Component**: GPU Boolean Preview
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.2.1 | Depth Peeling Compute | Not Started |

## Scope

Per arch spec §3 Pass #34: CSG resolve on depth intervals from depth peeling. Per-pixel: read N depth layers for body A and body B. Compute boolean operation (union/subtract/intersect) on depth intervals. Output: composited color + depth for preview layer.

- **Union**: merge all intervals from A and B.
- **Subtract (A-B)**: invert B intervals, intersect with A intervals.
- **Intersect**: overlap of A and B intervals.
- **Edge highlight**: detect boolean boundary pixels (transition between inside-A-only and inside-both) → render edge color.
- **Preview layer**: renders to Phase 8 Preview layer (separate from main scene, reduced quality).

## Steps

- [ ] **Step 1**: Implement CSG interval arithmetic compute shader (union/subtract/intersect)
      `[verify: compile]`
- [ ] **Step 2**: Implement edge highlight detection
      `[verify: compile]`
- [ ] **Step 3**: Tests (sphere-sphere union/subtract/intersect vs analytical)
      `[verify: test]`
