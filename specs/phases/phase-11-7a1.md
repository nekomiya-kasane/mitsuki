# Phase 11: 7a-1 — CAD Rendering: Edges & Section

**Sequence**: 11 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

GPU hidden-line removal and section plane/volume — the two most geometry-intensive CAD visualization features. 10M edges < 4ms budget. ISO 128 line type/weight system. Watertight stencil capping with cross-hatch fill.

## Roadmap Digest

### Key Components (expanded from roadmap, split per research)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 0 | Edge Types + EdgeBuffer | `EdgeType` enum, `EdgeDescriptor` 32B (2×16B), `EdgeBuffer` SSBO, `LineType`/`LineWeight`/`LinePattern` types. `LinePatternSegment` 4B, `LinePatternEntry` 80B (5×16B, GPU-aligned). Edge count readback for stats. | ~7 |
| 1 | Edge Classification Compute | **Pre-cull** (meshlet normal-cone + frustum, 40-60% reduction) → per-edge classify (BDA vertex fetch, WaveBallot subgroup early-out) → **stream compaction** (GpuPrefixSum + scatter, deterministic). Output: CompactedEdgeBuffer + DispatchIndirect args. | ~10 |
| 2 | Edge Visibility Compute | Per-edge **adaptive** HiZ ray-march (N = clamp(screenLength/4, 4, 64)). Reads CompactedEdgeBuffer via DispatchIndirect. Output: visible + hidden edge buffers. | ~6 |
| 3 | SDF Edge Render | Mesh shader (T1) / vertex (T2-4) SDF AA line rendering. **DisplayStyleEdgeConfig** for all 6 edge-centric styles (Wireframe/HLR/HLR_VisibleOnly/Pen/Artistic/Sketchy). All 15 ISO 128 line types + ANSI Y14.2 + JIS B 0001. TAA jitter integration. RenderGraph conditional nodes. | ~10 |
| 4 | Halo / Gap Lines | T-junction clarity: depth-sample perpendicular, discard crossing edges. Halo margin for technical illustration. | ~3 |
| 5 | Section Plane v2 | Multi-plane clip (up to 8 planes, AND/OR). Stencil capping (watertight). **Per-section cap color/material**. **Reference geometry** (translucent plane quad). Contour extraction compute. | ~8 |
| 6 | ISO 128 Hatch Library | Cross-hatch **object-space UV** procedural fill per material (no camera swimming). 12+ standard patterns. Custom hatch descriptor. | ~4 |
| 7 | Section Volume | `SectionVolume` — OBB/Cylinder/**Sphere**/Boolean clip. Per-fragment inside-volume test. Stencil capping reuse. | ~5 |
| 8 | Demo + Integration | `cad_hlr_section` — STEP assembly with GPU HLR (pre-cull→classify→compact→visibility→render) + section plane + volume. RenderGraph conditional wiring. | ~5 |

### Critical Technical Decisions

- **5-stage HLR pipeline** (per arch spec §3 passes #28-#30): **PreCull** (meshlet normal-cone + frustum) → **Classify** (compute, BDA vertex fetch, WaveBallot) → **Compact** (GpuPrefixSum + scatter) → **Visibility** (compute, adaptive HiZ) → **Render** (graphics, SDF). Pre-cull and compaction are new stages that reduce edge count by 60-80% before the expensive visibility pass.
- **Stream compaction**: Deterministic prefix sum (not append buffer) per arch spec §5.5 deterministic rendering requirement. `DispatchIndirect` chains all downstream passes to compacted edge count.
- **BDA vertex fetch**: Classify shader reads vertex normals via BDA from `MeshletVertexData`, consistent with material resolve (arch spec §5.4). No separate normal SSBO binding.
- **SDF edge rendering**: Each edge expanded to a screen-aligned quad in mesh shader. Fragment shader computes signed distance to line center → smooth coverage. LinePattern sampled by parametric T along edge length. Width = ISO 128 line weight × DPI scale. **TAA jitter** applied to quad vertex positions for temporal stability.
- **DisplayStyleEdgeConfig**: Parameterizes the SDF shader for all 6 edge-centric DisplayStyles (arch spec §5.9.2): Wireframe, HLR, HLR_VisibleOnly, Pen, Artistic, Sketchy. Each style configures edge filter mask, line pattern override, color, jitter amplitude, extension length.
- **Adaptive HiZ sampling**: `N = clamp(screenLength / 4, 4, 64)` — saves ~30% compute vs fixed 16-32 samples.
- **Silhouette detection**: `dot(faceNormal_A, viewDir) * dot(faceNormal_B, viewDir) <= 0` — sign change across edge. Computed per-frame in classify pass.
- **Section stencil capping**: Front-face stencil increment, back-face stencil decrement. Cap where stencil != 0 (inside geometry). **Object-space UV** cross-hatch fill on cap faces (no camera-dependent swimming). **Per-section cap color/material** for multi-section clarity.
- **Section reference geometry**: Optional translucent plane quad per section (standard CAD viewer UX).
- **Tier differentiation**: T1 uses mesh shader for edge rendering (task→mesh pipeline). T2/3/4 use vertex shader + instanced quads. Same EdgeBuffer input, different rendering backend. `IPipelineFactory::CreateHLRPass()` dispatches to correct tier.
- **GPU struct alignment**: `EdgeDescriptor` = 32B (2×16B) `{float v0[3], uint32 packed, float v1[3], uint32 instanceId}` — classify resolves vertex positions via BDA, faceA/faceB not stored (classify-only). `LinePatternSegment` = 4B `{uint16 length_centimm, uint8 type, uint8 symbolIdx}`, `LinePatternEntry` = 80B (5×16B, segments[16]×4B + 16B metadata), both `alignas(16)` compliant for GPU SSBO reads.
- **ISO 128 completeness**: All 15 non-continuous line types (ISO01-ISO15) + ANSI Y14.2 + JIS B 0001 presets.

### Performance Targets

| Metric | Target |
|--------|--------|
| Edge pre-cull + classify + compact (10M raw → ~4-6M compacted) | < 1ms |
| Edge visibility (4-6M compacted, adaptive HiZ sampling) | < 1.5ms |
| SDF edge render (3-5M visible edges, 6 DisplayStyles) | < 1.5ms |
| Section plane stencil + cap + reference geometry | < 0.5ms |
| Total HLR pipeline | < 4ms @10M raw edges |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase 7a-2 | HLR edge output for combined demo. Section plane for OIT integration. |
| Phase 7b | Iso-parameter lines reuse SDF render pipeline (#30). Section plane for measurement section views. |
| Phase 8 | `GpuInstance.flags.lineStyle` drives per-instance edge rendering via CadScene `AttributeResolver`. |
| Phase 10 | Contour isolines (#73) feed into HLR SDF render pipeline. |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 6a | `GpuCullPipeline` (mesh shader pipeline), `HiZPyramid`, `VisibilityBuffer`, `SceneBuffer`, `GpuInstance.flags` (lineStyle bits 20-23), `GpuPrefixSum` (for edge stream compaction), `MeshletVertexData` BDA (for classify vertex fetch), meshlet normal-cone bounding data (for edge pre-cull) |
| Phase 5 | `TopoGraph` (face adjacency, edge classification source), `BVH` |
| Phase 3b | `IPipelineFactory::CreateHLRPass()` interface (stub, now implemented) |
| Phase 3a | `RenderGraphBuilder` for conditional HLR pass nodes |
| Phase 2 | `MeshData` (vertex positions, normals for edge classification) |

---

## Components & Tasks

### Component 0: Edge Types + EdgeBuffer

> Core data types for the HLR pipeline. Shared by classify, visibility, and render passes.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.0.1 | Edge types — `EdgeType`, `EdgeDescriptor`, `EdgeBuffer`, `LineType`, `LineWeight`, `LinePatternEntry` | — | M |

### Component 1: Edge Classification Compute

> Per-face-pair edge classify: silhouette (normal sign change), boundary (single-face), crease (dihedral angle), wire (explicit).

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.1.1 | Edge pre-cull + classify + compact — `EdgeClassifier::PreCull()`, `Classify()` (BDA, WaveBallot), `Compact()` (GpuPrefixSum) | T7a1.0.1 | H |
| [x] | T7a1.1.2 | Edge classification tests (vs CPU reference, all edge types) | T7a1.1.1 | M |

### Component 2: Edge Visibility Compute

> Per-edge HiZ ray-march for visible/hidden T-interval determination.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.2.1 | Edge visibility compute — adaptive HiZ ray-march (N=clamp(screenLen/4,4,64)), reads CompactedEdgeBuffer via DispatchIndirect | T7a1.1.1 | H |
| [x] | T7a1.2.2 | Visibility tests (known occlusion scenarios) | T7a1.2.1 | M |

### Component 3: SDF Edge Render

> Mesh shader (T1) / vertex shader (T2-4) SDF anti-aliased line rendering with ISO 128 line patterns.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.3.1 | SDF edge render pipeline — mesh shader quad + SDF fragment + DisplayStyleEdgeConfig (6 styles) + TAA jitter + RG conditional nodes | T7a1.2.1 | H |
| [x] | T7a1.3.2 | LinePattern SSBO — all 15 ISO 128 types + ANSI Y14.2 + JIS B 0001 presets + custom registration | T7a1.0.1 | M |
| [x] | T7a1.3.3 | Custom line pattern support + per-instance lineStyle override | T7a1.3.2 | M |
| [x] | T7a1.3.4 | SDF render tests (visual quality, pattern correctness, line weight accuracy) | T7a1.3.1 | M |

### Component 4: Halo / Gap Lines

> T-junction clarity for technical illustration quality.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.4.1 | Halo/gap line implementation — depth-sample perpendicular, discard crossing | T7a1.3.1 | M |

### Component 5: Section Plane v2

> Multi-plane clip with stencil capping.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.5.1 | Section plane types — `SectionPlaneConfig` (per-section capColor, capMaterialType, referenceGeometryEnabled), `clipPlaneMask` | — | L |
| [x] | T7a1.5.2 | Multi-plane clip + stencil capping + per-section cap color + reference geometry rendering | T7a1.5.1 | H |
| [x] | T7a1.5.3 | Contour extraction compute — section plane intersection polyline | T7a1.5.2 | M |
| [x] | T7a1.5.4 | Section plane tests (watertight cap, multi-plane boolean) | T7a1.5.2 | M |

### Component 6: ISO 128 Hatch Library

> Cross-hatch procedural fill on section cap faces.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.6.1 | Hatch pattern library — 13 ISO 128 patterns + custom descriptor | T7a1.5.2 | M |
| [x] | T7a1.6.2 | Hatch render shader — object-space UV procedural fill on stencil cap faces (no camera swimming) | T7a1.6.1 | M |

### Component 7: Section Volume

> OBB/Cylinder/Boolean volume clipping.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.7.1 | Section volume types — `SectionVolume`, OBB/Cylinder/Sphere/Boolean params | T7a1.5.1 | L |
| [x] | T7a1.7.2 | Per-fragment volume test + stencil capping | T7a1.7.1, T7a1.5.2 | M |
| [x] | T7a1.7.3 | Volume tests (OBB, cylinder, sphere, boolean AND/OR/SUBTRACT) | T7a1.7.2 | M |

### Component 8: Demo + Integration

> Full CAD HLR + section demo.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T7a1.8.1 | cad_hlr_section demo — STEP assembly, GPU HLR (pre-cull→compact→vis→render), section plane v2, section volume, RG conditional nodes | T7a1.3.1, T7a1.5.2, T7a1.7.2 | H |
| [x] | T7a1.8.2 | Integration tests (headless HLR + section render, edge count validation) | T7a1.8.1 | M |

---

## Demo Plan

### cad_hlr_section

- **Name**: `demos/cad_hlr_section/`
- **Shows**: STEP assembly with GPU HLR (visible solid + hidden dashed edges), section plane v2 (multi-plane + stencil cap + ISO 128 hatch), section volume (OBB cutaway)
- **Requires Tasks**: T7a1.0.1 through T7a1.8.1
- **CLI**: `--backend vulkan --display-style HLR --section-enabled`
- **Acceptance**:
  - [ ] Correct visible/hidden edge classification (pre-cull + classify + compact pipeline)
  - [ ] All 15 ISO 128 line types rendered correctly
  - [ ] All 6 edge-centric DisplayStyles functional (Wireframe, HLR, HLR_VisibleOnly, Pen, Artistic, Sketchy)
  - [ ] Watertight stencil caps on section planes with per-section cap color
  - [ ] Section reference geometry (translucent plane quad) visible and toggleable
  - [ ] Object-space cross-hatch fill on cap faces (no camera swimming)
  - [ ] 10M edges < 4ms total HLR pipeline
  - [ ] Section volume OBB/Cylinder/Sphere cutaway renders correctly
  - [ ] RenderGraph conditional nodes: HLR passes zero-cost when DisplayStyle is non-edge

## Test Summary

| Category    | Target | Notes |
|-------------|--------|-------|
| Unit        | ~35    | +5 from pre-cull/compact, GPU alignment, sphere volume, 15 ISO types |
| Integration | ~12    | +2 from DisplayStyle switching, RG conditional node |
| Benchmark   | ~3     |       |
| **Total**   | ~50    | Roadmap target: ~40 (exceeded due to expanded scope) |

## Implementation Order (Layers)

| Layer | Tasks | Depends on Layer |
|-------|-------|------------------|
| L0 | T7a1.0.1, T7a1.5.1, T7a1.7.1 | — |
| L1 | T7a1.1.1, T7a1.3.2 | L0 |
| L2 | T7a1.1.2, T7a1.2.1, T7a1.3.3, T7a1.5.2 | L1 |
| L3 | T7a1.2.2, T7a1.3.1, T7a1.5.3, T7a1.5.4, T7a1.6.1, T7a1.7.2 | L2 |
| L4 | T7a1.3.4, T7a1.4.1, T7a1.6.2, T7a1.7.3 | L3 |
| L5 | T7a1.8.1, T7a1.8.2 | L4 |

**Critical path**: L0 (types) → L1 (classify + patterns) → L2 (visibility + section) → L3 (render + hatch + volume) → L4 (polish) → L5 (demo)

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 7a-2 | HLR edge output composited with OIT transparency | `EdgeBuffer` SSBO → shared with OIT resolve |
| Phase 7b | Iso-parameter lines feed into SDF render pipeline. **Pen/Artistic/Sketchy DisplayStyle shaders**: Phase 7a-1 provides `DisplayStyleEdgeConfig` data structure and parameterized SDF shader; Phase 7b implements the style-specific jitter/noise/extension shaders for Pen (black, jitter), Artistic (soft pencil, noise-modulated), Sketchy (jitter+extension, hand-drawn). Sketch renderer (#77) reuses same SDF PSO. | Same `LinePatternEntry` SSBO, same SDF fragment shader, `DisplayStyleEdgeConfig` push constant |
| Phase 8 | Per-instance lineStyle from CadScene AttributeResolver | `GpuInstance.flags[20:23]` → `LineType` lookup in edge render |
| Phase 10 | Contour isolines → HLR SDF render | `IsolineBuffer` SSBO → same SDF PSO as #30, same `DisplayStyleEdgeConfig` |
| Phase 15a | HLR→SVG/PDF export | `EdgeBuffer` GPU→CPU readback via `ReadbackEdgeCount()` → 2D vector paths |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T7a1.0.1 | `EdgeTypes.h` (**public**) | T7a1.1.1, T7a1.2.1, T7a1.3.1, Phase 7b, Phase 10 | `EdgeType`, `EdgeDescriptor` (32B), `LinePatternSegment` (4B), `LinePatternEntry` (80B) |
| T7a1.0.1 | `EdgeBuffer.h` (**public**) | T7a1.1.1, T7a1.2.1, T7a1.3.1, Phase 15a | `EdgeBuffer::Create()`, `GetBuffer()`, `GetEdgeCount()`, `GetEdgeCountGpu()`, `ReadbackEdgeCount()` |
| T7a1.1.1 | `EdgeClassifier.h` (**public**) | T7a1.2.1, Phase 8 | `EdgeClassifier::PreCull(cmd, meshletData) -> void`, `Classify(cmd, topoGraph) -> void` (BDA vertex fetch), `Compact(cmd) -> void` (DispatchIndirect args) |
| T7a1.3.1 | `EdgeRenderer.h` (**public**) | T7a1.8.1, Phase 7b, Phase 10 | `EdgeRenderer::Render(cmd, edgeBuffer, linePatterns, DisplayStyleEdgeConfig)`, `DisplayStyleEdgeConfig` (6 styles) |
| T7a1.5.1 | `SectionPlane.h` (**public**) | T7a1.5.2, T7a1.6.1, T7a1.7.2, Phase 7a-2, Phase 7b | `SectionPlaneConfig` (planeEquation, booleanOp, capColor, capMaterialType, referenceGeometryEnabled) |
| T7a1.5.2 | `SectionPlane.h` (**public**) | T7a1.6.1, T7a1.7.2, Phase 7a-2, Phase 7b | `SectionPlane::Clip(cmd, planes, stencilTarget)`, `SectionPlane::RenderCap(cmd)`, `SectionPlane::RenderReferenceGeometry(cmd)` |
| T7a1.6.1 | `HatchLibrary.h` (**public**) | T7a1.6.2, Phase 7b | `HatchLibrary::GetPattern(materialType) -> HatchPatternDesc` |
| T7a1.7.1 | `SectionVolume.h` (**public**) | T7a1.7.2, Phase 7a-2 | `SectionVolume::Create(volumeType, params)`, `Clip(cmd)`. VolumeType: OBB/Cylinder/Sphere/Boolean |
