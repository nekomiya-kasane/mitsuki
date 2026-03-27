# Phase 13: 7b — CAD Rendering: Measurement, PMI, Tessellation & Import

**Sequence**: 13 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

Precision tools, full PMI rendering, GPU parametric tessellation, multi-format import, iso-parameter lines, specular-glossiness conversion, displacement mapping, GPU boolean preview, sketch renderer, CAD translator SDK. Complete the CAD viewer for engineering use.

## Roadmap Digest

### Key Components (expanded from roadmap)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | GPU Measurement | Sub-mm precision: distance/angle/radius/OBB, body-body min distance (BVH), mass properties (area/volume/centroid/inertia). Float64 native + DS emulation for WebGPU. | ~15 |
| 2 | GPU Boolean Preview | Depth peeling (N=8 layers), CSG resolve compute, union/subtract/intersect. Preview layer. | ~6 |
| 3 | GPU Draft Angle Analysis | Per-face dot(N, pullDir) → color map overlay. DFM validation. | ~3 |
| 4 | GPU PMI & Annotation | GD&T symbols, datum markers, roughness, weld (ISO 2553), tolerance frames. STEP AP242 PMI import. PmiFilter. | ~10 |
| 5 | Sketch Renderer | SDF lines/arcs/splines, constraint icons, dimension rendering, sketch edit mode, 2D snap, analysis overlay. | ~8 |
| 6 | Parametric Tessellation | GPU NURBS/BSpline eval compute. Phase 1: adaptive untrimmed. Phase 2: trim (SDF/point-in-curve). | ~8 |
| 7 | Import Pipeline | STEP/JT/glTF/STL/OBJ/PLY/.miki. ParallelTessellator (Coca worker pool). No-kernel mode. | ~8 |
| 8 | CAD Translator SDK | ITranslator plugin interface. ODA/Datakit/CoreTechnologie integration. | ~4 |
| 9 | Iso-Parameter Lines | CPU NURBS iso-u/v curve eval → polyline → SDF render (reuse HLR pipeline). | ~3 |
| 10 | Specular-Glossiness Conversion | CPU SpecGloss→MetalRough (Khronos algorithm). Import-time, zero runtime cost. | ~2 |
| 11 | Displacement Mapping | Compute vertex displacement along normal. Tier1 only. | ~2 |
| 12 | Color Emoji & BiDi | ColorGlyphCache (RGBA atlas), FriBidi paragraph-level BiDi. | ~4 |
| 13 | GPU Direct Curve Text | Fragment shader Bézier winding-number for >48px text. Hybrid MSDF/curve pipeline. | ~5 |
| 14 | PMI RichText Prerequisites | RichTextSpan tolerance stacks, stacked fractions, GD&T frames. Atlas BC7 compression. | ~3 |
| 15 | Demo + Integration | `cad_viewer` — full CAD viewer with all Phase 7b features. | ~8 |

### Performance Targets

| Metric | Target |
|--------|--------|
| GPU measurement (single query) | < 0.5ms |
| Mass properties (1000-body assembly) | < 10ms |
| Body-to-body distance (100K tri pairs) | < 2ms |
| Boolean preview (100K tri bodies) | < 16ms |
| Draft angle (1M triangles) | < 1ms |
| PMI render (1000 annotations) | < 0.1ms |
| Parametric tess (untrimmed, 10K surfaces) | < 5ms |
| Parallel STEP tessellation | >= 10x faster than single-thread |
| Sketch render (1000 entities) | < 0.5ms |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 7a-1 | `EdgeRenderer` (SDF line pipeline), `SectionPlane`, `HatchLibrary` |
| Phase 7a-2 | `BLAS/TLAS` (picking, reused for measurement), `OIT`, `ClusteredLighting`, `ShadowAtlas` |
| Phase 6a | `VisibilityBuffer`, `SceneBuffer`, `GpuCullPipeline`, `GpuRadixSort` |
| Phase 5 | `IKernel`, `TopoGraph`, `BVH` |
| Phase 4 | `BDAManager` (float64 buffer access), `BindlessTable` |
| Phase 2 | `TextRenderer` (MSDF), `MaterialRegistry`, `StandardPBR` |

---

## Components & Tasks

### Component 1: GPU Measurement

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.1.1 | PrecisionArithmetic.slang — precision_float abstraction (double/DSFloat) | — | M |
| [ ] | T7b.1.2 | Point/face/distance/angle/radius GPU queries | T7b.1.1 | H |
| [ ] | T7b.1.3 | Body-to-body min distance (BVH pair-traversal compute) | T7b.1.2 | H |
| [ ] | T7b.1.4 | Mass properties compute (area/volume/centroid/inertia, float64 reduction) | T7b.1.1 | H |
| [ ] | T7b.1.5 | OBB compute (PCA on vertex positions) | T7b.1.1 | M |
| [ ] | T7b.1.6 | Measurement tests (vs analytical + vs IKernel::ExactMassProperties) | T7b.1.4 | M |

### Component 2: GPU Boolean Preview

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.2.1 | Depth peeling compute (N=8 layers) | — | H |
| [ ] | T7b.2.2 | CSG resolve compute (per-pixel boolean on depth intervals) | T7b.2.1 | H |
| [ ] | T7b.2.3 | Boolean preview tests (union/subtract/intersect correctness) | T7b.2.2 | M |

### Component 3: GPU Draft Angle Analysis

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.3.1 | Draft angle compute + color map overlay | — | M |

### Component 4: GPU PMI & Annotation

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.4.1 | PMI entity types — PmiAnnotation, GdtSymbol, DatumMarker, WeldSymbol, ToleranceFrame | — | M |
| [ ] | T7b.4.2 | PMI render pipeline — instanced quads + MSDF text + leader lines + arrows | T7b.4.1 | H |
| [ ] | T7b.4.3 | STEP AP242 PMI import integration | T7b.4.1 | M |
| [ ] | T7b.4.4 | PmiFilter — type bitmask + view-plane alignment filter | T7b.4.2 | M |
| [ ] | T7b.4.5 | PMI tests (GD&T rendering, datum placement, AP242 round-trip) | T7b.4.4 | M |

### Component 5: Sketch Renderer

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.5.1 | Sketch entity types + SDF render (lines/arcs/splines/circles) | — | H |
| [ ] | T7b.5.2 | Constraint icons (MSDF atlas) + color coding (under/fully/over-constrained) | T7b.5.1 | M |
| [ ] | T7b.5.3 | Sketch dimension rendering (linear/angular/radial/diametral) | T7b.5.1 | M |
| [ ] | T7b.5.4 | Sketch edit mode (3D fade, grid, camera orient, 2D snap) | T7b.5.3 | M |
| [ ] | T7b.5.5 | Sketch tests (entity render, constraint display, dimension accuracy) | T7b.5.4 | M |

### Component 6: Parametric Tessellation

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.6.1 | GPU NURBS surface evaluation compute (control points + knot vectors → positions) | — | H |
| [ ] | T7b.6.2 | Adaptive subdivision (screen-space curvature metric, LOD) | T7b.6.1 | H |
| [ ] | T7b.6.3 | Index buffer generation compute (adaptive grid → triangle mesh) | T7b.6.2 | M |
| [ ] | T7b.6.4 | Parametric tess tests (vs IKernel::Tessellate CPU reference, max deviation < 0.01mm) | T7b.6.3 | M |

### Component 7: Import Pipeline

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.7.1 | GltfPipeline — cgltf parsing, auto-meshlet, material import | — | M |
| [ ] | T7b.7.2 | JtImporter — Siemens JT format (tessellated LOD + PMI + assembly) | — | H |
| [ ] | T7b.7.3 | MeshImporter — STL/OBJ/PLY trivial parsers + auto-meshlet | — | L |
| [ ] | T7b.7.4 | ParallelTessellator — Coca worker pool, per-body IKernel::Tessellate, progressive upload | T7b.7.1 | H |
| [ ] | T7b.7.5 | Import tests (STEP round-trip, JT structure/LOD, glTF integrity, no-kernel .miki load) | T7b.7.4 | M |

### Component 8: CAD Translator SDK

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.8.1 | ITranslator plugin interface + TranslatorRegistry | — | M |
| [ ] | T7b.8.2 | ODA/Datakit stub integration (plugin DLL loading) | T7b.8.1 | M |

### Component 9: Iso-Parameter Lines

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.9.1 | CPU iso-u/v curve evaluation + polyline → EdgeBuffer → SDF render | — | M |

### Component 10: Specular-Glossiness Conversion

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.10.1 | SpecGlossToMetalRough CPU converter (Khronos algorithm) | — | L |

### Component 11: Displacement Mapping

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.11.1 | Vertex displacement compute shader (normal × texture × scale) | — | M |

### Component 12: Color Emoji & BiDi

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.12.1 | ColorGlyphCache — FT_LOAD_COLOR → RGBA atlas pages | — | M |
| [ ] | T7b.12.2 | FriBidi integration — paragraph-level UAX#9 BiDi reordering | T7b.12.1 | M |

### Component 13: GPU Direct Curve Text

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.13.1 | Bézier curve data SSBO + bounding quad generation | — | M |
| [ ] | T7b.13.2 | Fragment shader winding-number evaluation (Lengyel 2017) | T7b.13.1 | H |
| [ ] | T7b.13.3 | Hybrid MSDF/curve selection (auto-switch at 48px threshold) | T7b.13.2 | M |

### Component 14: PMI RichText Prerequisites

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.14.1 | RichTextSpan extensions (tolerance stacks, fractions, GD&T frames) | — | M |
| [ ] | T7b.14.2 | Atlas BC7 compression (4:1) for large PMI assemblies | T7b.14.1 | M |

### Component 15: Demo + Integration

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T7b.15.1 | cad_viewer demo — full CAD viewer with all Phase 7b features | all above | H |
| [ ] | T7b.15.2 | Integration tests (measurement accuracy, PMI render, import pipeline) | T7b.15.1 | H |

---

## Test Summary

| Category | Target | Notes |
|----------|--------|-------|
| Unit | ~50 | |
| Integration | ~20 | |
| Benchmark | ~8 | |
| **Total** | ~78 | Roadmap target: ~80 |

## Implementation Order (Layers)

| Layer | Tasks | Depends on |
|-------|-------|------------|
| L0 | T7b.1.1, T7b.2.1, T7b.3.1, T7b.4.1, T7b.5.1, T7b.6.1, T7b.7.1-7.3, T7b.8.1, T7b.9.1, T7b.10.1, T7b.11.1, T7b.12.1, T7b.13.1, T7b.14.1 | — |
| L1 | T7b.1.2, T7b.2.2, T7b.4.2, T7b.5.2, T7b.5.3, T7b.6.2, T7b.7.4, T7b.8.2, T7b.12.2, T7b.13.2, T7b.14.2 | L0 |
| L2 | T7b.1.3, T7b.1.4, T7b.1.5, T7b.2.3, T7b.4.3, T7b.4.4, T7b.5.4, T7b.6.3, T7b.13.3 | L1 |
| L3 | T7b.1.6, T7b.4.5, T7b.5.5, T7b.6.4, T7b.7.5 | L2 |
| L4 | T7b.15.1, T7b.15.2 | L3 |

**Critical path**: L0 (types + core shaders) → L1 (pipelines + import) → L2 (advanced features) → L3 (tests) → L4 (demo)

---

## Forward Design Notes

| Future Phase | What this phase prepares |
|-------------|------------------------|
| Phase 8 | CadScene imports via Import Pipeline. PMI attached to segments. Measurement invoked from CadScene commands. |
| Phase 9 | Sketch edit mode reuses SketchRenderer. RichTextInput uses Color Emoji + BiDi. Gizmo system uses measurement. |
| Phase 10 | CAE scalar field visualization reuses draft angle overlay infrastructure. Point cloud import reuses MeshImporter. |
| Phase 14 | Parametric tessellation GPU trim Phase 2 (SDF trim textures). DS emulation validation at 10B tri scale. |
| Phase 15a | Headless measurement + PMI for SDK. Export pipeline uses ITranslator. |
