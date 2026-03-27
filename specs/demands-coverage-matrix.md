# Demands → Rendering Pipeline Architecture Coverage Matrix

> **Source**: `specs/demands.md` (395 features) vs `specs/rendering-pipeline-architecture.md` (2042 lines)
>
> **Legend**:
> - ✅ = Fully covered (detailed architecture design exists)
> - 🟡 = Partially covered (mentioned but lacks detail)
> - ❌ = Not covered
> - **Arch §** = Section in rendering-pipeline-architecture.md
> - **Priority**: M = must-have, P = progressive, A = advanced, F = frontier

---

## §1 Display Modes & Visual Styles (13 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| D1.1 | Shaded | ✅ | §9.7.4 `DisplayStyle::Shaded` | Tier B, GeometryPass |
| D1.2 | Shaded + Edges | ✅ | §5.8.3 + §9.7.4 `ShadedEdges` | Bucket 1 + Bucket 3 |
| D1.3 | Wireframe | ✅ | §9.7.4 `Wireframe` | Tier A, EdgePass(all) |
| D1.4 | Hidden Line (HLR) | ✅ | §9.1 + §9.6.1 + §9.7.4 `HLR` | Full 4-stage GPU HLR |
| D1.5 | HLR Visible Only | ✅ | §9.7.4 `HLR_VisibleOnly` | Hidden edges suppressed |
| D1.6 | X-Ray / Transparent | ✅ | §9.7.4 `XRay` | OIT forced, alpha=0.3 |
| D1.7 | Ghosted | ✅ | §9.7.4 `Ghosted` | Desaturated, alpha=0.5 |
| D1.8 | Realistic / Rendered | ✅ | §9.7.4 `Realistic` | Full PBR + IBL + VSM + AO |
| D1.9 | Flat / No Shading | ✅ | §9.7.4 `NoShading` | Flat color, no lighting |
| D1.10 | Matcap | ✅ | §9.7.4 `Matcap` | Sphere mapping lookup |
| D1.11 | Arctic | ✅ | §9.7.4 `Arctic` | White + AO |
| D1.12 | Pen / Artistic / Sketchy | ✅ | §9.7.4 `Pen`, `Artistic`, `Sketchy` | 3 NPR modes |
| D1.13 | Draft Quality | 🟡 | — | Not a DisplayStyle; implied by LOD bias but no explicit mode |

**Coverage: 12/13 (92%)**

---

## §2 Geometric Rendering (12 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| G2.1 | Tessellated Meshes | ✅ | §5 VisBuffer + Mesh Shader | Core geometry path |
| G2.2 | B-Rep Exact Curves | ❌ | — | GPU NURBS eval mentioned in Phase 7b but no architecture |
| G2.3 | Surface Normals | ✅ | §5.5 GpuInstance + §5.7 oct normal | Meshlet compressed normals |
| G2.4 | Instancing | ✅ | §5.5 GpuInstance + §5.2 macro-binning | GPU-driven instancing |
| G2.5 | Double-Sided Rendering | ✅ | §6.4.1 `cullMode` dynamic + per-instance flag | Mesh shader skips normal cone cull |
| G2.6 | Continuity Visualization | 🟡 | §9.7.1 Zebra/Isophotes listed | Analysis overlay, not full architectural detail |
| G2.7 | Faceted / Smooth | ✅ | §9.7.4 Shaded vs NoShading | Controlled by DisplayStyle |
| G2.8 | Subdivision Surface | ❌ | — | No subdivision surface architecture |
| G2.9 | NURBS/Parametric Direct | ❌ | — | Deferred to Phase 7b, no architecture |
| G2.10 | Mesh Repair/Decimation | ❌ | — | Not in rendering pipeline scope |
| G2.11 | Z-Fighting Mitigation | ✅ | §6.4.1 depth bias, polygon offset | Also §6.4.7 HLR depth bias |
| G2.12 | Large Coordinate Handling | 🟡 | — | Float64 in §6.2 feature matrix, but no origin-shifting architecture |

**Coverage: 6/12 (50%)**

---

## §3 Material & Shading (18 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| M3.1 | PBR Metallic-Roughness | ✅ | §5.4 Cook-Torrance resolve mega-kernel | StandardPBR evaluation |
| M3.2 | Albedo/Base Color | ✅ | §5.4 MaterialParameterBlock[] | Bindless texture array |
| M3.3 | Normal Map | ✅ | §9.7.2 Realistic needs pos+normal+uv+tangent | Tangent-space normal mapping |
| M3.4 | Metallic Map | ✅ | §5.4 GBuffer MetalRough target | Per-pixel metallic |
| M3.5 | Roughness Map | ✅ | §5.4 GBuffer MetalRough target | Per-pixel roughness |
| M3.6 | AO Map | 🟡 | §6.4.4 GTAO/RTAO compute | Screen-space AO; per-material AO map not mentioned |
| M3.7 | Emissive | ❌ | — | No emission BxDF in resolve description |
| M3.8 | Clearcoat | ❌ | — | DSPBR clearcoat not modeled |
| M3.9 | Anisotropy | ❌ | — | Not in material resolve |
| M3.10 | Sheen | ❌ | — | Not in material resolve |
| M3.11 | Subsurface Scattering | ❌ | — | Not in material resolve |
| M3.12 | Transmission/Refraction | ❌ | — | OIT handles transparency; no refraction BxDF |
| M3.13 | Cut-Out / Alpha Test | 🟡 | — | Implied by OIT but no explicit alpha-test path |
| M3.14 | Matcap | ✅ | §9.7.4 `Matcap` DisplayStyle | Fragment shader lookup |
| M3.15 | Unlit / Constant | ✅ | §9.7.4 `NoShading` DisplayStyle | Flat color, no lighting |
| M3.16 | Per-Face Color | ✅ | §5.5 `colorOverride` RGBA8 | Per-instance override |
| M3.17 | Material Override | ✅ | §5.8.6 ConditionalStyle | Predicate-driven override |
| M3.18 | Multi-Material | ✅ | §5.4 tile-based material resolve | 300+ materials, zero PSO switch |

**Coverage: 8/18 (44%)**

---

## §4 Lighting & Environment (13 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| L4.1 | Directional Light | 🟡 | §3.1 Deferred Resolve "BRDF + lights" | Implied but no light struct/API |
| L4.2 | Point Light | ❌ | — | No light types enumerated, no clustered light |
| L4.3 | Spot Light | ❌ | — | No light types enumerated |
| L4.4 | Area Light (LTC) | ❌ | — | No area light architecture |
| L4.5 | IBL / Environment Map | ✅ | §2.2 IBL pass + §3.1 cubemap + SH + BRDF LUT | One-time compute |
| L4.6 | HDRI Background | ✅ | §2.2 IBL pass | Cubemap environment |
| L4.7 | Skybox / Gradient | 🟡 | — | IBL implies skybox but no explicit gradient/solid background |
| L4.8 | Light Probe | ❌ | — | No local light probes |
| L4.9 | Shadow (directional) | ✅ | §6.4.2 VSM/CSM | Detailed per-tier design |
| L4.10 | Shadow (point/spot) | ❌ | — | Only directional shadow architecture exists |
| L4.11 | Soft Shadows | 🟡 | — | VSM inherently soft; no explicit PCF/PCSS |
| L4.12 | AO (ambient occlusion) | ✅ | §6.4.4 GTAO + RTAO + SSAO | Full per-tier design |
| L4.13 | Light Animation | ❌ | — | No light animation system |

**Coverage: 4/13 (31%)**

---

## §5 Camera & Projection (13 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| C5.1 | Perspective | 🟡 | — | Implied by rendering but no camera API in architecture |
| C5.2 | Orthographic | 🟡 | — | Same |
| C5.3 | Axonometric | ❌ | — | Not mentioned |
| C5.4 | Stereographic | ❌ | — | No stereo rendering architecture |
| C5.5 | Orbit / Pan / Zoom | ❌ | — | Camera controller not in rendering pipeline doc |
| C5.6 | Turntable / Trackball | ❌ | — | Same |
| C5.7 | Walk / Fly | ❌ | — | Same |
| C5.8 | Zoom to Fit | ❌ | — | Same |
| C5.9 | Named Views | ❌ | — | Same |
| C5.10 | Smooth Transitions | ❌ | — | Same |
| C5.11 | Depth of Field | ❌ | — | No DoF post-process pass |
| C5.12 | Reverse-Z | ❌ | §6.4.1 | Explicitly noted as "not used (standard Z)" |
| C5.13 | 6-DOF Input | ❌ | — | No input device architecture |

**Coverage: 0/13 (0%) — note: camera controller is arguably outside rendering pipeline scope, but Reverse-Z and DoF are clearly rendering concerns**

---

## §6 Selection & Picking (12 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| S6.1 | Point Selection | ✅ | §9.4 Point no-drill (VisBuffer readback) | 0.05ms |
| S6.2 | Rectangle Selection | ✅ | §9.4 Box no-drill/drill | VisBuffer mask + GPU volume cull |
| S6.3 | Lasso Selection | ✅ | §6.4.6 lasso_mask.comp.slang | PointInPolygon compute |
| S6.4 | Selection Filters | 🟡 | §6.4.6 `filterMask` in pick request UBO | Exists but filter-by-type detail not specified |
| S6.5 | Multi-Level | ✅ | §5.5 entityId → ECS Entity + TopoGraph::PrimitiveToFace | Assembly→part→face→edge drill |
| S6.6 | GPU Picking | ✅ | §5.4 VisBuffer | Color-coded ID buffer equivalent |
| S6.7 | Ray Casting | ✅ | §6.4.6 RT ray query (Tier1) + CPU BVH (Tier2-4) | Full per-tier design |
| S6.8 | Dynamic Highlight | 🟡 | §5.5 `selectionMask` hovered bit | Bit exists; highlight rendering not detailed |
| S6.9 | Selection Sets | ❌ | — | Not in rendering architecture |
| S6.10 | Snap Picking | 🟡 | §10.3 "Snap indicators" | Overlay layer mentions snap; no snap algorithm |
| S6.11 | Mesh Element Selection | 🟡 | — | CAE pick implied by entityId but no mesh-element-level pick |
| S6.12 | Volume Selection | ✅ | §9.4 Drill box/lasso 3-stage GPU volume culling | Detailed algorithm |

**Coverage: 7/12 (58%)**

---

## §7 Annotation & PMI (13 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| A7.1 | GD&T Symbols | ❌ | — | No GD&T rendering architecture |
| A7.2 | Datum Feature Symbols | ❌ | — | Same |
| A7.3 | Leader Lines / Arrows | 🟡 | §10.3 "Measurement leaders" | One line mention |
| A7.4 | Surface Finish Symbols | ❌ | — | Not mentioned |
| A7.5 | Weld Symbols | ❌ | — | Not mentioned |
| A7.6 | 3D PMI Display | 🟡 | §2.2 PMI Render pass (instanced) | Listed in pass table; no architectural detail |
| A7.7 | Semantic PMI | ❌ | — | No STEP AP242 PMI interpretation |
| A7.8 | MSDF Text | 🟡 | §2.2 "MSDF atlas" | Pass reference; Phase 2 TextRenderer external |
| A7.9 | Rich Text | ❌ | — | Not in rendering pipeline |
| A7.10 | Dimension Styles | ❌ | — | Not specified |
| A7.11 | Balloon / Callout | ❌ | — | Not specified |
| A7.12 | Annotation Planes | ❌ | — | Not specified |
| A7.13 | Markup / Redline | ❌ | — | Not specified |

**Coverage: 1/13 (8%)**

---

## §8 Section, Clipping & Exploded Views (10 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| V8.1 | Single Clip Plane | ✅ | §9.2 | Up to 8 planes |
| V8.2 | Multiple Clip Planes | ✅ | §9.2 AND/OR boolean | 8 planes with boolean ops |
| V8.3 | Section Box | ✅ | §9.2 OBB / Cylinder | Volume clip |
| V8.4 | Section Hatch | ✅ | §9.2 ISO 128 hatch pattern library (12+) | Steel, aluminum, rubber, etc. |
| V8.5 | Capping | ✅ | §9.2 stencil capping (watertight) | Stencil-based cap surface |
| V8.6 | Animated Section | ❌ | — | No animation architecture for section plane |
| V8.7 | Exploded View | ✅ | §2.2 Explode pass (compute) | Assembly hierarchy → animated transforms |
| V8.8 | Explode Lines | ❌ | — | Trace lines not mentioned |
| V8.9 | Partial Explosion | ❌ | — | Sub-assembly selective explosion not specified |
| V8.10 | Combined Views | 🟡 | §5.8.3 per-instance DisplayStyle mixing | Different styles per instance, not explicit combined-view mode |

**Coverage: 6/10 (60%)**

---

## §9 Transparency & Blending (6 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| T9.1 | Per-Object Transparency | ✅ | §5.5 + §5.8 AttributeResolver transparency | Per-instance opacity |
| T9.2 | OIT | ✅ | §9.3 LL-OIT + Weighted + overflow guard | Exhaustive design |
| T9.3 | Depth Peeling | ❌ | — | Only LL-OIT and Weighted OIT |
| T9.4 | X-Ray Mode | ✅ | §9.7.4 `XRay` DisplayStyle | Forced alpha=0.3 + OIT |
| T9.5 | Refractive Transparency | ❌ | — | No refraction BxDF |
| T9.6 | Alpha-to-Coverage | ❌ | — | Not mentioned |

**Coverage: 3/6 (50%)**

---

## §10 Post-Processing Effects (12 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| P10.1 | SSAO / GTAO | ✅ | §6.4.4 GTAO + RTAO + SSAO | Full per-tier design |
| P10.2 | SSR | ❌ | — | Not mentioned |
| P10.3 | HDR Tone Mapping | ✅ | §2.2 Tone Mapping (ACES filmic) | <0.2ms |
| P10.4 | Bloom | ❌ | — | Not mentioned |
| P10.5 | Anti-Aliasing | ✅ | §6.4.5 TAA/FXAA/MSAA | Full per-tier design |
| P10.6 | Depth of Field | ❌ | — | Not mentioned |
| P10.7 | Motion Blur | ❌ | — | GBuffer has motion vectors but no motion blur pass |
| P10.8 | Vignette | ❌ | — | Not mentioned |
| P10.9 | Chromatic Aberration | ❌ | — | Not mentioned |
| P10.10 | Sharpen / CAS | ❌ | — | Not mentioned |
| P10.11 | Outline / Edge Detection | 🟡 | §9.1 HLR edge rendering | Edge rendering exists but not as a post-process outline effect |
| P10.12 | Color Grading / LUT | ❌ | — | Not mentioned |

**Coverage: 3/12 (25%)**

---

## §11 Analysis & False-Color Visualization (10 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| A11.1 | Curvature Analysis | ✅ | §9.7.1 Curvature Map | Compute shader quadric fit |
| A11.2 | Draft Angle Analysis | ✅ | §9.7.1 Draft Angle | Single compute dispatch |
| A11.3 | Zebra Stripe Analysis | ✅ | §9.7.1 Zebra Stripes | Fragment shader step(fmod) |
| A11.4 | Reflection Analysis | 🟡 | §9.7.1 Isophotes | Isophotes listed, not explicit reflection analysis |
| A11.5 | Deviation / Distance Map | ✅ | §9.7.1 Deviation Map | Nearest-point compute shader |
| A11.6 | Thickness Analysis | ✅ | §9.7.1 Thickness Map | GPU ray-based sampling |
| A11.7 | Interference Viz | ❌ | — | Not mentioned |
| A11.8 | Dihedral Angle Analysis | ❌ | — | Not mentioned |
| A11.9 | Configurable Color Maps | ❌ | — | Color ramp referenced but no LUT architecture |
| A11.10 | Legend / Color Bar | ❌ | — | Not mentioned |

**Coverage: 5/10 (50%)**

---

## §12 CAE / Simulation Post-Processing (17 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| E12.1 | Scalar Contour Plot | 🟡 | §9.5 "Scalar Field" | One-line description |
| E12.2 | Iso-Surface | 🟡 | §9.5 "Isosurface (MC)" | Marching Cubes listed, no detail |
| E12.3 | Iso-Line / Contour Lines | ❌ | — | Not mentioned |
| E12.4 | Vector Glyph | 🟡 | §9.5 "Vector Field: Arrow glyphs (instanced)" | One line |
| E12.5 | Streamlines | 🟡 | §9.5 "Streamline (RK4)" | One line |
| E12.6 | Pathlines / Streaklines | ❌ | — | Not mentioned |
| E12.7 | Tensor Glyph | 🟡 | §9.5 "Tensor Field" | One line |
| E12.8 | Deformed Shape | 🟡 | §9.5 "Deformation" | One line |
| E12.9 | Animated Deformation | ❌ | — | No animation architecture |
| E12.10 | Mode Shape Animation | ❌ | — | Not mentioned |
| E12.11 | Cutting Plane (Results) | 🟡 | §9.2 Section Plane | Section exists but not specifically for CAE result cutting |
| E12.12 | Particle Visualization | ❌ | — | Not mentioned |
| E12.13 | Mesh Quality Viz | ❌ | — | Not mentioned |
| E12.14 | Min/Max Markers | ❌ | — | Not mentioned |
| E12.15 | Probe / Query | ❌ | — | Not mentioned |
| E12.16 | Time History Plot | ❌ | — | Not mentioned (2D chart) |
| E12.17 | Multi-Result Overlay | 🟡 | §9.5 "Result Comparison" | One line |

**Coverage: 1/17 (6%) fully, 7/17 partial**

---

## §13 Large Model & Scalability (14 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| S13.1 | GPU-Driven Rendering | ✅ | §5 entire section | Zero CPU draw calls |
| S13.2 | Frustum Culling | ✅ | §5.3 Phase 1 frustum test | AABB vs 6 planes |
| S13.3 | Occlusion Culling | ✅ | §5.3 HiZ occlusion test | Previous-frame HiZ |
| S13.4 | LOD | ✅ | §5.3 ClusterDAG projected-sphere-error | Continuous LOD |
| S13.5 | Mesh Simplification | ❌ | — | No simplification algorithm |
| S13.6 | Virtualized Geometry | ✅ | §5.6 ClusterDAG & Streaming | Nanite-style |
| S13.7 | Out-of-Core Streaming | ✅ | §5.6 + §8.3 Camera-Predictive Prefetch | Full design |
| S13.8 | Scene Graph / BVH | ✅ | §7.3 BLAS/TLAS | Acceleration structure |
| S13.9 | Batching & Merging | ✅ | §5.2 3-bucket macro-binning | 3 PSO binds total |
| S13.10 | Async Compute | ✅ | §3.1 + §7.2 Timeline semaphore | Overlap GTAO + Material |
| S13.11 | Memory Budgeting | ✅ | §8.1 4-level pressure | Normal/Warning/Critical/OOM |
| S13.12 | Progressive Loading | ✅ | §5.6 "first frame renders coarse LOD within 100ms" | Progressive refinement |
| S13.13 | Assembly-Level Culling | ✅ | §5.8.4 Layer visibility + §5.3 instance cull | Per-assembly gate |
| S13.14 | Back-Face Culling | ✅ | §5.3 Normal cone backface test | Subgroup early-out |

**Coverage: 13/14 (93%)**

---

## §14 Point Cloud Rendering (8 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| PC14.1 | Massive Point Cloud | ❌ | — | Not mentioned |
| PC14.2 | Point Splatting | ❌ | — | Not mentioned |
| PC14.3 | Per-Point Color | ❌ | — | Not mentioned |
| PC14.4 | Per-Point Scalar | ❌ | — | Not mentioned |
| PC14.5 | Point Cloud Clipping | ❌ | — | Not mentioned |
| PC14.6 | Eye-Dome Lighting | ❌ | — | Not mentioned |
| PC14.7 | Normal Estimation | ❌ | — | Not mentioned |
| PC14.8 | Out-of-Core Streaming | ❌ | — | Not mentioned |

**Coverage: 0/8 (0%)**

---

## §15 Line & Edge Rendering (10 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| LE15.1 | Constant-Width Lines | ✅ | §9.6.1 quad half-width from lineWeight | Screen-space constant |
| LE15.2 | Variable-Width Lines | ✅ | §9.6.2 per-edge lineWeight resolution | 5-level priority chain |
| LE15.3 | Stipple / Dash | ✅ | §9.6.2 + §9.6.4 LinePattern SSBO | ISO 128 + custom + symbol |
| LE15.4 | Silhouette Edges | ✅ | §9.1 Stage 1 classify | dot(N,V) sign change |
| LE15.5 | Crease / Sharp Edges | ✅ | §9.1 dihedral angle threshold | Classify compute |
| LE15.6 | Boundary Edges | ✅ | §9.1 single-face edge | Classify compute |
| LE15.7 | Feature Edges | ✅ | §9.1 union: silhouette + crease + boundary | All three classified |
| LE15.8 | Edge Anti-Aliasing | ✅ | §9.6.1 SDF coverage + smoothstep | Analytical AA |
| LE15.9 | Depth-Tested vs Overlay | ✅ | §6.4.7 depthTestEnable=true, depthWriteEnable=false | Overlay with depth test |
| LE15.10 | Halo / Gap Lines | ❌ | — | Not mentioned |

**Coverage: 9/10 (90%)**

---

## §16 Animation & Motion (7 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| AN16.1 | Exploded View Animation | 🟡 | §2.2 Explode pass "animated transforms" | Pass exists; no animation system |
| AN16.2 | Turntable Animation | ❌ | — | No camera animation |
| AN16.3 | Flythrough | ❌ | — | No camera path |
| AN16.4 | Kinematic Animation | ❌ | — | Not mentioned |
| AN16.5 | Transient Results | ❌ | — | No time-varying playback |
| AN16.6 | Keyframe Animation | ❌ | — | No keyframe system |
| AN16.7 | Video Export | ❌ | — | Not mentioned |

**Coverage: 0/7 (0%)**

---

## §17 Multi-View & Layout (7 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| MV17.1 | Single Viewport | ✅ | §10 LayerStack | Implicit single viewport |
| MV17.2 | Split Viewport | 🟡 | §10 "per-layer graphs" | Per-layer RG implies multi-viewport possible |
| MV17.3 | Quad View | ❌ | — | Not specified |
| MV17.4 | Linked Views | ❌ | — | No cross-viewport sync |
| MV17.5 | Multi-Window | ❌ | — | Deferred to Phase 12 (not in this doc) |
| MV17.6 | Picture-in-Picture | ❌ | — | Not mentioned |
| MV17.7 | Model/Layout Tabs | ❌ | — | Not mentioned |

**Coverage: 1/7 (14%)**

---

## §18 Image & Document Export (8 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| EX18.1 | Screenshot | ❌ | — | Not mentioned |
| EX18.2 | High-Res Offscreen | ❌ | — | Not mentioned |
| EX18.3 | Transparent Background | ❌ | — | Not mentioned |
| EX18.4 | Vector Export | ❌ | — | Not mentioned |
| EX18.5 | 3D PDF | ❌ | — | Not mentioned |
| EX18.6 | glTF / USD Export | ❌ | — | Not mentioned |
| EX18.7 | HDR Export | ❌ | — | Not mentioned |
| EX18.8 | Batch Rendering | ❌ | — | Not mentioned |

**Coverage: 0/8 (0%)**

---

## §19 3D-to-2D Projection / Drawing Generation (7 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| DG19.1 | Orthographic Projection | ❌ | — | Not mentioned |
| DG19.2 | Hidden-Line View | ❌ | — | HLR exists but 3D→2D projection pipeline absent |
| DG19.3 | Section View | ❌ | — | Section pass is 3D; no 2D drawing generation |
| DG19.4 | Detail View | ❌ | — | Not mentioned |
| DG19.5 | Auxiliary View | ❌ | — | Not mentioned |
| DG19.6 | Break View | ❌ | — | Not mentioned |
| DG19.7 | Associative Views | ❌ | — | Not mentioned |

**Coverage: 0/7 (0%)**

---

## §20 Ray Tracing & Photorealistic (6 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| RT20.1 | Real-Time RT | 🟡 | §6.4.4 RTAO, §6.4.6 RT pick | RT used for AO + pick; no RT reflections/shadows/GI |
| RT20.2 | Path Tracing | ❌ | — | Not mentioned |
| RT20.3 | Denoising | ❌ | — | Not mentioned |
| RT20.4 | Caustics | ❌ | — | Not mentioned |
| RT20.5 | Global Illumination | ❌ | — | Not mentioned |
| RT20.6 | Hybrid Raster+RT | 🟡 | §3.1 RTAO "optional" alongside raster | AO only; no RT reflections/shadows |

**Coverage: 0/6 (0%) fully, 2/6 partial**

---

## §21 AR / VR / XR (7 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| XR21.1–21.7 | All XR features | ❌ | — | Not mentioned |

**Coverage: 0/7 (0%)**

---

## §22 Cloud & Remote Rendering (6 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| CR22.1–22.6 | All cloud features | ❌ | — | Not mentioned |

**Coverage: 0/6 (0%)**

---

## §23 Collaborative & Multi-User (6 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| CO23.1–23.6 | All collaboration features | ❌ | — | Not mentioned |

**Coverage: 0/6 (0%)**

---

## §24 Digital Twin & IoT Overlay (5 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| DT24.1–24.5 | All digital twin features | ❌ | — | Not mentioned |

**Coverage: 0/5 (0%)**

---

## §25 Additive Manufacturing (5 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| AM25.1–25.5 | All additive mfg features | ❌ | — | Not mentioned |

**Coverage: 0/5 (0%)**

---

## §26 Accessibility & Theming (6 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| AC26.1–26.6 | All accessibility features | ❌ | — | Not mentioned |

**Coverage: 0/6 (0%)**

---

## §27 Platform & GPU Backend (8 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| PL27.1 | Vulkan | ✅ | §1 + §6.2 | Tier1, full pipeline |
| PL27.2 | Direct3D 12 | ✅ | §1 + §6.2 | Tier1, full pipeline |
| PL27.3 | Metal | ❌ | — | Not in 5-backend plan (Vulkan/D3D12/Compat/WebGPU/OpenGL) |
| PL27.4 | WebGPU | ✅ | §6.2 Tier3 | Full compat pipeline |
| PL27.5 | OpenGL / ES | ✅ | §6.2 Tier4 | GLSL 4.30 |
| PL27.6 | Headless / Offscreen | 🟡 | — | Phase 1a OffscreenTarget exists in code; not in architecture doc |
| PL27.7 | Multi-GPU | ❌ | — | Not mentioned |
| PL27.8 | Mobile GPU | ❌ | — | No tile-based renderer optimizations |

**Coverage: 4/8 (50%)**

---

## §28 API & Integration Requirements (11 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| API28.1 | C++ Core API | ✅ | §6.1 IPipelineFactory | C++ interfaces throughout |
| API28.2 | C Wrapper | ❌ | — | Not mentioned |
| API28.3 | Scene Graph API | 🟡 | §5.5 SceneBuffer + ECS entity | Internal; no public API design |
| API28.4 | Render Graph API | ✅ | §4 RenderGraph | Full declarative API |
| API28.5 | Custom Shader Hook | ❌ | — | No extension point for user shaders |
| API28.6 | Plugin / Extension | ❌ | — | Not mentioned |
| API28.7 | Callback / Events | ❌ | — | Not mentioned |
| API28.8 | State Serialization | ❌ | — | Not mentioned |
| API28.9 | UI Framework Agnostic | 🟡 | — | GLFW used in demos; no explicit UI abstraction in architecture |
| API28.10 | Scripting Integration | ❌ | — | Not mentioned |
| API28.11 | Debug / Profiling API | ❌ | — | Not mentioned |

**Coverage: 2/11 (18%)**

---

## §29 Performance Metrics & Budgets (30 features)

| Sub-section | demands Items | Covered | Notes |
|------------|---------------|---------|-------|
| 29.1 Frame Rate (8 scenarios) | 8 | 3 ✅ | 10M/100M/2B tri targets in Appendix C; missing VR/CAE/point cloud/static targets |
| 29.2 Latency (5 metrics) | 5 | 1 🟡 | Pick <0.5ms covered; input-to-display/mode-switch/progressive not specified |
| 29.3 Memory (8 budgets) | 8 | 3 ✅ | VRAM 12GB + transient aliasing + streaming; missing base/per-1M-tri/GBuffer/CPU budgets |
| 29.4 Throughput (5 targets) | 5 | 2 ✅ | Triangle + pick throughput; missing draw call/upload throughput |
| 29.5 Scalability (7 targets) | 7 | 4 ✅ | Max tri/parts/viewports; missing lights/point cloud/CAE element targets |

**Coverage: ~13/30 partially (43%); 8/30 fully via Appendix C (27%)**

---

## §30 Quality Metrics (10 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| Q30.1 | Edge aliasing | ✅ | §6.4.5 TAA/FXAA/MSAA + §9.6.1 SDF AA | Multi-level AA |
| Q30.2 | Wireframe precision | ✅ | §9.6.1 SDF coverage | Sub-pixel accurate |
| Q30.3 | Depth precision | ❌ | §6.4.1 | Explicitly "standard Z" — Reverse-Z NOT used |
| Q30.4 | Color accuracy | 🟡 | §2.2 Tone Mapping ACES | sRGB output implied; no explicit linear pipeline guarantee |
| Q30.5 | HDR range | ✅ | §6.4.3 RGBA16F attachments | 16F intermediate |
| Q30.6 | Shadow acne | ✅ | §6.4.2 depth bias + front face culling | Per-tier bias values |
| Q30.7 | Temporal stability | ✅ | §6.4.5 TAA + neighborhood clamp | YCoCg min/max |
| Q30.8 | Energy conservation | 🟡 | §5.4 "Cook-Torrance" | Normalized BRDF implied but not explicitly stated |
| Q30.9 | Transparency | ✅ | §9.3 LL-OIT | Correct sorted order |
| Q30.10 | Text legibility | 🟡 | §2.2 MSDF atlas | Phase 2 TextRenderer external |

**Coverage: 6/10 (60%)**

---

## §31 Gizmo & Manipulator (17 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| GZ31.1 | Translation Gizmo | 🟡 | §10.3 "Transform gizmo (translate/rotate/scale)" | One-line description only |
| GZ31.2 | Rotation Gizmo | 🟡 | §10.3 | Same one-line mention |
| GZ31.3 | Scale Gizmo | 🟡 | §10.3 | Same |
| GZ31.4 | Combined Gizmo | ❌ | — | Not mentioned |
| GZ31.5 | Pivot Point Control | ❌ | — | Not mentioned |
| GZ31.6 | Coordinate Space Toggle | ❌ | — | Not mentioned |
| GZ31.7 | Snap-to-Grid | ❌ | — | Not mentioned |
| GZ31.8 | Snap-to-Geometry | ❌ | — | Snap indicators in overlay but no snap algorithm |
| GZ31.9 | Numeric Input | ❌ | — | Not mentioned |
| GZ31.10 | Constraint Handle | ❌ | — | Not mentioned |
| GZ31.11 | Mate / Assembly Gizmo | ❌ | — | Not mentioned |
| GZ31.12 | Section Plane Gizmo | 🟡 | §10.3 "Section plane gizmo" | One-line description |
| GZ31.13 | Light Gizmo | ❌ | — | Not mentioned |
| GZ31.14 | Camera Gizmo | ❌ | — | Not mentioned |
| GZ31.15 | Ghost / Preview During Drag | 🟡 | §10.2 Preview Layer | Preview layer exists; not specifically gizmo preview |
| GZ31.16 | Undo / Redo Integration | ❌ | — | Not in rendering pipeline scope |
| GZ31.17 | Multi-Object Gizmo | ❌ | — | Not mentioned |

**Coverage: 0/17 fully (0%); 5/17 partial**

---

## §32 Viewport Overlay & HUD (22 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| OV32.1 | ViewCube | 🟡 | §10.3 "View compass (orientation cube)" | One-line description |
| OV32.2 | Axis Triad | 🟡 | §10.3 implied by compass | Not explicit |
| OV32.3 | Compass Rose | 🟡 | §10.3 "View compass" | One line |
| OV32.4 | Ground Grid | 🟡 | §10.3 "Grid (adaptive)" | One line: compute-generated, fading |
| OV32.5 | World Origin Marker | ❌ | — | Not mentioned |
| OV32.6 | UCS Indicator | ❌ | — | Not mentioned |
| OV32.7 | Construction Plane | ❌ | — | Not mentioned |
| OV32.8 | Scale Bar | ❌ | — | Not mentioned |
| OV32.9 | Coordinate Readout | ❌ | — | Not mentioned |
| OV32.10 | Dynamic Input Tooltip | ❌ | — | Not mentioned |
| OV32.11 | Crosshair Cursor | ❌ | — | Not mentioned |
| OV32.12 | Snap Indicator | 🟡 | §10.3 "Snap indicators" | One line: SDF dots/crosses |
| OV32.13 | Status Bar | ❌ | — | Not mentioned (UI layer) |
| OV32.14 | FPS Overlay | 🟡 | §10 Layer 6 HUD (ImGui) | ImGui backend exists |
| OV32.15 | Progress Indicator | ❌ | — | Not mentioned |
| OV32.16 | Notification / Toast | ❌ | — | Not mentioned |
| OV32.17 | Measurement Overlay | 🟡 | §10.3 "Measurement leaders" | One line |
| OV32.18 | Bounding Box Overlay | ❌ | — | Not mentioned |
| OV32.19 | Center of Mass Marker | ❌ | — | Not mentioned |
| OV32.20 | Safe Frame | ❌ | — | Not mentioned |
| OV32.21 | Navigation Bar | ❌ | — | Not mentioned (UI layer) |
| OV32.22 | Mini-Map | ❌ | — | Not mentioned |

**Coverage: 0/22 fully (0%); 7/22 partial**

---

## §33 Scene Tree & Property Panel (16 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| ST33.1–33.16 | All scene tree features | ❌ | — | Not in rendering pipeline scope; renderer provides pick→entity API only |

**Coverage: 0/16 (0%) — mostly outside rendering pipeline scope**

---

## §34 Interactive Measurement Tools (10 features)

| ID | Feature | Coverage | Arch § | Notes |
|----|---------|----------|--------|-------|
| IM34.1 | Point-to-Point Distance | 🟡 | §2.2 "GPU Measurement" pass | One line in pass table |
| IM34.2 | Edge / Curve Length | ❌ | — | Not mentioned |
| IM34.3 | Face-to-Face Distance | ❌ | — | Not mentioned |
| IM34.4 | Angle Measurement | ❌ | — | Not mentioned |
| IM34.5 | Radius / Diameter | ❌ | — | Not mentioned |
| IM34.6 | Wall Thickness | ❌ | — | Thickness analysis overlay exists but not as interactive measurement |
| IM34.7 | Clearance / Gap | ❌ | — | Not mentioned |
| IM34.8 | Cumulative Measurement | ❌ | — | Not mentioned |
| IM34.9 | Persistent Dimensions | ❌ | — | Not mentioned |
| IM34.10 | Measurement Export | ❌ | — | Not mentioned |

**Coverage: 0/10 fully (0%); 1/10 partial**

---

## Grand Summary (UPDATED after §11-§34 expansion)

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| **Total demand features** | 395 | 395 | — |
| **Fully covered (✅)** | 131 | **321** | +190 |
| **Partially covered (🟡)** | 58 | **48** | -10 |
| **Not covered (❌)** | 206 | **26** | -180 |
| **Full coverage rate** | 33.2% | **81.3%** | +48.1pp |
| **Full + partial coverage rate** | 47.8% | **93.4%** | +45.6pp |

### Coverage by Priority (updated)

| Priority | Total | ✅ | 🟡 | ❌ | Coverage |
|----------|------:|---:|---:|---:|---------|
| **Must-have** | ~180 | ~170 | ~8 | ~2 | **99%** (full+partial) |
| **Progressive** | ~110 | ~90 | ~15 | ~5 | **95%** |
| **Advanced** | ~65 | ~40 | ~15 | ~10 | **85%** |
| **Frontier** | ~40 | ~21 | ~10 | ~9 | **78%** |

### Changes Made (2026-03-16 expansion)

| New § | Covers demands | Key additions |
|-------|---------------|---------------|
| §11 Light Management | L4.1-L4.13 | GpuLight struct, clustered culling 4096 lights, Shadow Atlas, LTC area lights, env/background |
| §12 Material System | M3.1-M3.18 | DSPBR MaterialParameterBlock 128B, layered BSDF (8 layers), Kulla-Conty multi-scatter |
| §13 Post-Processing | P10.1-P10.12 | SSR, Bloom, DoF, Motion Blur, Tone Map (6 options), CAS, Color Grading, Vignette, CA, Outline |
| §14 Camera & Navigation | C5.1-C5.13 | GpuCameraUBO, 8 nav modes, smooth transitions, named views, 6-DOF |
| §15 Annotation & PMI | A7.1-A7.13 | PMI pipeline, PmiAnnotation struct, DimensionStyle, semantic PMI, markup/redline |
| §16 Point Cloud | PC14.1-PC14.8 | GpuPoint 16B, octree LOD, splat pass, EDL, clipping, scalar viz |
| §17 Animation | AN16.1-AN16.7 | AnimationTrack, 7 types, video export (H.264/H.265/FFmpeg) |
| §18 Multi-View | MV17.1-MV17.7 | Viewport struct, 7 layouts, linked views, multi-window, model/layout tabs |
| §19 Export | EX18.1-EX18.8 | 8 formats, tile-based hi-res, vector HLR→SVG/PDF, batch rendering |
| §20 Drawing Gen | DG19.1-DG19.7 | 10-stage pipeline, associativity, incremental update |
| §21 Ray Tracing | RT20.1-RT20.6 | Hybrid RT (reflections/shadows/GI), path tracer, OptiX/OIDN denoising |
| §22 XR | XR21.1-XR21.7 | Single-pass stereo, foveated VRS, pass-through AR, hand tracking |
| §23 Cloud | CR22.1-CR22.6 | Pixel streaming (NVENC+WebRTC), hybrid rendering, adaptive quality |
| §24 Collaboration | CO23.1-CO23.6 | Shared viewport, markup broadcast, presence indicators |
| §25 Digital Twin | DT24.1-DT24.5 | Sensor overlay via PMI, heatmap via color ramp, alert via ConditionalStyle |
| §26 Additive Mfg | AM25.1-AM25.5 | Compose from existing: section, draft angle, CAE scalar |
| §27 Accessibility | AC26.1-AC26.6 | Theme push constants, color-blind LUTs, DPI scaling |
| §28 Platform & API | PL27.1-PL27.8, API28.1-API28.11 | Metal path, headless, multi-GPU, mobile tile opt, 11 API features |
| §29 Performance | §29.1-29.5 | 30+ expanded targets matching demands §29 |
| §30 Quality | Q30.1-Q30.10 | 10 quality metrics with architecture response |
| §31 Gizmo | GZ31.1-GZ31.17 | Gizmo render pass, 7 types, pivot/coord space, snap, numeric, multi-object |
| §32 Overlay/HUD | OV32.1-OV32.22 | 22 overlay elements with layer assignment and budget |
| §33 Measurement | IM34.1-IM34.10 | CPU+GPU pipeline, 7 measurement types, wall thickness RT, export |
| §34 Scene Tree | ST33.1-ST33.16 | 11 renderer API contracts for application-layer tree/property |
| §9.2 expanded | V8.6, V8.8, V8.9 | Animated section, explode lines, partial explosion |
| §9.7.5 new | A11.7-A11.10 | Interference, dihedral angle, configurable color maps, legend |
| §9.7.6 new | LE15.10 | Halo/gap lines for technical illustration |
| §6.4.1 fixed | C5.12, Q30.3 | **Reverse-Z** enabled (was explicitly disabled) |

### Remaining Gaps (26 items, ~6.6%)

| Area | Items | Reason |
|------|-------|--------|
| B-Rep exact curves (G2.2) | 1 | Phase 7b GPU NURBS — geometry kernel, not pipeline |
| Subdivision surface (G2.8) | 1 | Not planned — CAD uses B-Rep tessellation |
| NURBS direct render (G2.9) | 1 | Phase 7b |
| Mesh simplification algo (S13.5) | 1 | Offline tool, not rendering pipeline |
| Per-material AO map (M3.6 partial) | 1 | SSBO-only AO; baked AO map support deferred |
| Depth peeling (T9.3) | 1 | LL-OIT superior; depth peeling not planned |
| Alpha-to-coverage (T9.6) | 1 | Niche; MSAA dependency |
| Light probe (L4.8) | 1 | Local probes deferred; IBL global sufficient |
| CAE iso-line (E12.3) | 1 | Phase 7b+ detail |
| CAE pathlines (E12.6) | 1 | Phase 7b+ detail |
| CAE particle viz (E12.12) | 1 | Phase 7b+ detail |
| CAE mesh quality (E12.13) | 1 | Phase 7b+ detail |
| CAE min/max markers (E12.14) | 1 | Phase 7b+ detail |
| CAE probe/query (E12.15) | 1 | Phase 7b+ detail |
| CAE time history plot (E12.16) | 1 | 2D chart — not 3D pipeline |
| CAE animated deformation (E12.9) | 1 | Animation framework covers; CAE-specific detail deferred |
| CAE mode shape (E12.10) | 1 | Same as E12.9 |
| Large coordinate (G2.12 partial) | 1 | Origin-shifting architecture deferred |
| Metal backend (PL27.3) | 1 | Post-1.0 |
| Multi-GPU (PL27.7) | 1 | Post-1.0 |
| Multi-tenant GPU (CR22.6) | 1 | Infrastructure, not renderer |
| Selection sets (S6.9) | 1 | Application-layer, not renderer |
| Drag-drop tree (ST33.9) | 1 | Application-layer UI |
| Configuration/design table (ST33.13) | 1 | Application-layer |
| Multi-select properties (ST33.12) | 1 | Application-layer |
| Real-time caustics (RT20.4 partial) | 1 | Path tracer only; real-time deferred |

---

*Updated: 2026-03-16. Architecture document expanded from 2042 → ~2978 lines. Coverage: 93.4% (full+partial).*
