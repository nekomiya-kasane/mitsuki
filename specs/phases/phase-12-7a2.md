# Phase 12: 7a-2 — CAD Rendering: Transparency, Picking & Lighting

**Sequence**: 12 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

Order-independent transparency, ray picking with acceleration structure, explode view, RTAO activation, clustered light culling (4096 lights), shadow atlas, SSR, DoF, motion blur, CAS sharpen, color grading, chromatic aberration, outline post-process, texture projection modes, material graph. Complete the core CAD interaction loop and Realistic display mode.

## Roadmap Digest

### Key Components (expanded, grouped by subsystem)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | Linked-List OIT | Per-pixel atomic linked list, 16M node pool, sort resolve (insertion ≤16, merge >16), X-Ray mode | ~10 |
| 2 | Ray Picking v2 — BLAS/TLAS | Incremental BLAS refit/compact, incremental TLAS rebuild, single-point pick via VisBuffer + RT ray query | ~12 |
| 3 | Ray Picking v2 — Area + Lasso | Box select (no-drill: VisBuffer collect, drill: 3-stage volume cull), lasso polygon pick (winding number compute), SelectionFilter | ~15 |
| 4 | Explode View v2 | GPU compute transforms from assembly hierarchy, multi-level, smoothstep animation | ~5 |
| 5 | RTAO Activation | Activate Phase 3b stub, reuse BLAS/TLAS from picking, ray query compute, temporal accumulation | ~5 |
| 6 | Clustered Light Culling | GPU 3D froxel grid, per-light AABB project + atomicAdd, `GpuLight` 64B, LTC area light textures | ~8 |
| 7 | Shadow Atlas | Single D32F atlas for point/spot/area, LRU tile management, distance-based resolution | ~6 |
| 8 | SSR (Screen-Space Reflections) | Hi-Z ray march compute, half-res + bilateral upsample, temporal accumulation | ~4 |
| 9 | Depth of Field | Gather-based bokeh (Jimenez 2014), CoC from depth+aperture, half-res 16 samples | ~4 |
| 10 | Motion Blur | Per-pixel directional blur (McGuire 2012), tile max velocity, GBuffer motion vectors | ~4 |
| 11 | Post-Process Bundle | CAS Sharpen + Color Grading (3D LUT) + Chromatic Aberration + Outline (Sobel) | ~6 |
| 12 | Texture Projection Modes | UV/Triplanar/Box/Sphere/Cylinder/Decal in material resolve + forward fragment | ~4 |
| 13 | Material Graph | `MaterialGraphCompiler` — node-based → Slang permutation, built-in presets, ImGui editor (Phase 9) | ~6 |
| 14 | Demo + Integration | `cad_oit_pick` + combined `cad_hlr_oit` demo, integration tests | ~8 |

### Performance Targets

| Metric | Target |
|--------|--------|
| LL-OIT insert + resolve | < 2ms @4K (16 layers) |
| Single-point pick | < 0.5ms |
| Lasso pick (4K, 32-vertex polygon) | < 2ms |
| Drill box-select (100K instances) | < 3ms |
| Clustered light assign (4K lights) | < 0.3ms |
| Shadow atlas render (32 lights) | < 2ms |
| SSR (half-res 4K) | < 1.5ms |
| DoF (half-res 4K) | < 1.5ms |
| Motion blur (4K) | < 1.0ms |
| CAS + ColorGrade + CA + Outline | < 0.7ms combined |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 7a-1 | `EdgeRenderer`, `SectionPlane`, `SectionVolume`, `HatchLibrary` |
| Phase 6a | `VisibilityBuffer`, `SceneBuffer`, `GpuCullPipeline`, `HiZPyramid`, `GpuRadixSort`, `GpuCompact` |
| Phase 5 | `BVH`, `TopoGraph` (FaceType for SelectionFilter) |
| Phase 3b | RTAO stub, TAA, Bloom, ToneMapping, AutoExposure, FXAA |
| Phase 4 | `BindlessTable`, `BDAManager`, `ResourceManager` |

---

## Components & Tasks

### Component 1: Linked-List OIT

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.1.1 | OIT types — `OitNodePool`, `OitHeadImage`, `OitConfig` | — | M |
| [ ] | T7a2.1.2 | OIT insert pass — fragment shader atomic linked-list build | T7a2.1.1 | H |
| [ ] | T7a2.1.3 | OIT resolve pass — compute sort (insertion ≤16, merge >16) + composite | T7a2.1.2 | H |
| [ ] | T7a2.1.4 | X-Ray mode + hybrid weighted fallback + tests | T7a2.1.3 | M |

### Component 2: Ray Picking v2 — BLAS/TLAS

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.2.1 | Incremental BLAS — refit dirty + periodic compact | — | H |
| [ ] | T7a2.2.2 | Incremental TLAS — update moved instances | T7a2.2.1 | M |
| [ ] | T7a2.2.3 | Single-point pick — VisBuffer readback (no-drill) + RT ray query (drill) | T7a2.2.2 | M |
| [ ] | T7a2.2.4 | Multi-hit drill pick + topology-level return | T7a2.2.3 | M |

### Component 3: Ray Picking v2 — Area + Lasso

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.3.1 | No-drill box/lasso — VisBuffer collect + dedup via GpuRadixSort | T7a2.2.3 | M |
| [ ] | T7a2.3.2 | Drill box — 3-stage GPU volume culling (instance→meshlet→triangle) | T7a2.3.1 | H |
| [ ] | T7a2.3.3 | Lasso polygon pick — winding number compute + mask generation | T7a2.3.1 | H |
| [ ] | T7a2.3.4 | SelectionFilter — CPU post-filter (FaceType, layerMask, customPredicate) | T7a2.3.1 | M |
| [ ] | T7a2.3.5 | Edge/vertex proximity tolerance (snap-to-edge neighborhood sampling) | T7a2.3.4 | M |
| [ ] | T7a2.3.6 | Selectability enforcement (GPU selectableMask + CPU safety net) | T7a2.3.4 | M |
| [ ] | T7a2.3.7 | Pick system tests (all 6 modes, selectability, filter, proximity) | T7a2.3.6 | H |

### Component 4: Explode View v2

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.4.1 | Explode compute — GPU transforms from assembly hierarchy + animation | — | M |

### Component 5: RTAO Activation

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.5.1 | RTAO activation — ray query compute, reuse BLAS, temporal accumulation | T7a2.2.2 | M |

### Component 6: Clustered Light Culling

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.6.1 | GpuLight struct (64B) + LightBuffer SSBO | — | L |
| [ ] | T7a2.6.2 | 3D froxel grid compute — per-light AABB project + atomicAdd assign | T7a2.6.1 | H |
| [ ] | T7a2.6.3 | Deferred resolve integration — read cluster grid, iterate lights | T7a2.6.2 | M |
| [ ] | T7a2.6.4 | LTC area light textures (2× RGBA32F 64×64) | T7a2.6.1 | M |
| [ ] | T7a2.6.5 | Clustered light tests (100/1K/4K lights correctness) | T7a2.6.3 | M |

### Component 7: Shadow Atlas

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.7.1 | Shadow atlas types — `ShadowAtlas`, tile allocation, LRU management | T7a2.6.1 | M |
| [ ] | T7a2.7.2 | Shadow atlas render — per-light viewport, mesh shader (T1) / vertex (T2-4) | T7a2.7.1 | H |
| [ ] | T7a2.7.3 | Shadow atlas tests (tile allocation/LRU, shadow correctness) | T7a2.7.2 | M |

### Component 8: SSR

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.8.1 | SSR compute — Hi-Z ray march, half-res trace, bilateral upsample, temporal | — | H |

### Component 9: Depth of Field

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.9.1 | DoF compute — CoC, gather-based bokeh (Jimenez 2014), half-res 16 samples | — | M |

### Component 10: Motion Blur

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.10.1 | Motion blur compute — tile max velocity, per-pixel directional blur (McGuire 2012) | — | M |

### Component 11: Post-Process Bundle

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.11.1 | CAS Sharpen — FidelityFX single compute pass | — | L |
| [ ] | T7a2.11.2 | Color Grading — 3D LUT (32³ RGBA8) sampler3D | — | L |
| [ ] | T7a2.11.3 | Chromatic Aberration — 3-channel radial distortion folded into tone-map | — | L |
| [ ] | T7a2.11.4 | Outline Post-Process — Sobel depth+normal, configurable color | — | L |

### Component 12: Texture Projection Modes

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.12.1 | Projection mode types + material resolve integration (Triplanar/Box/Sphere/Cylinder/Decal) | — | M |

### Component 13: Material Graph

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.13.1 | MaterialGraphCompiler — node-based → Slang permutation compile | — | H |
| [ ] | T7a2.13.2 | Built-in material presets (brushed metal, carbon fiber, wood, concrete) | T7a2.13.1 | M |

### Component 14: Demo + Integration

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7a2.14.1 | cad_oit_pick demo — OIT + pick + explode + clustered lights + shadows | T7a2.1.3, T7a2.3.7, T7a2.6.3, T7a2.7.2 | H |
| [ ] | T7a2.14.2 | cad_hlr_oit combined demo — all Phase 7a-1 + 7a-2 features together | T7a2.14.1 | M |
| [ ] | T7a2.14.3 | Integration tests (all subsystems) | T7a2.14.2 | H |

---

## Test Summary

| Category | Target | Notes |
|----------|--------|-------|
| Unit | ~55 | |
| Integration | ~25 | |
| Benchmark | ~10 | |
| **Total** | ~90 | Roadmap target: ~95 |

## Implementation Order (Layers)

| Layer | Tasks | Depends on |
|-------|-------|------------|
| L0 | T7a2.1.1, T7a2.2.1, T7a2.4.1, T7a2.6.1, T7a2.8.1, T7a2.9.1, T7a2.10.1, T7a2.11.1-11.4, T7a2.12.1, T7a2.13.1 | — |
| L1 | T7a2.1.2, T7a2.2.2, T7a2.6.2, T7a2.6.4, T7a2.7.1, T7a2.13.2 | L0 |
| L2 | T7a2.1.3, T7a2.2.3, T7a2.5.1, T7a2.6.3, T7a2.7.2 | L1 |
| L3 | T7a2.1.4, T7a2.2.4, T7a2.3.1, T7a2.6.5, T7a2.7.3 | L2 |
| L4 | T7a2.3.2, T7a2.3.3, T7a2.3.4 | L3 |
| L5 | T7a2.3.5, T7a2.3.6 | L4 |
| L6 | T7a2.3.7, T7a2.14.1, T7a2.14.2, T7a2.14.3 | L5 |

**Critical path**: OIT + BLAS/TLAS (L0-L2) → Pick system (L3-L5) → Demo (L6)

---

## Forward Design Notes

| Future Phase | What this phase prepares |
|-------------|------------------------|
| Phase 7b | Pick topology return for measurement. Section + OIT combined. |
| Phase 8 | CadScene layer selectability. Pick filter CommandBus integration. |
| Phase 9 | Explode view gizmo. Pick modes for interactive editing. |
| Phase 10 | Point cloud pick via BVH. CAE scalar field selection. |
| Phase 14 | Shadow atlas optimization. Clustered light scaling. |
