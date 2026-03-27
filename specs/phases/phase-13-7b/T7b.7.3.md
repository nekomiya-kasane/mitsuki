# T7b.7.3 — MeshImporter (STL/OBJ/PLY Trivial Parsers + Auto-Meshlet)

**Phase**: 13-7b
**Component**: Import Pipeline
**Status**: Not Started
**Effort**: L (< 1h)

## Dependencies

None (L0 task).

## Scope

Trivial mesh format parsers: STL (binary + ASCII), OBJ (vertex + normal + UV + face), PLY (binary + ASCII, vertex + face). Auto-meshlet via `MeshletGenerator::Build()`. No material import for STL/PLY (default material). OBJ material via .mtl file → MaterialParameterBlock mapping.

## Steps

- [ ] **Step 1**: Implement STL/OBJ/PLY parsers + auto-meshlet
      `[verify: compile]`
- [ ] **Step 2**: Tests (format correctness, vertex count, meshlet generation)
      `[verify: test]`
