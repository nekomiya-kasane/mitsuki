# Tcd1.3.6 — Forward Compatibility Audit

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: API Audit
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Scope

- **Problem**: Before freezing APIs, verify that ALL remaining phases (7a-1 through 15b) can be implemented using the frozen API surface. Identify any breaking changes needed BEFORE the freeze.
- **Affected files**: `specs/roadmap.md` (read all remaining phases), all frozen public headers

### Acceptance Criteria

- [ ] Every downstream phase's Component table scanned for API dependencies on frozen headers
- [ ] No downstream phase requires a breaking change to any frozen header
- [ ] Any API gaps identified and resolved (either add missing API before freeze, or document workaround)
- [ ] Report: per-header consumer count across remaining phases

## Steps

- [ ] **Step 1**: Read all remaining phase Component tables from roadmap.md
      `[verify: compile]`
- [ ] **Step 2**: Cross-reference with frozen API surface, identify gaps
      `[verify: compile]`
- [ ] **Step 3**: Fix any API gaps found, document in Implementation Notes
      `[verify: test]`
