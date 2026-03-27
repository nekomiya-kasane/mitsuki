# T6b.8.1 — CPU QEM Simplifier (Garland-Heckbert, Border-Locked, Attribute-Aware)

**Phase**: 09-6b
**Component**: GPU Mesh Simplification (QEM)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

None (L0 task).

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/MeshSimplifier.h` | **public** | **H** | `MeshSimplifier::Simplify()`, `SimplifiedMesh`, `SimplifyConfig` |

- **Algorithm**: Garland-Heckbert 1997 quadric error metric edge collapse.
  1. Compute per-vertex quadric Q (sum of outer products of incident face planes).
  2. Per-edge: compute collapse cost `v_bar^T * Q * v_bar` where Q = Q_v1 + Q_v2.
  3. Priority queue (min-heap) by cost.
  4. Collapse cheapest edge, update neighbors, repeat until target count or max error.
- **Border locking**: Boundary edges get infinite cost → never collapsed. Option `lockBorder` flag.
- **Attribute-aware**: Optional `attributeWeights` (normal, UV) added to quadric error. Per meshoptimizer: ~1.5× memory/time.
- **Performance**: 3M tri/s on CPU (meshoptimizer-class). 1M→100K in ~300ms.
- **UV seam preservation**: Seam vertices split into multiple quadric entries (one per UV chart).

### Downstream Consumers

- T6b.8.2 (QEM Integration with ClusterDAG): per-group simplification for LOD levels.
- T6b.11.2 (mesh_simplify_demo): interactive simplification with quality slider.
- Phase 14: GPU QEM accelerator extends this with parallel edge collapse.

### Technical Direction

- Per meshoptimizer research: `meshopt_simplify` achieves 3M tri/s with attribute-aware mode at 1.5× cost.
- Our implementation follows the same algorithm but in C++23 with `std::expected` error handling.
- Independent set selection for GPU QEM (T6b.8.3) is a stretch goal — CPU QEM is sufficient for import-time LOD generation.

## Steps

- [ ] **Step 1**: Define MeshSimplifier.h with Simplify API + config types
      `[verify: compile]`
- [ ] **Step 2**: Implement quadric computation + edge collapse + priority queue
      `[verify: compile]`
- [ ] **Step 3**: Tests (simplification ratio, boundary preservation, attribute quality)
      `[verify: test]`

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(MeshSimplifier, SimplifySphere_50Percent)` | Positive | 1K tri sphere → 500 tri |
| `TEST(MeshSimplifier, BoundaryPreservation)` | Positive | Open mesh boundary edges preserved |
| `TEST(MeshSimplifier, UVSeamPreservation)` | Positive | UV seam vertices not collapsed |
| `TEST(MeshSimplifier, ErrorBelowThreshold)` | Positive | Max screen-space error < target |
| `TEST(MeshSimplifier, LockBorder)` | Positive | lockBorder=true → boundary unchanged |
| `TEST(MeshSimplifier, Perf_1M_Under500ms)` | Benchmark | 1M tri → 100K in <500ms |
| `TEST(MeshSimplifier, EmptyMesh)` | Boundary | 0 tri → empty result |
| `TEST(MeshSimplifier, EndToEnd_QualityVsReference)` | Integration | Simplified mesh Hausdorff distance vs CPU reference |
