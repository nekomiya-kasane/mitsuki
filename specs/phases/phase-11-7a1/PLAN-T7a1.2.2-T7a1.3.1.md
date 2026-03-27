# Implementation Plan: T7a1.2.2 + T7a1.3.1

## Part A: T7a1.2.2 — Edge Visibility Tests (M effort, ~1h)

Expand `tests/unit/test_edge_visibility_gpu.cpp` with 5 new test cases:

| Test | Scenario | Validates |
|------|----------|-----------|
| `KnownOcclusion_AllHidden` | Edge behind wall (HiZ depth < edge depth) | All edges in hidden buffer |
| `PartialOcclusion_BothBuffers` | Occluder covers middle of edge | Edge in BOTH buffers (Fix D) |
| `SilhouetteBoundary_Visible` | Edge at grazing angle | Edge classified visible |
| `IndirectDispatch_CountConsistency` | Verify visible+hidden >= input | DispatchIndirect args correct |
| `SubPixelEdge_Handled` | Very short edge (< 1px) | No crash, appears in one buffer |

## Part B: T7a1.3.1 — SDF Edge Render Pipeline (H effort, ~3h)

### Architecture: Mesh Shader Primary + VS Fallback

```
EdgeBuffer SSBO
  |
  +--[Mesh Path]  Task(64/WG, sub-pixel cull) -> Mesh(4v+2t quad) -> Fragment(SDF)
  |
  +--[VS Path]    DrawIndirect(6*count) -> VS(quad expand) -> Fragment(SDF, shared)
```

### Files

| File | Action |
|------|--------|
| `include/miki/hlr/EdgeRenderer.h` | NEW — EdgeRenderer class, DisplayStyleEdgeConfig UBO (224B) |
| `src/miki/hlr/EdgeRenderer.cpp` | NEW — Pipeline creation (mesh+VS), tier dispatch, Render() |
| `shaders/hlr/edge_sdf_task.slang` | NEW — Task shader: sub-pixel cull, payload emit |
| `shaders/hlr/edge_sdf_mesh.slang` | NEW — Mesh shader: screen-aligned quad, lineCoord |
| `shaders/hlr/edge_sdf_frag.slang` | NEW — Fragment: SDF AA + dash + DisplayStyle |
| `shaders/hlr/edge_sdf_vert.slang` | NEW — VS fallback: instanced quad expansion |
| `shaders/hlr/hlr_common.slang` | MODIFY — Add LinePatternEntry Slang mirror, DisplayStyleEdgeConfig |
| `tests/unit/test_edge_renderer_gpu.cpp` | NEW — GPU tests |
| `src/miki/hlr/CMakeLists.txt` | MODIFY — Add EdgeRenderer.cpp |
| `tests/unit/CMakeLists.txt` | MODIFY — Add test target |

### DisplayStyleEdgeConfig (224B, UBO)

```
edgeColors[8][4]      128B  per-EdgeType RGBA
widthMultipliers[8]    32B  per-EdgeType width scale
globalWidthScale        4B  DPI scale
hiddenEdgeAlpha         4B  0.0-1.0
aaKernelWidth           4B  1.0 standard
wobbleAmplitude         4B  pixels (Artistic/Sketchy)
wobbleFrequency         4B  oscillations/100px
overshootLength         4B  pixels (Sketchy)
displayStyle            4B  enum
showHiddenEdges         4B  0 or 1
edgeTypeMask            4B  bitmask
_pad[3]                12B
```

### SDF Fragment Algorithm

1. Section plane clip (sectionClipDiscard)
2. SDF: |lineCoord.x| - halfWidth, round caps at endpoints
3. AA: fwidth-based smoothstep (1px transition band)
4. Dash: sampleDashPattern(lineCoord.y, lineType, patternLib)
5. DisplayStyle modulation (wobble, ink variation)
6. Stochastic alpha for sub-pixel lines (TAA-friendly)
7. Output premultiplied alpha

### Execution Order

1. T7a1.2.2 — visibility tests
2. T7a1.3.1 Step 1-2 — headers + shaders
3. T7a1.3.1 Step 3-4 — EdgeRenderer.cpp + DisplayStyle presets
4. T7a1.3.1 Step 5 — tests + verify

### Performance Target

< 1.5ms @ 5M visible edges (1080p, RTX 4070)
