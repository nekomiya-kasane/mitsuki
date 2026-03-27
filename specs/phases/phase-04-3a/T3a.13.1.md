# T3a.13.1 — RichTextSpan Layout Engine + Bitmap Fallback Crossfade

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 13 — TextRenderer — RichText (deferred from Phase 2 T2.7.6)
**Roadmap Ref**: `roadmap.md` L1269 — Rich text support (TextRenderer Details)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.12.1 | GPU Text Pipeline | Not Started | `TextRenderer::Draw()`, `TextDrawCmd` |
| T3a.9.1 | Text Shaping | Not Started | `TextShaper::ShapeText()` per-span |
| T3a.11.1 | Glyph Atlas | Not Started | `GlyphAtlas` for bitmap fallback cache |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/text/RichText.h` | **public** | **M** | `RichTextSpan`, `RichTextNode`, `RichTextLayout` — rich text layout engine |
| `src/miki/text/RichTextLayout.cpp` | internal | L | Layout implementation |
| `src/miki/text/BitmapGlyphCache.cpp` | internal | L | Bitmap fallback for <8px text |
| `tests/unit/test_rich_text.cpp` | internal | L | Unit tests |

- **RichTextSpan**: `{start, end, fontId, size, bold, italic, underline, strikethrough, color, superscript, subscript}`
- **RichTextNode**: tree-of-nodes for nesting (bold inside italic inside color)
- **Layout**: processes span array → per-glyph position/style, supports inline format changes
- **Bitmap fallback**: for text <8px, pre-rasterized hinted bitmap (FreeType auto-hinter)
- **Crossfade band [8,16]px**: `float t = smoothstep(8.0, 16.0, effectiveScreenPx); alpha = mix(bitmapAlpha, msdfAlpha, t);`

### Downstream Consumers

- `RichText.h` (**public**, heat **M**):
  - T3a.15.1 (text_demo) — rich text rendering in demo
  - Phase 7b: PMI annotation rendering
  - Phase 9: RichTextInput editor

### Upstream Contracts
- T3a.12.1: `TextRenderer::Draw()` for final GPU rendering
- T3a.9.1: `TextShaper::ShapeText()` for per-span shaping
- T3a.11.1: `GlyphAtlas` for MSDF glyphs
- T3a.8.1: `FontManager` for bitmap rasterization via FreeType

### Technical Direction
- **Flat API**: `Layout(span<RichTextSpan>, maxWidth) -> LayoutResult` for simple use cases
- **Tree API**: `Layout(span<RichTextNode>, maxWidth) -> LayoutResult` for nested styles
- **Line breaking**: greedy line break at word boundaries, respecting maxWidth
- **Superscript/subscript**: 0.7× size, vertical offset ±0.4× baseline
- **Underline/strikethrough**: SDF line at baseline offset (not separate draw — baked into glyph quad)
- **BitmapGlyphCache**: per-size cache of FreeType-rasterized bitmaps, uploaded as separate atlas page

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/text/RichText.h` | **public** | **M** | Rich text API |
| Create | `src/miki/text/RichTextLayout.cpp` | internal | L | Layout engine |
| Create | `src/miki/text/BitmapGlyphCache.cpp` | internal | L | Bitmap fallback |
| Create | `tests/unit/test_rich_text.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define RichText.h (public M)
      **Signatures**:

      | Symbol | Fields / Signature | Attrs |
      |--------|-------------------|-------|
      | `RichTextSpan` | `{ uint32_t start, end; FontId fontId; float size; bool bold, italic, underline, strikethrough; float4 color; bool superscript, subscript; }` | — |
      | `RichTextNode` | `{ RichTextSpan style; std::vector<RichTextNode> children; std::string_view text; }` | tree node |
      | `LayoutGlyph` | `{ ShapedGlyph glyph; float2 position; float4 color; float size; uint32_t flags; }` | positioned glyph |
      | `LayoutResult` | `{ std::vector<LayoutGlyph> glyphs; float2 boundingBox; uint32_t lineCount; }` | — |
      | `RichTextLayout::Layout` | `(span<const RichTextSpan>, std::string_view text, float maxWidth) -> expected<LayoutResult, ErrorCode>` | `[[nodiscard]]` flat |
      | `RichTextLayout::Layout` | `(span<const RichTextNode>, float maxWidth) -> expected<LayoutResult, ErrorCode>` | `[[nodiscard]]` tree |

      **Acceptance**: compiles
      `[verify: compile]`

- [ ] **Step 2**: Implement flat layout (span array → positioned glyphs)
      **Acceptance**: "Hello **World**" lays out with bold span
      `[verify: test]`

- [ ] **Step 3**: Implement bitmap fallback + crossfade
      **Acceptance**: <8px text uses bitmap, [8,16]px crossfades
      `[verify: test]`

- [ ] **Step 4**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(RichText, SingleSpan)` | Positive | Plain text lays out correctly | 2-4 |
| `TEST(RichText, BoldSpan)` | Positive | Bold span uses bold font | 2-4 |
| `TEST(RichText, MultiSpan_ColorChange)` | Positive | Color changes mid-string | 2-4 |
| `TEST(RichText, Superscript)` | Positive | Superscript offset + 0.7× size | 2-4 |
| `TEST(RichText, LineBreak)` | Positive | Long text wraps at maxWidth | 2-4 |
| `TEST(RichText, BitmapFallback_SmallSize)` | Positive | <8px uses bitmap path | 3-4 |
| `TEST(RichText, Crossfade_Band)` | Positive | [8,16]px produces blended alpha | 3-4 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
