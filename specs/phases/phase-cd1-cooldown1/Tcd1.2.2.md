# Tcd1.2.2 — RenderGraph Test Gap-Fill

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Test Gap
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Missing tests for barrier correctness, transient aliasing edge cases, conditional pass enable/disable, cache invalidation.
- **Affected files**: `tests/unit/test_render_graph_builder.cpp`, `test_render_graph_compiler.cpp`, `test_render_graph_executor.cpp`, `test_render_graph_cache.cpp`
- **Affected phases**: 3a

### Acceptance Criteria

- [ ] ≥80% line coverage on rendergraph source
- [ ] Barrier correctness tests: compute→graphics, graphics→compute, read-after-write, write-after-write
- [ ] Conditional pass tests: disabled pass produces no barriers, no allocations
- [ ] All new tests pass

## Steps

- [ ] **Step 1**: Coverage audit + write missing rendergraph tests (+10 tests)
      `[verify: test]`
