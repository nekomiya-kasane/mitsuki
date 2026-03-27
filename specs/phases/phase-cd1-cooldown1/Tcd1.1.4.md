# Tcd1.1.4 — Audit + Resolve TODO/FIXME/HACK in miki::shader + miki::resource + miki::scene

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Tech Debt
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Accumulated markers in shader, resource, and scene modules from Phases 1a–5.
- **Affected files**: `src/miki/shader/`, `src/miki/resource/`, `src/miki/scene/`, corresponding include dirs

### Acceptance Criteria

- [ ] 0 TODO/FIXME/HACK hits in shader + resource + scene
- [ ] Build passes, all existing tests pass

## Steps

- [ ] **Step 1**: Scan + fix/document all markers
      `[verify: test]`
