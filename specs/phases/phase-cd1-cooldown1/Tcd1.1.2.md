# Tcd1.1.2 — Audit + Resolve TODO/FIXME/HACK in miki::vgeo

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Tech Debt
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Accumulated TODO/FIXME/HACK markers in vgeo layer from Phases 6a–6b.
- **Affected files**: `src/miki/vgeo/`, `include/miki/vgeo/`, `shaders/vgeo/`
- **Affected phases**: 6a, 6b

### Acceptance Criteria

- [ ] `grep -r "TODO\|FIXME\|HACK" src/miki/vgeo/ include/miki/vgeo/ shaders/vgeo/` returns 0 hits
- [ ] Build passes, all existing tests pass

## Steps

- [ ] **Step 1**: Scan + categorize + fix/document all markers
      `[verify: test]`
