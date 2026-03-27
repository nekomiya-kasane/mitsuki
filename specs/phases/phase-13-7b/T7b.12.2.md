# T7b.12.2 — FriBidi Integration (Paragraph-Level UAX#9 BiDi Reordering)

**Phase**: 13-7b
**Component**: Color Emoji & BiDi
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.12.1 | ColorGlyphCache | Not Started |

## Scope

Integrate FriBidi (LGPL-2.1+) or ICU for UAX #9 paragraph reordering. `TextShaper::ShapeText()` calls FriBidi before HarfBuzz to reorder mixed LTR+RTL runs (e.g., "Ø12.5 مم"). Required for correct PMI annotation display with Arabic/Hebrew mixed text.

## Steps

- [ ] **Step 1**: Integrate FriBidi library + call in TextShaper pipeline
      `[verify: compile]`
- [ ] **Step 2**: Tests (mixed LTR+RTL text ordering, pure RTL, pure LTR unchanged)
      `[verify: test]`
