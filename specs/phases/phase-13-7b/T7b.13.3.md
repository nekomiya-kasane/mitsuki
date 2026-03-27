# T7b.13.3 — Hybrid MSDF/Curve Selection (Auto-Switch at 48px Threshold)

**Phase**: 13-7b
**Component**: GPU Direct Curve Text
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.13.2 | Fragment Shader Winding-Number | Not Started |

## Scope

`TextRenderer` auto-selects MSDF (<48px) vs direct curve (≥48px) based on `effectiveScreenPx = fontSize * viewportScale`. Smooth transition: at 48px boundary, blend between MSDF and curve paths over 4px range to avoid pop. Per-glyph instance data includes `renderMode` flag for shader dispatch.

## Steps

- [ ] **Step 1**: Implement hybrid selection logic in TextRenderer
      `[verify: compile]`
- [ ] **Step 2**: Tests (correct mode selection at various sizes, smooth transition)
      `[verify: test]`
