# Tcd1.3.4 — API Audit: vgeo Public Headers

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: API Audit
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Phase 6a+6b vgeo public headers need audit before freeze: GpuCullTypes.h, GpuCullPipeline.h, GpuInstance.h, SceneBuffer.h, MacroBinning.h, VisibilityBuffer.h, MaterialResolve.h, SwRasterizer.h, MeshletTypes.h, MeshletGenerator.h, HiZPyramid.h, ClusterDAG.h, MeshletCompressor.h, MeshSimplifier.h, ChunkLoader.h, PersistentDispatch.h, LodSelector.h, DagCutOptimizer.h, VisibilityPersistence.h.
- **Affected files**: `include/miki/vgeo/*.h`

### Acceptance Criteria

- [ ] All public methods have `[[nodiscard]]` where applicable
- [ ] All GPU structs have `alignas`, `static_assert(sizeof)`, `is_trivially_copyable`
- [ ] Complete Doxygen on all public types and methods
- [ ] Consistent error model (`Result<T>` for fallible, void for infallible)

## Steps

- [ ] **Step 1**: Audit all vgeo public headers
      `[verify: compile]`
