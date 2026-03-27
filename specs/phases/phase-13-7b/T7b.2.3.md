# T7b.2.3 — Boolean Preview Tests (Union/Subtract/Intersect Correctness)

**Phase**: 13-7b
**Component**: GPU Boolean Preview
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.2.2 | CSG Resolve Compute | Not Started |

## Scope

Tests: sphere-sphere union (merged volume), sphere-sphere subtract (cavity), sphere-sphere intersect (lens), box-cylinder subtract (hole), edge highlight at boolean boundary, depth layer correctness (N=8 sufficient for test cases).

## Steps

- [ ] **Step 1**: Write boolean preview correctness tests
      `[verify: test]`
