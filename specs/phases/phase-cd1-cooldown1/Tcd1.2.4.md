# Tcd1.2.4 — Integration Test Sweep

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Test Gap
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Cross-module smoke tests for all demos may have gaps — ensure every demo path exercises the full pipeline without crash or validation error.
- **Affected files**: `tests/integration/`

### Acceptance Criteria

- [ ] Every demo (triangle, forward_cubes, deferred_pbr, bindless_scene, gpu_driven_basic, virtual_geometry) has at least 1 headless integration test
- [ ] All integration tests pass with 0 Vulkan validation errors
- [ ] All new tests pass

## Steps

- [ ] **Step 1**: Audit existing integration tests, add missing ones
      `[verify: test]`
