# T7b.6.4 — Parametric Tessellation Tests (vs IKernel::Tessellate CPU Reference)

**Phase**: 13-7b
**Component**: Parametric Tessellation
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.6.3 | Index Buffer Generation | Not Started |

## Scope

Validation: GPU tessellation vs `IKernel::Tessellate` CPU reference. Max positional deviation < 0.01mm. Normal deviation < 1°. Triangle count within 2× of CPU result. Performance: ≥10× faster than single-thread CPU. Watertight mesh (no cracks at adaptive boundaries).

## Steps

- [ ] **Step 1**: Write parametric tessellation accuracy + performance tests
      `[verify: test]`
