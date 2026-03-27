# T6b.1.1 — ClusterDAG Types

**Phase**: 09-6b (ClusterDAG, Streaming, LOD)
**Component**: ClusterDAG Builder
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | — | — | Phase 6a `MeshletTypes.h` (MeshletDescriptor 64B, MeshletBuildResult) |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/ClusterDAG.h` | **public** | **H** | `ClusterNode`, `ClusterLevel`, `ClusterDAG`, `ClusterBuildConfig`, `ClusterBuildResult` |

- **Error model**: `ClusterDAG::Build()` → `Result<ClusterBuildResult>`.
- **Thread safety**: Build is single-threaded (import-time). ClusterDAG is immutable after build.
- **GPU constraints**: `ClusterNode` must be GPU-friendly (alignas(16), trivially copyable). Contains: `boundingSphere` (float4), `parentError` (float), `childError` (float), `meshletRange` (uint2 — start+count into per-level meshlet array), `childRange` (uint2 — start+count into child level), `flags` (uint32).

**Key types**:

| Symbol | Signature | Attrs |
|--------|-----------|-------|
| `ClusterNode` | 48B GPU struct: `{boundingSphere:f4, parentError:f32, childError:f32, meshletStart:u32, meshletCount:u32, childStart:u32, childCount:u32, _pad:u32[2]}` | `alignas(16)`, trivially copyable |
| `ClusterLevel` | `{nodes: vector<ClusterNode>, meshlets: vector<MeshletDescriptor>}` | Per-LOD level |
| `ClusterDAG` | `{levels: vector<ClusterLevel>, totalMeshletCount: u32, maxLodLevel: u32}` | Immutable after build |
| `ClusterBuildConfig` | `{maxLodLevels:u32=12, targetSimplificationRatio:f32=0.5, groupSize:u32=4}` | Build params |
| `ClusterDAG::Build` | `(MeshletBuildResult const&, ClusterBuildConfig const&) -> Result<ClusterBuildResult>` | `[[nodiscard]]` static |
| `ClusterDAG::GetLevel` | `(uint32_t level) const -> ClusterLevel const&` | `[[nodiscard]]` |
| `ClusterDAG::GetLevelCount` | `() const noexcept -> uint32_t` | `[[nodiscard]]` |

### Downstream Consumers

- T6b.1.2 (same Phase): adjacency graph reads `ClusterLevel::meshlets`
- T6b.2.1 (same Phase): LOD selector reads `ClusterNode::parentError/childError`
- T6b.6.3 (same Phase): OctreeResidency indexes `ClusterDAG` levels
- Phase 7a-1: HLR reads `ClusterDAG` for edge LOD
- Phase 8: `CadSegment` owns `ClusterDAG` handle
- Phase 14: 2B+ tri validation reads `ClusterDAG`

### Upstream Contracts

- Phase 6a `MeshletTypes.h`: `MeshletDescriptor` (64B), `MeshletBuildResult`, `Meshlet` (16B)
- Phase 6a `MeshletGenerator.h`: `MeshletGenerator::Build()` → `MeshletBuildResult`

### Technical Direction

- **ClusterNode layout** matches nanite-webgpu's `NaniteMeshletTreeNode`: bounding sphere + error + child/meshlet ranges. 48B fits in 3 cache lines.
- **Error monotonicity invariant**: `parent.parentError >= max(child.childError for all children)`. Verified in T6b.1.6.
- **Level 0 = highest detail** (original meshlets from Phase 6a). Level N = coarsest (single root cluster).
- Per research: meshoptimizer's `simplifyClusters` uses group-of-4 → merge → border-locked QEM → split. We match this pattern.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/vgeo/ClusterDAG.h` | **public** | **H** | ClusterDAG types + Build API |
| Create | `tests/unit/test_cluster_dag_types.cpp` | internal | L | Layout + static_assert tests |

## Steps

- [ ] **Step 1**: Define ClusterDAG.h with all types (heat H)
      `[verify: compile]`

- [ ] **Step 2**: CPU-only layout tests (sizeof, offsetof, trivially_copyable)
      `[verify: test]`

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(ClusterDAG, ClusterNode_Size)` | Positive | sizeof == 48 |
| `TEST(ClusterDAG, ClusterNode_Align)` | Positive | alignof >= 16 |
| `TEST(ClusterDAG, ClusterNode_TriviallyCopyable)` | Positive | trivially copyable |
| `TEST(ClusterDAG, ClusterBuildConfig_Defaults)` | Positive | maxLodLevels=12, ratio=0.5, groupSize=4 |

## Design Decisions

*(Fill during implementation.)*

## Implementation Notes

*(Fill during implementation.)*
