# T3a.8.1 — FreeType Font Loading + Metrics + System Font Discovery

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 8 — TextRenderer — Font Loading (deferred from Phase 2 T2.7.1)
**Roadmap Ref**: `roadmap.md` L1247 — Font loading (TextRenderer Details)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | (Phase 1a/1b) | Complete | `ErrorCode`, `Handle<Tag>` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/text/FontManager.h` | **public** | **H** | `FontManager` — font loading, metrics, system discovery, fallback chain |
| `include/miki/text/FontTypes.h` | **public** | **H** | `FontId`, `GlyphMetrics`, `FontMetrics`, `FontStyle` |
| `src/miki/text/FontManager.cpp` | internal | L | FreeType integration |
| `src/miki/text/SystemFontDiscovery.cpp` | internal | L | Platform-specific font discovery |
| `tests/unit/test_font_manager.cpp` | internal | L | Unit tests |

- **Error model**: `LoadFont()` returns `expected<FontId, ErrorCode>`
- **Thread safety**: `FontManager` is single-owner; `FontId` and metrics are immutable value types
- **Platform font discovery**: DirectWrite (Windows), fontconfig (Linux), CTFontManager (macOS)
- **FreeType**: font parsing, hinting, metrics extraction. Internal `FT_Face` ownership.

### Downstream Consumers

- `FontManager.h` (**public**, heat **H**):
  - T3a.9.1 (Shaping) — `GetFreetypeFace()` for HarfBuzz font creation
  - T3a.10.1 (MSDF) — glyph outlines for MSDF generation
  - T3a.11.1 (Atlas) — glyph metrics for atlas packing
  - T3a.14.1 (Outlines) — glyph outline extraction
  - Phase 7b: direct curve rendering reads glyph outlines
- `FontTypes.h` (**public**, heat **H**):
  - All TextRenderer consumers use `FontId`, `GlyphMetrics`

### Upstream Contracts
- Phase 1a: `ErrorCode` from `include/miki/core/Error.h`
- External: FreeType 2 library (CMake `find_package(Freetype)`)

### Technical Direction
- **FreeType 2**: proven, MIT-licensed, industry standard font rasterizer
- **Font fallback chain**: primary → CJK → symbol → last-resort. Configured at FontManager level.
- **System font discovery**: best-effort platform APIs. Fallback to bundled fonts if discovery fails.
- **Lazy loading**: FT_Face opened on first glyph request, not at LoadFont() time (except metrics extraction)
- **CMake integration**: `MIKI_TEXT=ON` option, links FreeType. Disabled by default for minimal builds.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/text/FontManager.h` | **public** | **H** | Font API |
| Create | `include/miki/text/FontTypes.h` | **public** | **H** | Font value types |
| Create | `src/miki/text/FontManager.cpp` | internal | L | FreeType impl |
| Create | `src/miki/text/SystemFontDiscovery.cpp` | internal | L | Platform discovery |
| Create | `src/miki/text/CMakeLists.txt` | internal | L | `miki_text` target |
| Create | `tests/unit/test_font_manager.cpp` | internal | L | Tests |
| Modify | `src/CMakeLists.txt` | internal | L | Add text subdirectory |
| Modify | `cmake/miki_options.cmake` | internal | L | Add `MIKI_TEXT` option |

## Steps

- [ ] **Step 1**: Define FontTypes.h + FontManager.h (public H)
      **Signatures** (`FontTypes.h`):

      | Symbol | Fields / Signature | Attrs |
      |--------|-------------------|-------|
      | `FontId` | `{ uint32_t value; }` | value type, `{~0u}` = invalid |
      | `GlyphMetrics` | `{ float advanceX; float bearingX, bearingY; float width, height; }` | — |
      | `FontMetrics` | `{ float ascender, descender, lineGap; float unitsPerEm; }` | — |
      | `FontStyle` | `enum class : uint8_t { Regular, Bold, Italic, BoldItalic }` | — |

      **Signatures** (`FontManager.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `FontManager::Create` | `() -> expected<FontManager, ErrorCode>` | `[[nodiscard]]` static |
      | `LoadFont` | `(std::string_view path) -> expected<FontId, ErrorCode>` | `[[nodiscard]]` |
      | `LoadSystemFont` | `(std::string_view familyName, FontStyle) -> expected<FontId, ErrorCode>` | `[[nodiscard]]` |
      | `GetFontMetrics` | `(FontId) const -> expected<FontMetrics, ErrorCode>` | `[[nodiscard]]` |
      | `GetGlyphMetrics` | `(FontId, uint32_t glyphIndex, float size) const -> expected<GlyphMetrics, ErrorCode>` | `[[nodiscard]]` |
      | `GetGlyphIndex` | `(FontId, uint32_t codepoint) const -> uint32_t` | `[[nodiscard]]` 0 = missing |
      | `SetFallbackChain` | `(span<const FontId>) -> void` | — |
      | `GetFallbackFont` | `(uint32_t codepoint) const -> FontId` | `[[nodiscard]]` — first font in chain containing glyph |

      **Acceptance**: compiles
      `[verify: compile]`

- [ ] **Step 2**: Implement FontManager + FreeType integration
      **Acceptance**: LoadFont succeeds for a TTF file
      `[verify: test]`

- [ ] **Step 3**: System font discovery (Windows DirectWrite)
      **Acceptance**: LoadSystemFont("Arial", Regular) succeeds on Windows
      `[verify: test]`

- [ ] **Step 4**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(FontManager, Create)` | Positive | FreeType library initializes | 2-4 |
| `TEST(FontManager, LoadFont_TTF)` | Positive | Load bundled test TTF | 2-4 |
| `TEST(FontManager, LoadFont_InvalidPath)` | Error | Returns error for missing file | 2-4 |
| `TEST(FontManager, GetFontMetrics)` | Positive | Ascender/descender non-zero | 2-4 |
| `TEST(FontManager, GetGlyphMetrics)` | Positive | 'A' glyph has positive width | 2-4 |
| `TEST(FontManager, GetGlyphIndex_Missing)` | Boundary | Missing glyph returns 0 | 2-4 |
| `TEST(FontManager, FallbackChain)` | Positive | CJK codepoint resolved via fallback | 2-4 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
