# T6b.9.2 — Early+Late Task Shader (Two-Phase Occlusion Culling)

**Phase**: 09-6b
**Component**: Two-Phase Occlusion Culling
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.9.1 | Visibility Persistence Types | Not Started | `VisibilityPersistence::GetBuffer()` |

## Context Anchor

### This Task's Contract

**Produces**: Modified task_cull.slang split into early and late passes. Modified render graph to insert HiZ rebuild between passes.

- **Early pass**: Render meshlets where `visibilityBuffer[meshletIdx] == 1` (visible last frame). Use previous-frame HiZ for occlusion. Builds current-frame depth.
- **HiZ rebuild**: After early pass depth is complete, rebuild HiZ pyramid from current-frame depth.
- **Late pass**: Test remaining meshlets (visibility == 0) against current-frame HiZ. Render survivors. Update visibility buffer.
- **Benefit**: Eliminates 1-frame temporal lag where newly-visible objects are missed. Camera rotation reveals geometry immediately instead of 1 frame late.
- Per niagara: early pass uses `LATE=false`, late pass uses `LATE=true` with depth pyramid check.

### Downstream Consumers

- T6b.11.1 (Demo): two-phase cull integrated into render graph.

## Steps

- [ ] **Step 1**: Split task_cull.slang into early/late variants with visibility persistence read/write
      `[verify: compile]`
- [ ] **Step 2**: Modify render graph to insert HiZ rebuild between early and late passes
      `[verify: compile]`
- [ ] **Step 3**: Tests (temporal coherence, camera rotation reveals hidden geometry within 1 frame)
      `[verify: test]`
