# T3a.10.1 — MSDF Glyph Generation (msdfgen Wrapper + AsyncBatch)

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 10 — TextRenderer — MSDF Generation (deferred from Phase 2 T2.7.3)
**Roadmap Ref**: `roadmap.md` L1251 — MSDF pipeline (TextRenderer Details)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.8.1 | Font Loading | Not Started | `FontManager`, FreeType face for glyph outlines |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/text/MsdfGenerator.h` | **shared** | **M** | `MsdfGenerator` — 4-channel MSDF glyph bitmap generation |
| `src/miki/text/MsdfGenerator.cpp` | internal | L | msdfgen integration |
| `src/miki/text/AsyncMsdfBatch.cpp` | internal | L | Background `std::jthread` batch generation |
| `tests/unit/test_msdf_generator.cpp` | internal | L | Unit tests |

- **4-channel MSDF**: Multi-channel SDF preserves sharp corners (superior to single-channel SDF)
- **Glyph size**: 32×32 px per em (configurable)
- **Generation**: ~0.5ms/glyph CPU
- **AsyncMsdfBatch**: background thread generates MSDF glyphs; pending glyphs show placeholder (50% alpha solid quad)
- **External dep**: msdfgen library (MIT license, CMake `find_package(msdfgen)`)

### Downstream Consumers

- `MsdfGenerator.h` (**shared**, heat **M**):
  - T3a.11.1 (Atlas) — generated MSDF bitmaps uploaded to atlas pages

### Upstream Contracts
- T3a.8.1: `FontManager` — FreeType face access for glyph outline extraction
- External: msdfgen library

### Technical Direction
- **msdfgen API**: `msdfgen::generateMTSDF()` for 4-channel (MTSDF) or `generateMSDF()` for 3-channel
- **Thread safety**: `AsyncMsdfBatch` uses `std::jthread` + `std::stop_token` for clean shutdown
- **Batch queue**: `std::mutex`-protected queue of glyph requests; batch processes up to 64 glyphs per wake-up
- **Producer-consumer boundary with GlyphAtlas (T3a.11.1)**: `AsyncMsdfBatch` is the **producer** (background thread generates `MsdfBitmap`s). `GlyphAtlas` is the **consumer** (render thread uploads to GPU). The handoff uses `PollCompleted() -> span<const MsdfResult>` which returns completed results from a **lock-free SPSC ring buffer** (single-producer = jthread, single-consumer = render thread calling `GlyphAtlas::FlushUploads`). No mutex contention on the hot path. The ring buffer size is 256 entries (sufficient for one frame's worth of new glyphs). If the ring is full, the producer blocks (backpressure). This contract must be respected by T3a.11.1's `FlushUploads()` implementation.
- **Quality tuning**: edge coloring via `msdfgen::edgeColoringByDistance()`, range = 4.0 px

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/text/MsdfGenerator.h` | **shared** | **M** | MSDF API |
| Create | `src/miki/text/MsdfGenerator.cpp` | internal | L | msdfgen impl |
| Create | `src/miki/text/AsyncMsdfBatch.cpp` | internal | L | Background thread |
| Create | `tests/unit/test_msdf_generator.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define MsdfGenerator.h (shared M)
      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `MsdfBitmap` | `{ std::vector<uint8_t> data; uint32_t width, height, channels; }` | 4 channels (RGBA) |
      | `MsdfConfig` | `{ uint32_t glyphSize = 32; float range = 4.0f; }` | — |
      | `MsdfGenerator::Create` | `(FontManager&) -> expected<MsdfGenerator, ErrorCode>` | `[[nodiscard]]` static |
      | `GenerateGlyph` | `(FontId, uint32_t glyphIndex, MsdfConfig) -> expected<MsdfBitmap, ErrorCode>` | `[[nodiscard]]` |
      | `GenerateBatchAsync` | `(span<const GlyphRequest> requests) -> void` | non-blocking, results via callback |
      | `PollCompleted` | `() -> span<const MsdfResult>` | `[[nodiscard]]` — poll completed async results |
      | `IsIdle` | `() const -> bool` | `[[nodiscard]]` |

      **Acceptance**: compiles
      `[verify: compile]`

- [ ] **Step 2**: Implement synchronous GenerateGlyph via msdfgen
      **Acceptance**: generates valid MSDF bitmap for 'A'
      `[verify: test]`

- [ ] **Step 3**: Implement AsyncMsdfBatch (jthread + queue)
      **Acceptance**: async batch completes without deadlock
      `[verify: test]`

- [ ] **Step 4**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(MsdfGenerator, Create)` | Positive | Generator creates | 2-4 |
| `TEST(MsdfGenerator, GenerateGlyph_A)` | Positive | 'A' produces 32×32×4 bitmap | 2-4 |
| `TEST(MsdfGenerator, GenerateGlyph_NonZero)` | Positive | Bitmap has non-zero pixels | 2-4 |
| `TEST(MsdfGenerator, ConfigurableSize)` | Positive | 64×64 config produces 64×64 | 2-4 |
| `TEST(MsdfGenerator, AsyncBatch)` | Positive | 10 glyphs generated async | 3-4 |
| `TEST(MsdfGenerator, AsyncBatch_Shutdown)` | Boundary | Clean shutdown with pending work | 3-4 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
