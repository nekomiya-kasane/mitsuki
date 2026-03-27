# T7b.7.5 — Import Tests (STEP Round-Trip, JT Structure/LOD, glTF Integrity, No-Kernel .miki Load)

**Phase**: 13-7b
**Component**: Import Pipeline
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.7.4 | ParallelTessellator | Not Started |

## Scope

Tests: STEP import via IKernel round-trip (body count, face count, PMI count), JT assembly structure + LOD levels, glTF PBR material integrity, STL/OBJ/PLY vertex count, no-kernel mode (load .miki archive without kernel linked), ParallelTessellator thread safety + speedup benchmark.

## Steps

- [ ] **Step 1**: Write import pipeline correctness + round-trip tests
      `[verify: test]`
