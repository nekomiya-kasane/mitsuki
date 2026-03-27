# T3a.15.1 — text_demo (Multi-Script, MSDF vs Bitmap, 5 Backends) + Golden Image CI

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 15 — TextRenderer Demo (deferred from Phase 2 T2.8.2)
**Roadmap Ref**: `roadmap.md` L1240 — text_demo
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.13.1 | RichText Layout | Not Started | `RichTextLayout::Layout()` |
| T3a.14.1 | Outlines & Symbols | Not Started | `SymbolAtlas`, engineering symbols |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/text_demo/main.cpp` | internal | L | Demo entry point |
| `demos/text_demo/CMakeLists.txt` | internal | L | Build target |
| `tests/integration/test_text_demo.cpp` | internal | L | 5-backend integration tests |

- **Scene**: ASCII + CJK + engineering symbols rendered at multiple sizes (8px–72px), world-space billboarded labels, MSDF vs bitmap fallback with [8,16]px crossfade band
- **Render pipeline**: Text rendering via `TextRenderer` + `RichTextLayout`, optional ImGui overlay
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`, `--offscreen`
- **Golden image**: cross-backend diff < 5%

### Downstream Consumers

- None (leaf demo). Pattern reused by Phase 7b for PMI text rendering.

### Upstream Contracts
- T3a.13.1: `RichTextLayout::Layout()` for styled text
- T3a.14.1: `SymbolAtlas::GetSymbol()` for engineering symbols
- T3a.12.1: `TextRenderer::Draw()`, `Flush()`
- Phase 2: `ImGuiBackend`, `ISwapchain`, `OrbitCamera`

### Technical Direction
- **Multi-size showcase**: render same text at 8, 12, 16, 24, 36, 48, 72px to demonstrate MSDF quality + bitmap fallback
- **Multi-script**: English, Chinese (CJK), Arabic (RTL), Japanese (mixed horizontal/vertical)
- **Engineering symbols**: GD&T symbols from `SymbolAtlas`
- **Crossfade visualization**: optional debug mode showing which path (MSDF vs bitmap) is active per glyph
- **World-space labels**: 3D-positioned text that billboards toward camera

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `demos/text_demo/main.cpp` | internal | L | Demo |
| Create | `demos/text_demo/CMakeLists.txt` | internal | L | Build |
| Modify | `demos/CMakeLists.txt` | internal | L | Add subdirectory |
| Create | `tests/integration/test_text_demo.cpp` | internal | L | Integration tests |
| Modify | `.github/workflows/ci.yml` | internal | L | CI matrix |

## Steps

- [ ] **Step 1**: Demo skeleton + CLI + text scene setup
      **Acceptance**: compiles, window opens
      `[verify: compile]`

- [ ] **Step 2**: Multi-size + multi-script text rendering
      **Acceptance**: ASCII + CJK + Arabic visible at multiple sizes
      `[verify: visual]`

- [ ] **Step 3**: Engineering symbols + world-space labels
      **Acceptance**: GD&T symbols and 3D labels render
      `[verify: visual]`

- [ ] **Step 4**: Integration tests (5 backends + golden image)
      **Acceptance**: all tests pass or skip appropriately
      `[verify: test]`

- [ ] **Step 5**: CI matrix update
      **Acceptance**: CI jobs added
      `[verify: compile]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(TextDemo, RenderVulkan)` | Integration | Vulkan renders non-black | 4 |
| `TEST(TextDemo, RenderCompat)` | Integration | Compat renders non-black | 4 |
| `TEST(TextDemo, RenderD3D12)` | Integration | D3D12 renders non-black | 4 |
| `TEST(TextDemo, RenderGL)` | Integration | GL renders non-black | 4 |
| `TEST(TextDemo, RenderWebGPU)` | Integration | WebGPU renders non-black | 4 |
| `TEST(TextDemo, GoldenImageParity)` | Visual | Cross-backend PSNR > 30 dB vs Vulkan Tier1 reference; RMSE < 0.02 normalized | 4 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
