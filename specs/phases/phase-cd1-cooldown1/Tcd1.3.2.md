# Tcd1.3.2 — API Audit: ICommandBuffer.h

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: API Audit
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: ICommandBuffer.h signature consistency, error model review, mesh shader dispatch methods.
- **Affected files**: `include/miki/rhi/ICommandBuffer.h`

### Acceptance Criteria

- [ ] All virtual methods have consistent parameter naming (i-prefix convention)
- [ ] Mesh shader dispatch methods (DrawMeshTasks*) have complete Doxygen
- [ ] DispatchIndirect documented with argument buffer layout
- [ ] No breaking changes needed

## Steps

- [ ] **Step 1**: Audit ICommandBuffer.h, fix all issues
      `[verify: compile]`
