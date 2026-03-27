# T6b.2.1 — LOD Selector Types + Projected Sphere Error Metric

**Phase**: 09-6b
**Component**: LOD Selector + DAG Cut Optimizer
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.1.1 | ClusterDAG Types | Not Started | `ClusterNode`, `ClusterDAG` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/LodSelector.h` | **public** | **M** | `ProjectedSphereError()`, `IsCorrectLOD()`, `LodSelectionConfig` |

- **Projected sphere error**: `error = meshletError * screenHeight / (2 * tan(fov/2) * distance)`. Returns pixels of screen-space error.
- **IsCorrectLOD**: `parentProjectedError > threshold AND myProjectedError <= threshold` — render this meshlet. Both C++ (CPU debug) and Slang (GPU compute) versions.
- **LodSelectionConfig**: `{errorThreshold:f32=1.0, screenHeight:u32, fov:f32, forceMaxLod:bool=false}`

### Downstream Consumers

- T6b.2.2 (GPU DAG Cut): GPU compute uses `IsCorrectLOD` logic
- T6b.3.1 (Perceptual LOD): extends error metric with perceptual weight
- T6b.10.1 (LOD Transition): uses error metric for dither blend factor

## Steps

- [ ] **Step 1**: Define LodSelector.h with projected error + IsCorrectLOD
      `[verify: compile]`
- [ ] **Step 2**: CPU-side LOD selection tests (known distances → expected LOD levels)
      `[verify: test]`
