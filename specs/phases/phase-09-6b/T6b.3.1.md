# T6b.3.1 — Perceptual LOD Weights

**Phase**: 09-6b
**Component**: Perceptual LOD
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.2.1 | LOD Selector Types | Not Started | `ProjectedSphereError()` |

## Context Anchor

### This Task's Contract

**Produces**: Perceptual weighting for LOD error metric. `perceptual_weight = max(silhouette_factor, curvature_factor, selection_factor)`. Flat interior faces simplify 3× more aggressively than silhouette edges.

- **Curvature bound**: Per-meshlet float stored in `MeshletDescriptor.lodError` field (repurposed). Precomputed during ClusterDAG build from max triangle-pair dihedral angle.
- **Silhouette factor**: `1.0` at silhouette (cone cutoff near 0), `0.3` at interior (cone cutoff << 0).
- **Selection factor**: `forceMaxLOD` within 2m radius of selected entity.
- **Effect**: ~25% meshlet count reduction for typical CAD (70% flat faces) with zero perceptual quality loss.

### Downstream Consumers

- T6b.2.2 (GPU DAG Cut): applies perceptual weight to projected error in compute shader.

## Steps

- [ ] **Step 1**: Compute per-meshlet curvature bound during ClusterDAG build
      `[verify: compile]`
- [ ] **Step 2**: Integrate perceptual weight into LOD selector + GPU shader
      `[verify: compile]`
- [ ] **Step 3**: Tests (flat mesh → low weight, curved mesh → high weight)
      `[verify: test]`
