# T7b.13.2 — Fragment Shader Winding-Number Evaluation (Lengyel 2017)

**Phase**: 13-7b
**Component**: GPU Direct Curve Text
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.13.1 | Bézier Curve Data SSBO | Not Started |

## Scope

Per roadmap: "Fragment shader winding-number algorithm (Lengyel 2017 / GreenLightning). Per-pixel quadratic intersection against curve segments. Band-based acceleration: only test curves in the pixel's band (bitmask lookup). Coverage = winding number parity (even=outside, odd=inside)."

- **Algorithm**: For each pixel in glyph quad, iterate curve segments active in the pixel's vertical band. For each quadratic Bézier: solve `t` where curve crosses pixel's y-coordinate → accumulate winding number from crossing direction.
- **Anti-aliasing**: SDF-like coverage from sub-pixel distance to nearest curve. Optional 3× R/G/B subpixel sampling.
- **Performance**: Band acceleration reduces per-pixel curve tests from ~30 to ~4-8 on average.
- **All 5 backends**: Fragment shader SSBO read only, no compute/atomic required.

## Steps

- [ ] **Step 1**: Implement winding-number fragment shader with band acceleration
      `[verify: compile]`
- [ ] **Step 2**: Implement AA coverage (SDF distance + optional subpixel)
      `[verify: compile]`
- [ ] **Step 3**: Tests (glyph rendering quality at 48px/96px/200px, band correctness)
      `[verify: test]`
