# T6b.9.3 — Visibility Persistence Tests

**Phase**: 09-6b
**Component**: Two-Phase Occlusion Culling
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (1h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.9.2 | Early+Late Task Shader | Not Started | Two-phase cull implementation |

## Context Anchor

Comprehensive test suite validating two-phase occlusion culling correctness: temporal coherence across frames, camera rotation reveals hidden geometry within 1 frame (not 2), static scene optimization (skip cull when no dirty instances).

## Steps

- [ ] **Step 1**: Two-phase occlusion culling correctness + temporal coherence tests
      `[verify: test]`
