# T6b.11.1 — virtual_geometry Demo (100M Tri, Seamless LOD, Streaming)

**Phase**: 09-6b
**Component**: Demo + Integration
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.6.4 | Progressive Rendering | Not Started | Streaming-aware render path |
| T6b.2.2 | GPU DAG Cut Optimizer | Not Started | Per-frame LOD selection |
| T6b.4.2 | Mesh Shader Decoder | Not Started | Compressed meshlet rendering |
| T6b.9.2 | Two-Phase Occlusion | Not Started | Early+late cull |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/virtual_geometry/main.cpp` | internal | L | Full virtual geometry demo |
| `demos/virtual_geometry/CMakeLists.txt` | internal | L | Build target |
| `tests/integration/test_virtual_geometry.cpp` | internal | L | Integration tests |

- **Scene**: 1000 instances of Dragon mesh (~100K tri each) = 100M total triangles. Each instance → ClusterDAG built at startup. Compressed meshlets. Streamed from `.miki` archive.
- **Render loop**: SceneBuffer upload → HiZ (prev frame) → Early cull → HiZ rebuild → Late cull → DAG cut → MacroBin → Geometry (3× IndirectCount) → Material Resolve → Deferred Lighting → Post-Process → Present.
- **ImGui overlay**: LOD level histogram, visible meshlet/triangle count, streaming bandwidth, compression ratio, two-phase cull stats (early vs late survivors), VRAM usage.
- **CLI**: `--backend vulkan --instances 1000 --mesh dragon.obj --archive dragon.miki`

### Acceptance Criteria

- [ ] 100M triangles >= 60fps on RTX 4070
- [ ] Seamless LOD transitions (no visual popping)
- [ ] Progressive rendering: coarse LOD visible < 100ms
- [ ] Streaming: >2GB/s sustained throughput
- [ ] Compression ratio >= 50%
- [ ] Two-phase occlusion: zero 1-frame temporal lag

## Steps

- [ ] **Step 1**: Create CMakeLists + demo skeleton
      `[verify: compile]`
- [ ] **Step 2**: Scene generation (ClusterDAG build, archive write, meshlet compression)
      `[verify: compile]`
- [ ] **Step 3**: Render graph assembly (two-phase cull, DAG cut, geometry, resolve)
      `[verify: compile]`
- [ ] **Step 4**: ImGui overlay + streaming stats
      `[verify: compile]`
- [ ] **Step 5**: Integration tests
      `[verify: test]`
- [ ] **Step 6**: Visual + performance verification
      `[verify: visual]` `[verify: manual]`
