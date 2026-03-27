# T3a.12.1 — GPU Text Rendering Pipeline (Instanced Quad + MSDF Fragment)

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 12 — TextRenderer — GPU Pipeline (deferred from Phase 2 T2.7.5)
**Roadmap Ref**: `roadmap.md` L1261 — GPU rendering pipeline (TextRenderer Details)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.9.1 | Text Shaping | Not Started | `GlyphRun`, `ShapedGlyph` for quad generation |
| T3a.11.1 | Glyph Atlas | Not Started | `GlyphAtlas`, `GlyphEntry`, page textures for sampling |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/text/TextRenderer.h` | **public** | **H** | `TextRenderer` — GPU text rendering, instanced quads, MSDF sampling |
| `include/miki/text/TextDrawCmd.h` | **public** | **H** | `TextDrawCmd` — draw command struct |
| `shaders/text/text_msdf.slang` | internal | L | Instanced quad vertex + MSDF fragment |
| `src/miki/text/TextRenderer.cpp` | internal | L | Implementation |
| `tests/unit/test_text_renderer.cpp` | internal | L | Unit tests |

- **Instanced quads**: one quad per glyph, positioned from `GlyphRun` data
- **MSDF sampling**: `median(r,g,b)` → distance → `smoothstep` for anti-aliased coverage
- **Screen-space text**: pixel-snapped positions (no sub-pixel jitter)
- **World-space text**: billboarded or plane-anchored, mm/px dual sizing
- **screenPxRange clamp**: `max(screenPxRange(), 1.0)` prevents salt-and-pepper noise; alpha fades when `screenPxRange < 1.0`
- **Frustum culling**: world-space labels culled before shaping
- **RenderGraph integration strategy**: In Phase 3a, `TextRenderer::Flush()` writes directly to `ICommandBuffer` (not registered as a render graph pass). This is acceptable because text is rendered **after** tone mapping as a UI overlay, outside the HDR pipeline. However, Phase 3b TAA requires knowing which pixels are UI text (reactive mask). To prepare: (a) `TextRenderer` header declares a `RegisterAsPass(RenderGraphBuilder&) -> RGHandle` method signature (stub, returns invalid handle in Phase 3a); (b) Phase 3b activates this method to register text as a graph node and auto-generate a reactive mask output. This avoids refactoring `Flush()` in Phase 3b while keeping the Phase 3a implementation simple.

### Downstream Consumers

- `TextRenderer.h` (**public**, heat **H**):
  - T3a.13.1 (RichText) — `Draw()` for rich text spans
  - T3a.15.1 (text_demo) — demo usage
  - Phase 5: debug/entity labels
  - Phase 7b: quality upgrade path selection
  - Phase 9: RichTextInput editor

### Upstream Contracts
- T3a.9.1: `TextShaper::ShapeText()` → `GlyphRun`
- T3a.11.1: `GlyphAtlas::GetGlyph()` → `GlyphEntry`, `GetPageTexture()`, `FlushUploads()`
- Phase 2: `IDevice`, `ICommandBuffer`, `GraphicsPipelineDesc`, `DescriptorSet`, `StagingUploader`

### Technical Direction
- **Compute shader layout** (optional): `TextDrawCmd` → instanced quad buffer `{pos, uv, color}` per glyph via compute. For v1, CPU-side quad generation is acceptable.
- **Fragment shader**: sample MSDF atlas, `float sd = median(msd.r, msd.g, msd.b); float alpha = smoothstep(0.5 - pw, 0.5 + pw, sd);` where `pw` = 0.5/screenPxRange
- **Batching**: group draws by atlas page to minimize texture binds
- **Descriptor set strategy**: one `DescriptorSet` per atlas page texture. On `Flush()`, group glyphs by page, bind the corresponding descriptor set, then issue one instanced draw per page batch. This avoids per-glyph descriptor updates. Phase 4 `BindlessTable` will replace this with a single bindless descriptor set (texture array index passed via instance data), but Phase 3a uses the Phase 2 traditional `DescriptorSet` path. Maximum active page count bounded by atlas budget (default 16 pages = 16 descriptor sets).
- **Performance target**: <0.1ms for 1000 glyphs, <0.5ms for 10K glyphs

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/text/TextRenderer.h` | **public** | **H** | Renderer API |
| Create | `include/miki/text/TextDrawCmd.h` | **public** | **H** | Draw command |
| Create | `shaders/text/text_msdf.slang` | internal | L | MSDF shader |
| Create | `src/miki/text/TextRenderer.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_text_renderer.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define TextDrawCmd.h + TextRenderer.h (public H)
      **Signatures** (`TextDrawCmd.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `TextDrawCmd` | `{ std::string_view text; float3 position; FontId fontId; float size; float4 color; uint32_t flags; }` | — |
      | `TextFlags` | `enum class : uint32_t { ScreenSpace = 0, WorldSpace = 1<<0, Billboard = 1<<1 }` | — |

      **Signatures** (`TextRenderer.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `TextRenderer::Create` | `(IDevice&, TextShaper&, GlyphAtlas&) -> expected<TextRenderer, ErrorCode>` | `[[nodiscard]]` static |
      | `Draw` | `(TextDrawCmd const&) -> void` | queue draw for current frame |
      | `Flush` | `(ICommandBuffer&, mat4 viewProj, float2 screenSize) -> void` | emit all queued draws |
      | `RegisterAsPass` | `(RenderGraphBuilder&) -> RGHandle` | stub in Phase 3a (returns invalid handle); activated in Phase 3b for TAA reactive mask |
      | `GetGlyphCount` | `() const -> uint32_t` | `[[nodiscard]]` — glyphs queued this frame |

      **Acceptance**: compiles
      `[verify: compile]`

- [ ] **Step 2**: MSDF Slang shader (instanced quad vertex + MSDF fragment)
      **Acceptance**: shader compiles
      `[verify: compile]`

- [ ] **Step 3**: Implement TextRenderer (quad generation, batching, GPU draw)
      **Acceptance**: "Hello" renders on screen via Vulkan
      `[verify: visual]`

- [ ] **Step 4**: World-space text + billboard + frustum culling
      **Acceptance**: world-space labels visible and billboard correctly
      `[verify: visual]`

- [ ] **Step 5**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(TextRenderer, Create)` | Positive | Renderer creates | 3-5 |
| `TEST(TextRenderer, DrawScreenSpace)` | Positive | Screen-space text visible | 3-5 |
| `TEST(TextRenderer, DrawWorldSpace)` | Positive | World-space text at position | 4-5 |
| `TEST(TextRenderer, Billboard)` | Positive | Billboard faces camera | 4-5 |
| `TEST(TextRenderer, FlushEmitsDraws)` | Positive | Flush produces GPU commands | 3-5 |
| `TEST(TextRenderer, GlyphCount)` | Positive | "Hello" → 5 glyphs queued | 3-5 |
| `TEST(TextRenderer, BatchByPage)` | Positive | Glyphs on same page batched | 3-5 |
| `TEST(TextRenderer, FrustumCull_Offscreen)` | Boundary | Off-screen world text not drawn | 4-5 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
