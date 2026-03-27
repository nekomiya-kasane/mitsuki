# T7b.5.3 — Sketch Dimension Rendering (Linear/Angular/Radial/Diametral)

**Phase**: 13-7b
**Component**: Sketch Renderer
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.5.1 | Sketch Entity Types + SDF Render | Not Started |

## Scope

Reuses `TextRenderer` (Phase 2) MSDF text + arrow/leader infrastructure projected onto sketch plane. Dimension types: linear (horizontal/vertical/aligned), angular, radial, diametral, arc length. Driving dimensions = black, reference (driven) = gray italic. Dimension drag: miki captures drag → `IKernel::SetDimensionValue(dimId, newValue)` → kernel re-solves → miki re-renders.

## Steps

- [ ] **Step 1**: Implement dimension rendering (text + arrows on sketch plane)
      `[verify: compile]`
- [ ] **Step 2**: Implement dimension drag → IKernel value update pipeline
      `[verify: compile]`
- [ ] **Step 3**: Tests (dimension accuracy, drag update, driving vs reference display)
      `[verify: test]`
