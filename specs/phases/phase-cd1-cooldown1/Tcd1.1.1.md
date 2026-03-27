# Tcd1.1.1 — Audit + Resolve TODO/FIXME/HACK in miki::rhi

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Tech Debt
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Accumulated TODO/FIXME/HACK markers in RHI layer from Phases 1a–6a.
- **Affected files**: `src/miki/rhi/vulkan/`, `src/miki/rhi/d3d12/`, `src/miki/rhi/opengl/`, `src/miki/rhi/webgpu/`, `src/miki/rhi/mock/`, `include/miki/rhi/`
- **Affected phases**: 1a, 1b, 2, 3b, 6a

### Acceptance Criteria

- [ ] `grep -r "TODO\|FIXME\|HACK" src/miki/rhi/ include/miki/rhi/` returns 0 hits
- [ ] All resolved items either fixed in-place or documented as intentional deferral with phase reference
- [ ] Build passes, all existing tests pass

## Steps

- [ ] **Step 1**: Scan all RHI source for TODO/FIXME/HACK, categorize each
      `[verify: compile]`
- [ ] **Step 2**: Fix or document each marker, add regression tests where applicable
      `[verify: test]`
