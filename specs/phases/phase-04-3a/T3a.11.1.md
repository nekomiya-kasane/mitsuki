# T3a.11.1 — GlyphAtlas (Virtual Page + Shelf Packing + LRU + GPU Upload)

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 11 — TextRenderer — Glyph Atlas (deferred from Phase 2 T2.7.4)
**Roadmap Ref**: `roadmap.md` L1253-1258 — Dynamic Virtual Atlas (TextRenderer Details)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.8.1 | Font Loading | Not Started | `FontManager`, `GlyphMetrics` for sizing |
| T3a.10.1 | MSDF Generation | Not Started | `MsdfBitmap` for atlas upload |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/text/GlyphAtlas.h` | **shared** | **H** | `GlyphAtlas` — virtual page atlas with shelf packing, LRU, GPU upload |
| `src/miki/text/GlyphAtlas.cpp` | internal | L | Implementation |
| `src/miki/text/ShelfPacker.cpp` | internal | L | Shelf-first-fit packing algorithm |
| `tests/unit/test_glyph_atlas.cpp` | internal | L | Unit tests |

- **Virtual pages**: 256 glyphs per 512×512 texture page. LRU eviction when exceeding budget (default 16 pages = 8M texels).
- **Resident pages**: ASCII (U+0020-U+007E), Latin Extended, engineering symbols, GD&T symbols — always loaded.
- **On-demand pages**: CJK, Hangul, Arabic, Cyrillic — loaded per-page as accessed.
- **GPU upload**: via `StagingUploader` on first access, non-blocking.
- **Shelf packing**: rows of varying height, first-fit allocation within page.

### Downstream Consumers

- `GlyphAtlas.h` (**shared**, heat **H**):
  - T3a.12.1 (GPU Pipeline) — `GetGlyph()` for UV lookup, `GetPageTexture()` for sampling
  - T3a.13.1 (RichText) — `GetGlyph()` for layout positioning
  - T3a.14.1 (Outlines) — page management for symbol atlas

### Upstream Contracts
- T3a.8.1: `FontManager::GetGlyphMetrics()` for sizing
- T3a.10.1: `MsdfGenerator::GenerateGlyph()` / `PollCompleted()` for MSDF data
- Phase 2: `StagingUploader` for GPU upload, `IDevice::CreateTexture()`

### Technical Direction
- **Shelf-first-fit**: simple, cache-friendly, ~85% utilization for mixed glyph sizes
- **LRU eviction**: per-page LRU. When budget exceeded, evict least-recently-used on-demand page.
- **Tier-adaptive budget**: Tier1/2: 16 pages (default). Tier3 (WebGPU): 8 pages. Tier4 (GL): 12 pages.
- **Fallback**: if virtual paging unavailable, multi-page atlas (4×2K×2K pre-allocated)
- **GPU upload**: `StagingUploader::UploadTexture()` for each dirty page region
- **Thread safety (consumer side)**: `FlushUploads()` is called on the **render thread only**. It drains completed `MsdfResult`s from `MsdfGenerator::PollCompleted()` (lock-free SPSC ring buffer, see T3a.10.1). No mutex needed on the hot path. `RequestGlyph()` enqueues into the `AsyncMsdfBatch` request queue (mutex-protected, but off the render hot path). The invariant: `FlushUploads()` and `RequestGlyph()` may be called from the same thread; `PollCompleted()` is the only cross-thread boundary.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/text/GlyphAtlas.h` | **shared** | **H** | Atlas API |
| Create | `src/miki/text/GlyphAtlas.cpp` | internal | L | Implementation |
| Create | `src/miki/text/ShelfPacker.cpp` | internal | L | Packing algorithm |
| Create | `tests/unit/test_glyph_atlas.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define GlyphAtlas.h (shared H)
      **Signatures**:

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `GlyphEntry` | `{ uint32_t pageIndex; float u0, v0, u1, v1; float width, height; float bearingX, bearingY; }` | — |
      | `AtlasConfig` | `{ uint32_t pageSize = 512; uint32_t maxPages = 16; uint32_t glyphsPerPage = 256; }` | — |
      | `GlyphAtlas::Create` | `(IDevice&, FontManager&, MsdfGenerator&, AtlasConfig) -> expected<GlyphAtlas, ErrorCode>` | `[[nodiscard]]` static |
      | `GetGlyph` | `(FontId, uint32_t codepoint) -> GlyphEntry const*` | `[[nodiscard]]` nullptr if not yet loaded |
      | `RequestGlyph` | `(FontId, uint32_t codepoint) -> void` | trigger async MSDF + upload |
      | `GetPageTexture` | `(uint32_t pageIndex) const -> TextureHandle` | `[[nodiscard]]` |
      | `GetPageCount` | `() const -> uint32_t` | `[[nodiscard]]` |
      | `FlushUploads` | `(ICommandBuffer&) -> void` | upload dirty regions |
      | `GetOccupancy` | `() const -> float` | `[[nodiscard]]` 0.0–1.0 |

      **Acceptance**: compiles
      `[verify: compile]`

- [ ] **Step 2**: Implement ShelfPacker
      **Acceptance**: packing 100 varied-size glyphs achieves >80% utilization
      `[verify: test]`

- [ ] **Step 3**: Implement GlyphAtlas (page management + LRU + GPU upload)
      **Acceptance**: ASCII glyphs loaded into atlas, GPU texture valid
      `[verify: test]`

- [ ] **Step 4**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(GlyphAtlas, Create)` | Positive | Atlas creates with default config | 3-4 |
| `TEST(GlyphAtlas, GetGlyph_ASCII)` | Positive | 'A' returns valid entry after request+flush | 3-4 |
| `TEST(GlyphAtlas, GetGlyph_NotLoaded)` | Boundary | Unrequested glyph returns nullptr | 1-3 |
| `TEST(GlyphAtlas, RequestAndFlush)` | Positive | Request → MSDF gen → upload → entry valid | 3-4 |
| `TEST(GlyphAtlas, PageTexture_Valid)` | Positive | Page texture handle is valid | 3-4 |
| `TEST(ShelfPacker, PackVariedSizes)` | Positive | 100 glyphs packed, >80% utilization | 2 |
| `TEST(GlyphAtlas, LRU_Eviction)` | Positive | Exceeding budget evicts LRU page | 3-4 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
