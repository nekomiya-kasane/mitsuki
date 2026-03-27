# Tcd1.3.3 — API Audit: RenderGraphBuilder.h

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: API Audit
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: RenderGraphBuilder.h builder pattern consistency, resource declaration API completeness.
- **Affected files**: `include/miki/rendergraph/RenderGraphBuilder.h`, `RenderGraphCompiler.h`, `RenderGraphExecutor.h`, `RenderGraphCache.h`

### Acceptance Criteria

- [ ] Builder pattern methods return `*this` consistently
- [ ] Complete Doxygen on all public methods
- [ ] Resource declaration types cover all Phase 6b needs (compressed meshlet buffers, streaming pages)
- [ ] No breaking changes needed

## Steps

- [ ] **Step 1**: Audit rendergraph public headers, fix all issues
      `[verify: compile]`
