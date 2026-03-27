# T7b.7.1 — GltfPipeline (cgltf Parsing, Auto-Meshlet, Material Import)

**Phase**: 13-7b
**Component**: Import Pipeline
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

Kernel-independent glTF import: cgltf parsing → vertex/index extraction → auto-meshlet via `MeshletGenerator::Build()` → material mapping to `MaterialParameterBlock`. Supports glTF 2.0 (binary .glb + text .gltf), PBR metallic-roughness, normal/occlusion/emissive maps. Node hierarchy → ECS entity tree.

## Steps

- [ ] **Step 1**: Implement GltfPipeline (cgltf parse → mesh extraction → meshlet)
      `[verify: compile]`
- [ ] **Step 2**: Implement material mapping (glTF PBR → MaterialParameterBlock)
      `[verify: compile]`
- [ ] **Step 3**: Tests (glTF integrity, material round-trip, meshlet generation)
      `[verify: test]`
