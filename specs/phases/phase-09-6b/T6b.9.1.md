# T6b.9.1 — Two-Phase Occlusion Culling Types

**Phase**: 09-6b
**Component**: Two-Phase Occlusion Culling
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/VisibilityPersistence.h` | **public** | **M** | `VisibilityPersistence::Create()`, `GetBuffer()`, `Swap()` |

- **VisibilityPersistenceBuffer**: Per-meshlet uint8 visibility bit (0=not visible last frame, 1=visible). Double-buffered: current frame writes, previous frame reads.
- **Purpose**: Early pass renders meshlets that were visible last frame (high confidence → minimal overdraw). Late pass tests remaining meshlets against current-frame HiZ.
- Per niagara research: `drawVisibility` + `meshletVisibility` buffers track per-draw and per-meshlet visibility across frames.
- Per arch spec §5.3: "Two-phase hierarchical culling" with "static frames: skip cull entirely (dirty flag optimization)".

### Downstream Consumers

- T6b.9.2 (Early+Late Task Shader): reads/writes visibility persistence buffer.
- Phase 14: extends with visibility prediction heuristics.

## Steps

- [ ] **Step 1**: Define VisibilityPersistence.h + double-buffered GPU buffer
      `[verify: compile]`
- [ ] **Step 2**: Tests (create, swap, buffer validity)
      `[verify: test]`
