# T7b.4.2 — PMI Render Pipeline (Instanced Quads + MSDF Text + Leader Lines + Arrows)

**Phase**: 13-7b
**Component**: GPU PMI & Annotation
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.4.1 | PMI Entity Types | Not Started |

## Scope

Per arch spec §3 Pass #45: "PMI Render — Graphics (instanced) — PmiAnnotation[], MSDF atlas, LeaderLine SSBO — Text+leaders+symbols — <0.1ms @1K".

Reuses Phase 2 `TextRenderer` for all text. 3D dimension lines + arrow heads (compute-generated geometry). GD&T symbols from engineering MSDF atlas. Billboarding option. World-space mm or screen-space px sizing. Per-view PMI visibility toggle.

## Steps

- [ ] **Step 1**: Implement PMI render pipeline (instanced quads + leaders + arrows)
      `[verify: compile]`
- [ ] **Step 2**: Integrate GD&T symbol rendering from MSDF atlas
      `[verify: compile]`
- [ ] **Step 3**: Tests (GD&T rendering, datum placement, leader line accuracy)
      `[verify: test]`
