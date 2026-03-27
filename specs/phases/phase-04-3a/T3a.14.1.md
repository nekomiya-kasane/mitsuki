# T3a.14.1 — Glyph Outline Extraction + Special Symbol Atlas (GD&T/ISO)

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 14 — TextRenderer — Outlines & Symbols (deferred from Phase 2 T2.7.7)
**Roadmap Ref**: `roadmap.md` L1267, L1271 — Print-quality vector output + Special symbol atlas
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.8.1 | Font Loading | Not Started | `FontManager`, FreeType face for outline extraction |
| T3a.11.1 | Glyph Atlas | Not Started | Atlas page management for symbol atlas |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/text/GlyphOutline.h` | **public** | **M** | `GetGlyphOutlines()` — Bezier path extraction for SVG/PDF |
| `include/miki/text/SymbolAtlas.h` | **shared** | **M** | `SymbolAtlas` — dedicated MSDF page for CAD/CAE symbols |
| `src/miki/text/GlyphOutline.cpp` | internal | L | FreeType outline decomposition |
| `src/miki/text/SymbolAtlas.cpp` | internal | L | Pre-baked symbol MSDF page |
| `tests/unit/test_glyph_outline.cpp` | internal | L | Unit tests |

- **Bezier paths**: extract TTF glyph outlines as `vector<BezierPath>` for SVG/PDF embedding (Phase 15a)
- **Symbol atlas**: dedicated MSDF page for welding (ISO 2553), surface texture (ISO 1302), GD&T (ISO 1101), electrical symbols
- **Pre-baked**: symbol atlas generated at build time (or first launch), stored as texture

### Downstream Consumers

- `GlyphOutline.h` (**public**, heat **M**):
  - Phase 15a: vector export (SVG/PDF) uses glyph outlines for embedded text
- `SymbolAtlas.h` (**shared**, heat **M**):
  - T3a.15.1 (text_demo) — display engineering symbols
  - Phase 7b: PMI annotation uses GD&T symbols

### Upstream Contracts
- T3a.8.1: `FontManager` — FreeType face access for `FT_Outline_Decompose()`
- T3a.11.1: `GlyphAtlas` — page management patterns for symbol atlas

### Technical Direction
- **FT_Outline_Decompose**: callback-based outline decomposition → `moveTo/lineTo/conicTo/cubicTo`
- **BezierPath**: `{ vector<BezierSegment> segments; bool closed; }` where segment = line/quadratic/cubic
- **Symbol atlas**: ~110 symbols total (30 GD&T + 30 welding + 30 surface + 20 electrical)
- **Pre-baked MSDF**: generate all symbols at startup (or cache to disk), single 512×512 page

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/text/GlyphOutline.h` | **public** | **M** | Outline API |
| Create | `include/miki/text/SymbolAtlas.h` | **shared** | **M** | Symbol atlas API |
| Create | `src/miki/text/GlyphOutline.cpp` | internal | L | FreeType decompose |
| Create | `src/miki/text/SymbolAtlas.cpp` | internal | L | Symbol MSDF page |
| Create | `tests/unit/test_glyph_outline.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define GlyphOutline.h + SymbolAtlas.h
      **Signatures** (`GlyphOutline.h`):

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `BezierSegment` | `{ enum Type { Line, Quadratic, Cubic }; Type type; float2 p0, p1, p2, p3; }` | p2/p3 unused for line/quadratic |
      | `BezierPath` | `{ std::vector<BezierSegment> segments; bool closed; }` | — |
      | `GetGlyphOutlines` | `(FontManager&, FontId, uint32_t codepoint) -> expected<vector<BezierPath>, ErrorCode>` | `[[nodiscard]]` free function |

      **Signatures** (`SymbolAtlas.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `GdtSymbol` | `enum class { Flatness, Straightness, Circularity, Cylindricity, ... }` | ISO 1101 symbols |
      | `SymbolAtlas::Create` | `(IDevice&, FontManager&, MsdfGenerator&) -> expected<SymbolAtlas, ErrorCode>` | `[[nodiscard]]` static |
      | `GetSymbol` | `(GdtSymbol) const -> GlyphEntry const*` | `[[nodiscard]]` |
      | `GetTexture` | `() const -> TextureHandle` | `[[nodiscard]]` — single 512×512 page |

      **Acceptance**: compiles
      `[verify: compile]`

- [ ] **Step 2**: Implement glyph outline extraction
      **Acceptance**: 'A' outline has non-empty Bezier paths
      `[verify: test]`

- [ ] **Step 3**: Implement symbol atlas (GD&T/welding/surface symbols)
      **Acceptance**: GD&T symbols loaded into texture
      `[verify: test]`

- [ ] **Step 4**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(GlyphOutline, ExtractA)` | Positive | 'A' produces closed paths | 2-4 |
| `TEST(GlyphOutline, ExtractSpace)` | Boundary | Space has no outline | 2-4 |
| `TEST(GlyphOutline, BezierSegmentTypes)` | Positive | Mix of line/quadratic/cubic | 2-4 |
| `TEST(SymbolAtlas, Create)` | Positive | Atlas creates with symbols | 3-4 |
| `TEST(SymbolAtlas, GetFlatness)` | Positive | Flatness symbol has valid entry | 3-4 |
| `TEST(SymbolAtlas, TextureValid)` | Positive | Texture handle is valid | 3-4 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
