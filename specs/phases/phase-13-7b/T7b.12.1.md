# T7b.12.1 — ColorGlyphCache (FT_LOAD_COLOR → RGBA Atlas Pages)

**Phase**: 13-7b
**Component**: Color Emoji & BiDi
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

Per roadmap: "FreeType `FT_LOAD_COLOR` + `FT_RENDER_MODE_NORMAL` → RGBA bitmap → dedicated atlas pages (true RGBA, not MSDF). Shader `isColor` flag bypasses median-SDF, samples texture directly. Covers COLR/CBDT/sbix color font formats."

- **Architecture**: Extend Phase 2 `GlyphAtlas` with RGBA pages alongside existing MSDF pages. `isColor` flag per glyph entry. Fragment shader branches: `if (isColor) return textureSample; else return msdfMedian;`.
- **Atlas management**: Separate RGBA pages (no SDF distance encoding). Page allocation follows existing LRU eviction from Phase 2.
- **Use case**: Color emoji in PMI text, RichTextInput (Phase 9).

## Steps

- [ ] **Step 1**: Extend GlyphAtlas with RGBA page support + ColorGlyphCache
      `[verify: compile]`
- [ ] **Step 2**: Modify text fragment shader for isColor branch
      `[verify: compile]`
- [ ] **Step 3**: Tests (color emoji rendering, RGBA vs MSDF path selection)
      `[verify: test]`
