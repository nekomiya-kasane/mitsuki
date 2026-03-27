# T7b.13.1 — Bézier Curve Data SSBO + Bounding Quad Generation

**Phase**: 13-7b
**Component**: GPU Direct Curve Text
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

Per roadmap: GPU direct Bézier curve rendering for text >48px. Glyph curve data in storage buffer (SSBO), per-glyph bounding quad. Band-based curve acceleration (N vertical bands, per-band bitset).

- **Curve data SSBO**: Per-glyph: array of quadratic/cubic Bézier segments `{float2 p0, p1, p2[, p3]}`. Band acceleration: N vertical bands (default 8), per-band uint32 bitmask of active curve segments.
- **Bounding quad**: Screen-aligned quad per glyph instance, sized to glyph bbox + 1px padding for AA.
- **Selection**: `TextRenderer` auto-selects MSDF (<48px) vs direct curve (≥48px) based on `effectiveScreenPx`.

## Steps

- [ ] **Step 1**: Define curve data SSBO format + band acceleration structure
      `[verify: compile]`
- [ ] **Step 2**: Implement bounding quad generation from font metrics
      `[verify: compile]`
- [ ] **Step 3**: Tests (curve data extraction from FreeType, band bitmask correctness)
      `[verify: test]`
