# T3a.9.1 — HarfBuzz Text Shaping + Script Detection + Font Fallback + ShapingCache

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 9 — TextRenderer — Shaping (deferred from Phase 2 T2.7.2)
**Roadmap Ref**: `roadmap.md` L1249 — Text shaping (TextRenderer Details)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.8.1 | Font Loading | Not Started | `FontManager`, `FontId`, FreeType face access |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/text/TextShaper.h` | **public** | **H** | `TextShaper` — HarfBuzz shaping, script detection, fallback |
| `include/miki/text/GlyphRun.h` | **public** | **H** | `GlyphRun`, `ShapedGlyph` — shaping output types |
| `src/miki/text/TextShaper.cpp` | internal | L | HarfBuzz integration |
| `src/miki/text/ShapingCache.cpp` | internal | L | LRU cache (1024 entries) |
| `tests/unit/test_text_shaper.cpp` | internal | L | Unit tests |

- **Error model**: `ShapeText()` returns `expected<GlyphRun, ErrorCode>`
- **HarfBuzz**: complex script shaping (kerning, ligatures, RTL, vertical CJK, combining marks)
- **ShapingCache**: LRU cache (key = `hash(string, fontId, fontSize, isRTL)`, 1024 entries)
- **ASCII fast path**: simple left-to-right advance-width layout (no HarfBuzz call, <0.01ms)

### Downstream Consumers

- `TextShaper.h` (**public**, heat **H**):
  - T3a.12.1 (GPU Pipeline) — `GlyphRun` for instanced quad generation
  - T3a.13.1 (RichText) — per-span shaping
- `GlyphRun.h` (**public**, heat **H**):
  - All text rendering consumers

### Upstream Contracts
- T3a.8.1: `FontManager::GetFreetypeFace()` (internal), `FontId`, `GetGlyphIndex()`
- External: HarfBuzz library (CMake `find_package(harfbuzz)`)

### Technical Direction
- **HarfBuzz font from FreeType face**: `hb_ft_font_create()` wraps FT_Face
- **Script detection**: `hb_buffer_guess_segment_properties()` for auto-detect
- **Font fallback**: if shaping produces `.notdef`, retry with next font in fallback chain
- **Cache key**: FNV-1a hash of `{string_view, fontId, fontSize_bits, isRTL}`
- **ASCII fast path**: if all codepoints < 128 and LTR, use simple advance-width (skip HarfBuzz)

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/text/TextShaper.h` | **public** | **H** | Shaper API |
| Create | `include/miki/text/GlyphRun.h` | **public** | **H** | Output types |
| Create | `src/miki/text/TextShaper.cpp` | internal | L | HarfBuzz impl |
| Create | `src/miki/text/ShapingCache.cpp` | internal | L | LRU cache |
| Create | `tests/unit/test_text_shaper.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define GlyphRun.h + TextShaper.h (public H)
      **Signatures** (`GlyphRun.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `ShapedGlyph` | `{ uint32_t glyphIndex; FontId fontId; float xOffset, yOffset; float xAdvance; }` | — |
      | `GlyphRun` | `{ std::vector<ShapedGlyph> glyphs; float totalAdvance; bool isRTL; }` | — |

      **Signatures** (`TextShaper.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `TextShaper::Create` | `(FontManager&) -> expected<TextShaper, ErrorCode>` | `[[nodiscard]]` static |
      | `ShapeText` | `(std::string_view text, FontId, float fontSize, bool isRTL = false) -> expected<GlyphRun, ErrorCode>` | `[[nodiscard]]` |
      | `SetFallbackChain` | `(span<const FontId>) -> void` | — |
      | `ClearCache` | `() -> void` | — |
      | `GetCacheHitRate` | `() const -> float` | `[[nodiscard]]` |

      **Acceptance**: compiles
      `[verify: compile]`

- [ ] **Step 2**: Implement HarfBuzz shaping + ASCII fast path
      **Acceptance**: "Hello" shapes correctly
      `[verify: test]`

- [ ] **Step 3**: Font fallback + script detection
      **Acceptance**: CJK text shapes with fallback font
      `[verify: test]`

- [ ] **Step 4**: ShapingCache (LRU, 1024 entries)
      **Acceptance**: cache hit on repeated string
      `[verify: test]`

- [ ] **Step 5**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(TextShaper, Create)` | Positive | HarfBuzz initializes | 2-5 |
| `TEST(TextShaper, ShapeASCII)` | Positive | "Hello" produces 5 glyphs | 2-5 |
| `TEST(TextShaper, ShapeASCII_FastPath)` | Positive | ASCII skips HarfBuzz | 2-5 |
| `TEST(TextShaper, ShapeKerning)` | Positive | "AV" has negative kern offset | 2-5 |
| `TEST(TextShaper, ShapeCJK)` | Positive | CJK codepoints shaped | 3-5 |
| `TEST(TextShaper, ShapeRTL)` | Positive | Arabic text RTL order | 3-5 |
| `TEST(TextShaper, FallbackChain)` | Positive | Missing glyph resolved via fallback | 3-5 |
| `TEST(TextShaper, CacheHit)` | Positive | Second call hits cache | 4-5 |
| `TEST(TextShaper, CacheMiss_DifferentSize)` | Positive | Different fontSize misses | 4-5 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
