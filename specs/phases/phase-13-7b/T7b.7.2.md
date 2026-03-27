# T7b.7.2 — JtImporter (Siemens JT Format: Tessellated LOD + PMI + Assembly)

**Phase**: 13-7b
**Component**: Import Pipeline
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

None (L0 task).

## Scope

Kernel-independent JT format import: tessellated LOD levels + PMI annotations + assembly structure. No B-Rep kernel required (JT stores pre-tessellated geometry). LOD hierarchy maps to ClusterDAG levels. Assembly tree → ECS entity hierarchy. PMI from JT → PmiAnnotation types (T7b.4.1).

## Steps

- [ ] **Step 1**: Implement JT file parser (header + segment table + mesh data)
      `[verify: compile]`
- [ ] **Step 2**: Implement LOD extraction + assembly tree mapping
      `[verify: compile]`
- [ ] **Step 3**: Tests (JT structure correctness, LOD level count, PMI extraction)
      `[verify: test]`
