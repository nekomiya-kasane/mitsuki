# T7b.14.1 — RichTextSpan Extensions (Tolerance Stacks, Fractions, GD&T Frames)

**Phase**: 13-7b
**Component**: PMI RichText Prerequisites
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

Extend Phase 2 `RichTextSpan` to represent all PMI text styles: tolerance stacks (`+0.02/-0.01` as super/subscript spans), stacked fractions (`1/2` vertical layout), GD&T feature control frames (symbol + datum + tolerance in structured spans), multi-line notes with paragraph breaks. These are prerequisites for Phase 9 `RichTextInput` full editor.

## Steps

- [ ] **Step 1**: Extend RichTextSpan with tolerance/fraction/GD&T layout types
      `[verify: compile]`
- [ ] **Step 2**: Tests (tolerance stack rendering, fraction layout, GD&T frame structure)
      `[verify: test]`
