# Tcd1.1.3 — Audit + Resolve TODO/FIXME/HACK in miki::render + miki::rendergraph

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Tech Debt
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Accumulated markers in render and rendergraph modules from Phases 2–3b.
- **Affected files**: `src/miki/render/`, `src/miki/rendergraph/`, `include/miki/render/`, `include/miki/rendergraph/`

### Acceptance Criteria

- [ ] 0 TODO/FIXME/HACK hits in render + rendergraph source and headers
- [ ] Build passes, all existing tests pass

## Steps

- [ ] **Step 1**: Scan + fix/document all markers
      `[verify: test]`
