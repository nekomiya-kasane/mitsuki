# Tcd1.5.2 — Doxygen Stubs for Frozen Public Headers

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Docs
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Frozen public headers need complete Doxygen documentation for SDK consumers.
- **Affected files**: All frozen headers in `include/miki/rhi/`, `include/miki/vgeo/`, `include/miki/rendergraph/`, `include/miki/render/`

### Acceptance Criteria

- [ ] Every frozen public header has `@brief` on every class/struct/enum
- [ ] Every public method has `@param` and `@return` documentation
- [ ] Doxygen build produces zero warnings on frozen headers
- [ ] `doxygen Doxyfile` runs without errors (Doxyfile created if not existing)

## Steps

- [ ] **Step 1**: Add Doxygen stubs to all frozen public headers
      `[verify: compile]`
- [ ] **Step 2**: Create/update Doxyfile, verify zero-warning build
      `[verify: manual]`
