# CAD/CAE Renderer External Feature & Performance Demands

> **Scope**: Exhaustive enumeration of features and performance metrics a production CAD/CAE
> renderer must expose. Technology-agnostic; implementation details omitted unless frontier
> technique is the only viable path.
>
> **Methodology**: 5-round iterative research across HOOPS Visualize/Envision, OCCT, Dassault
> 3DEXPERIENCE (Enterprise PBR), Google Filament, Rhino, SolidWorks, CATIA, NX, Creo, Onshape,
> Abaqus/CAE, ANSYS, COMSOL, ParaView/VTK, UE5 Nanite, and 30+ academic/industry sources
> (2024-2025).
>
> **Audience**: miki renderer roadmap planners, API designers, QA engineers.

---

## 1. Display Modes & Visual Styles

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| D1.1 | **Wireframe** | All edges visible; no face fill; per-edge color/width/stipple. | AutoCAD, Rhino, CATIA, NX |
| D1.2 | **Hidden-Line Removal (HLR)** | Occluded edges dashed or invisible. CPU or GPU path. | OCCT StdPrs, AutoCAD, SolidWorks |
| D1.3 | **Flat Shading** | Per-face flat shading with face normals. | All CAD |
| D1.4 | **Smooth Shading** | Interpolated vertex normals (Gouraud/Phong). | All CAD |
| D1.5 | **Shaded with Edges** | Smooth shading + silhouette/crease/boundary edge overlay. Most-used CAD mode. | SolidWorks, CATIA, NX |
| D1.6 | **Shaded with Wireframe** | Smooth shading + full wireframe overlay. | Rhino, Onshape |
| D1.7 | **Realistic / PBR** | PBR with environment lighting, shadows, reflections, AO. | SolidWorks Visualize, CATIA 3DX |
| D1.8 | **X-Ray / Ghost** | Semi-transparent for internal structure visibility. | SolidWorks, Fusion 360, NX |
| D1.9 | **NPR / Illustration** | Tonal art, cartoon shading, technical illustration. | Autodesk, Rhino Arctic |
| D1.10 | **Monochrome / Clay** | Single-color shading for form study. | Rhino Clay, Blender MatCap |
| D1.11 | **Per-Part Override** | Individual parts in assembly with different display modes. | All major CAD |
| D1.12 | **Custom Visual Styles** | User-definable edge+face+transparency+effect combos. | AutoCAD, Rhino |
| D1.13 | **Mode Transition** | Smooth animated transition between display modes. | Onshape |

---

## 2. Geometric Rendering

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| G2.1 | **B-Rep Tessellation** | Adaptive tessellation (chord height, angular deflection). | OCCT, HOOPS, Parasolid |
| G2.2 | **Exact Curve Rendering** | NURBS/splines/circles with sub-pixel accuracy, resolution-independent. | OCCT, Rhino |
| G2.3 | **Iso-Parameter Lines** | Iso-u/v lines on NURBS surfaces. | Rhino, CATIA, NX |
| G2.4 | **Mesh Display** | Tri/quad mesh elements with per-element/node attributes. | All FEA tools |
| G2.5 | **Entity Highlighting** | Vertex/edge/face highlight on hover/selection. | All CAD |
| G2.6 | **Surface Continuity Viz** | G0/G1/G2/G3 continuity feedback. | Rhino, Alias |
| G2.7 | **Tessellation LOD** | Multiple levels per shape; runtime screen-error switching. | HOOPS, OCCT |
| G2.8 | **Instanced Rendering** | GPU instancing for repeated geometry. | All modern renderers |
| G2.9 | **Double-Sided Rendering** | Back faces with different color/material for open shells. | SolidWorks, NX, OCCT |
| G2.10 | **Z-Fighting Prevention** | Polygon offset, stencil for coincident geometry. | OCCT, all CAD |
| G2.11 | **Curve-on-Surface** | Trimming curves, UV annotations on faces. | CATIA, NX, Rhino |
| G2.12 | **Subdivision Surfaces** | Catmull-Clark/Loop with crease control. | Fusion 360, Rhino SubD |

---

## 3. Material & Shading System

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| M3.1 | **Metallic-Roughness PBR** | Cook-Torrance GGX; albedo, metallic, roughness, normal, AO maps. | glTF 2.0, DSPBR, Filament |
| M3.2 | **Specular-Glossiness PBR** | Alternative workflow for legacy libraries. | glTF extension |
| M3.3 | **Clearcoat** | Extra specular layer (car paint, lacquer). | DSPBR, Filament |
| M3.4 | **Anisotropy** | Directional roughness (brushed metal). | DSPBR, Filament |
| M3.5 | **Sheen** | Rim lighting for fabrics. | DSPBR, glTF extension |
| M3.6 | **Subsurface Scattering** | Translucent materials (wax, jade, plastic). | DSPBR Volume |
| M3.7 | **Transmission / Refraction** | Glass, water with IOR and Beer-Lambert absorption. | DSPBR, glTF extension |
| M3.8 | **Emission** | Self-illuminating surfaces. Luminous flux/emittance. | DSPBR |
| M3.9 | **Cut-Out / Alpha Mask** | Binary transparency for perforated materials, decals. | DSPBR |
| M3.10 | **Normal Mapping** | Tangent-space bump detail. | Universal |
| M3.11 | **Displacement Mapping** | Vertex displacement for true geometry. | SolidWorks Visualize |
| M3.12 | **Texture Projection** | UV, triplanar, box/sphere/cylinder, decal projection. | All visualizers |
| M3.13 | **Procedural Materials** | Shader-generated noise, checker, wood, carbon fiber. | KeyShot, Blender |
| M3.14 | **Multi-Layer Materials** | Stacked layers (base + coat + weathering + decal). | DSPBR, Substance |
| M3.15 | **Material Library** | Predefined physically-accurate presets. | SolidWorks, CATIA, KeyShot |
| M3.16 | **Thin-Film Iridescence** | Anodized aluminum, oil slick. | glTF extension |
| M3.17 | **Specular Tint** | Non-physical specular coloring for NPR. | DSPBR |
| M3.18 | **Unlit / Constant** | Pure color, no lighting (CAE false-color, overlays). | glTF extension |

---

## 4. Lighting & Environment

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| L4.1 | **Directional Light** | Infinite parallel light + shadow. | All renderers |
| L4.2 | **Point Light** | Omnidirectional with inverse-square falloff. | All renderers |
| L4.3 | **Spot Light** | Cone-shaped with inner/outer angle. | All renderers |
| L4.4 | **Area Light** | Rect/disc/sphere for soft shadows (LTC or RT). | KeyShot, Filament, UE5 |
| L4.5 | **IBL** | HDRI → pre-filtered specular + diffuse irradiance SH L2. | All modern PBR |
| L4.6 | **Skybox / Skydome** | Cubemap, equirectangular, procedural sky background. | All 3D viewers |
| L4.7 | **Gradient / Solid Background** | Configurable 2-color gradient or solid. | AutoCAD, SolidWorks |
| L4.8 | **Ground Plane / Shadow Catcher** | Infinite plane with shadow/reflection + grid. | SolidWorks, KeyShot |
| L4.9 | **Multiple Lights** | 8-256 direct + IBL simultaneously. | All renderers |
| L4.10 | **Light Presets** | Pre-configured rigs (studio, outdoor, neutral). | SolidWorks Visualize |
| L4.11 | **Headlight / Camera Light** | Light attached to camera for constant illumination. | OCCT, AutoCAD, NX |
| L4.12 | **Shadow Quality Levels** | Configurable resolution, cascades, softness, bias. | All modern renderers |
| L4.13 | **Environment Rotation** | Rotate HDRI independently of geometry. | KeyShot |

---

## 5. Camera & Projection

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| C5.1 | **Perspective** | Configurable FOV, near/far. | Universal |
| C5.2 | **Orthographic** | Parallel projection for engineering views. | All CAD |
| C5.3 | **Axonometric** | Isometric / dimetric / trimetric. | AutoCAD, SolidWorks, NX |
| C5.4 | **Stereographic** | Stereo pair for VR/AR. | OCCT, XR |
| C5.5 | **Orbit / Pan / Zoom** | Standard navigation with inertia. | All 3D viewers |
| C5.6 | **Turntable / Trackball** | Multiple orbit modes (Y-up vs unconstrained). | Rhino, Blender |
| C5.7 | **Walk / Fly** | First-person for architectural/plant viz. | Revit, Navisworks |
| C5.8 | **Zoom to Fit / Selection** | Auto-framing. | All CAD |
| C5.9 | **Named Views** | Save/restore camera bookmarks. | All CAD |
| C5.10 | **Smooth Transitions** | Animated slerp+lerp between views. | Onshape |
| C5.11 | **Depth of Field** | Aperture-based DoF for photorealistic output. | KeyShot |
| C5.12 | **Reverse-Z** | [1,0] depth for precision at distance. | UE5, Filament |
| C5.13 | **6-DOF Input** | SpaceMouse/3Dconnexion. | All professional CAD |

---

## 6. Selection & Picking

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| S6.1 | **Point Selection** | Single entity under cursor. | Universal |
| S6.2 | **Rectangle Selection** | Window/crossing rectangle. | AutoCAD, SolidWorks |
| S6.3 | **Lasso Selection** | Arbitrary polygon region. | Rhino, Blender |
| S6.4 | **Selection Filters** | By type: vertex/edge/face/body/component/datum/annotation. | All CAD, ANSYS |
| S6.5 | **Multi-Level** | Assembly→part→body→face→edge→vertex drill-down. | SolidWorks, CATIA, NX |
| S6.6 | **GPU Picking** | Color-coded ID buffer for O(1) pixel-accurate pick. | HOOPS, modern renderers |
| S6.7 | **Ray Casting** | CPU BVH-accelerated intersection. | OCCT, all CAD |
| S6.8 | **Dynamic Highlight** | Auto highlight on hover (pre-selection). | OCCT AIS, SolidWorks |
| S6.9 | **Selection Sets** | Named sets, add/remove/toggle. | All professional CAD |
| S6.10 | **Snap Picking** | Snap to vertex/edge/midpoint/center. | AutoCAD OSNAP |
| S6.11 | **Mesh Element Selection** | Nodes, elements for CAE. | ANSYS, Abaqus |
| S6.12 | **Volume Selection** | Entities inside 3D region. | ANSYS, Abaqus |

---

## 7. Annotation, PMI & Text Rendering

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| A7.1 | **3D PMI Display** | GD&T, dimensions, datum targets, notes in 3D. | STEP AP242, SolidWorks MBD, NX |
| A7.2 | **Leader Lines** | Dimension leaders with arrowheads. | All CAD dimensioning |
| A7.3 | **Tolerance Stacks** | Bilateral/unilateral/limit per ASME Y14.5 / ISO GPS. | SolidWorks MBD, CATIA FTA |
| A7.4 | **GD&T Frames** | Feature control frames. | ASME Y14.5, ISO 1101 |
| A7.5 | **Surface Finish Symbols** | Ra/Rz callouts per ISO 1302. | NX, CATIA |
| A7.6 | **Screen-Space Text** | HUD overlays always facing camera. | All viewers |
| A7.7 | **World-Space Text** | 3D-anchored, perspective + occlusion. | All CAD |
| A7.8 | **MSDF Text** | Resolution-independent SDF text. | miki T2.7.5, Valve |
| A7.9 | **Rich Text** | Mixed fonts/sizes/bold/italic/sub-super. | AutoCAD MTEXT |
| A7.10 | **Dimension Styles** | Arrowheads, extension lines, text placement. | All CAD |
| A7.11 | **Balloon / Callout** | Part number BOM balloons. | SolidWorks, NX |
| A7.12 | **Annotation Planes** | PMI by view direction. | STEP AP242 |
| A7.13 | **Markup / Redline** | Freehand, arrows, text overlay for review. | HOOPS Communicator |

---

## 8. Section, Clipping & Exploded Views

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| V8.1 | **Single Clip Plane** | Arbitrary plane + optional section fill. | All CAD, OCCT |
| V8.2 | **Multiple Clip Planes** | 6+ simultaneous with boolean ops. | Rhino, CATIA, HOOPS |
| V8.3 | **Section Box** | 6-plane AABB clip. | Revit, Navisworks |
| V8.4 | **Section Hatch** | Cross-hatch/solid fill on cut faces. | AutoCAD, SolidWorks |
| V8.5 | **Capping** | Cap surface on section cut. | OCCT, SolidWorks |
| V8.6 | **Animated Section** | Smooth clip plane animation. | Navisworks, HOOPS |
| V8.7 | **Exploded View** | Auto/manual explosion along axes. | SolidWorks, NX, CATIA |
| V8.8 | **Explode Lines** | Original-to-exploded trace lines. | SolidWorks, NX |
| V8.9 | **Partial Explosion** | Explode selected sub-assemblies only. | SolidWorks, NX |
| V8.10 | **Combined Views** | Section + wireframe + shaded + exploded. | CAD Exchanger, Rhino |

---

## 9. Transparency & Blending

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| T9.1 | **Per-Object Transparency** | Configurable opacity 0-100% per part/face. | All CAD |
| T9.2 | **Order-Independent Transparency (OIT)** | Correct without sorting: weighted blended, linked-list, or moment-based. | NVIDIA samples, HOOPS, VTK |
| T9.3 | **Depth Peeling** | Exact multi-layer transparency (quality vs performance). | VTK, ParaView |
| T9.4 | **X-Ray Mode** | Global semi-transparency for assembly interior. | SolidWorks, Fusion 360 |
| T9.5 | **Refractive Transparency** | PBR glass with refraction. | DSPBR, Filament |
| T9.6 | **Alpha-to-Coverage** | MSAA-based for vegetation/perforated. | Standard GPU |

---

## 10. Post-Processing Effects

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| P10.1 | **SSAO** | Contact shadows in concavities. GTAO/HBAO+. | VTK, SolidWorks, Kitware |
| P10.2 | **SSR** | Screen-space reflections from depth+color. | UE5, modern viewers |
| P10.3 | **HDR Tone Mapping** | ACES, Reinhard, exposure control. | Universal PBR |
| P10.4 | **Bloom** | Bright region bleed for emissives/specular. | KeyShot, UE5 |
| P10.5 | **Anti-Aliasing** | MSAA, FXAA, TAA, SMAA. Configurable quality. | All modern renderers |
| P10.6 | **Depth of Field** | Post-process bokeh blur. | KeyShot, SolidWorks Visualize |
| P10.7 | **Motion Blur** | Per-pixel for animation/turntable. | KeyShot, UE5 |
| P10.8 | **Vignette** | Edge darkening for photographic feel. | KeyShot, Sketchfab |
| P10.9 | **Chromatic Aberration** | Lens color fringing (optional). | KeyShot |
| P10.10 | **Sharpen / CAS** | Contrast-adaptive sharpening post-TAA. | AMD FidelityFX |
| P10.11 | **Outline / Edge Detection** | Sobel/Roberts for illustration style. | VTK, Rhino Arctic |
| P10.12 | **Color Grading / LUT** | 3D LUT or curves correction. | Filament, UE5 |

---

## 11. Analysis & False-Color Visualization

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| A11.1 | **Curvature Analysis** | Gaussian/mean/min/max curvature false-color. | Rhino, Onshape, CATIA |
| A11.2 | **Draft Angle Analysis** | Color map vs pull direction for mold design. | SolidWorks, Creo, Onshape |
| A11.3 | **Zebra Stripe Analysis** | Reflected stripes for G1/G2 continuity. | Rhino, NX, CATIA |
| A11.4 | **Reflection Analysis** | Continuous environment reflection for surface quality. | Rhino, Alias |
| A11.5 | **Deviation / Distance Map** | Scan-to-CAD or surface-to-surface deviation. | GOM Inspect, Onshape |
| A11.6 | **Thickness Analysis** | Wall thickness via ray-cast. | SolidWorks, Onshape |
| A11.7 | **Interference Visualization** | Highlight interfering volumes. | SolidWorks, NX, Navisworks |
| A11.8 | **Dihedral Angle Analysis** | Adjacent face angle color map. | Onshape |
| A11.9 | **Configurable Color Maps** | Rainbow, viridis, plasma, diverging, sequential transfer functions. | ParaView |
| A11.10 | **Legend / Color Bar** | Auto-generated value-to-color legend. | All analysis tools |

---

## 12. CAE / Simulation Post-Processing

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| E12.1 | **Scalar Contour Plot** | Stress/temperature/pressure false-color on mesh. | Abaqus, ANSYS, COMSOL |
| E12.2 | **Iso-Surface** | Constant-value surfaces from volume fields. | ParaView, ANSYS CFD-Post |
| E12.3 | **Iso-Line / Contour Lines** | Constant-value lines on mesh surfaces. | COMSOL, Abaqus |
| E12.4 | **Vector Glyph** | Scaled/colored arrows for velocity/force fields. | ParaView, ANSYS |
| E12.5 | **Streamlines** | Particle paths through steady-state vector fields. | ParaView, ANSYS Fluent |
| E12.6 | **Pathlines / Streaklines** | Transient flow visualization. | ANSYS Fluent |
| E12.7 | **Tensor Glyph** | Ellipsoid/superquadric for stress/strain tensors. | ParaView, Abaqus |
| E12.8 | **Deformed Shape** | Deformed mesh overlaid on undeformed (scale factor). | Abaqus, ANSYS, COMSOL |
| E12.9 | **Animated Deformation** | Time-stepping deformation playback. | Abaqus, ANSYS |
| E12.10 | **Mode Shape Animation** | Eigenmode vibration animation. | All modal analysis |
| E12.11 | **Cutting Plane (Results)** | Section through volume results with contour on cut face. | ANSYS, ParaView |
| E12.12 | **Particle Visualization** | DEM/SPH particles with per-particle scalars. | Rocky, ParaView |
| E12.13 | **Mesh Quality Viz** | Element quality false-color (aspect ratio, skewness, Jacobian). | ANSYS, Abaqus |
| E12.14 | **Min/Max Markers** | Extreme-value location markers. | Abaqus, ANSYS |
| E12.15 | **Probe / Query** | Click to display interpolated value. | All CAE post-processors |
| E12.16 | **Time History Plot** | 2D XY chart alongside 3D view. | Abaqus, ANSYS |
| E12.17 | **Multi-Result Overlay** | Multiple fields simultaneously. | ANSYS, ParaView |

---

## 13. Large Model & Scalability

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| S13.1 | **GPU-Driven Rendering** | Indirect draws, GPU scene submission, compute culling. | UE5 Nanite, The Forge |
| S13.2 | **Frustum Culling** | Per-object AABB/OBB test (CPU or GPU). | Universal |
| S13.3 | **Occlusion Culling** | HZB or GPU queries for hidden rejection. | UE5, HOOPS |
| S13.4 | **LOD** | Multiple mesh LODs with screen-error switching. | HOOPS, UE5 Nanite |
| S13.5 | **Mesh Simplification** | Decimation preserving topology + sharp features. | Polygonica, Simplygon |
| S13.6 | **Virtualized Geometry** | Nanite-style continuous LOD + cluster mesh shaders. | UE5 Nanite (frontier) |
| S13.7 | **Out-of-Core Streaming** | On-demand geometry from disk/network. | HOOPS, Cesium, potree |
| S13.8 | **Scene Graph / BVH** | Hierarchical spatial structure. | HOOPS, OCCT |
| S13.9 | **Batching & Merging** | Merge small draw calls. | All modern renderers |
| S13.10 | **Async Compute** | Parallel compute for shadows/culling/post-process. | Vulkan/D3D12 |
| S13.11 | **Memory Budgeting** | Configurable VRAM budget + eviction. | HOOPS, UE5 |
| S13.12 | **Progressive Loading** | Show partial model while loading. | HOOPS Communicator, Onshape |
| S13.13 | **Assembly-Level Culling** | Skip entire sub-assemblies off-screen or too small. | HOOPS, NX, CATIA |
| S13.14 | **Back-Face Culling** | Auto for closed solids. | Universal |

---

## 14. Point Cloud Rendering

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| PC14.1 | **Massive Point Cloud** | 1B+ points via hierarchical octree LOD. | HOOPS 2024, potree |
| PC14.2 | **Point Splatting** | Screen-space discs/squares with depth attenuation. | potree, Unreal PCL |
| PC14.3 | **Per-Point Color** | RGB from LiDAR/photo. | Universal |
| PC14.4 | **Per-Point Scalar** | False-color of attributes (intensity, elevation). | CloudCompare, ParaView |
| PC14.5 | **Point Cloud Clipping** | Plane/box clipping. | HOOPS, CloudCompare |
| PC14.6 | **Eye-Dome Lighting** | Depth-based shading without normals (Boucheny). | CloudCompare, ParaView |
| PC14.7 | **Normal Estimation** | k-NN PCA normals for lit rendering. | CloudCompare, PCL |
| PC14.8 | **Out-of-Core Streaming** | Tile-based streaming with budget. | HOOPS 2024, potree |

---

## 15. Line & Edge Rendering

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| LE15.1 | **Constant-Width Lines** | Screen-space constant regardless of depth. | All CAD |
| LE15.2 | **Variable-Width Lines** | Width by attribute (pen weight, stress). | AutoCAD |
| LE15.3 | **Stipple / Dash** | Dashed, dotted, dash-dot, ISO 128 patterns. | AutoCAD, all CAD |
| LE15.4 | **Silhouette Edges** | Normal crosses 90deg to view. | SolidWorks, NX, Rhino |
| LE15.5 | **Crease / Sharp Edges** | Dihedral angle threshold. | All shaded-with-edges |
| LE15.6 | **Boundary Edges** | Single-face edges (open shell). | All CAD |
| LE15.7 | **Feature Edges** | Union: silhouette + crease + boundary. | OCCT, SolidWorks |
| LE15.8 | **Edge Anti-Aliasing** | MSAA, line smoothing, or post-process. | All modern renderers |
| LE15.9 | **Depth-Tested vs Overlay** | Lines with/without depth testing. | AutoCAD, SolidWorks |
| LE15.10 | **Halo / Gap Lines** | White halo at T-junctions for clarity. | Technical illustration |

---

## 16. Animation & Motion

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| AN16.1 | **Exploded View Animation** | Animated assembly/disassembly. | SolidWorks, NX |
| AN16.2 | **Turntable Animation** | Camera orbit for presentation. | KeyShot |
| AN16.3 | **Flythrough** | Camera path through interiors. | Revit, Navisworks |
| AN16.4 | **Kinematic Animation** | Joint/gear/cam mechanism motion. | SolidWorks Motion |
| AN16.5 | **Transient Results** | Time-varying simulation playback. | Abaqus, ANSYS |
| AN16.6 | **Keyframe Animation** | Camera/object/material/light keyframes. | KeyShot |
| AN16.7 | **Video Export** | MP4/H.264, AVI, GIF; configurable resolution/FPS. | All animation tools |

---

## 17. Multi-View & Layout

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| MV17.1 | **Single Viewport** | Full-window 3D view. | Universal |
| MV17.2 | **Split Viewport** | 2/3/4-way tiled with independent cameras. | AutoCAD, NX, Rhino |
| MV17.3 | **Quad View** | Top/Front/Right + Perspective standard. | AutoCAD, Rhino, NX |
| MV17.4 | **Linked Views** | Synchronized selection/highlight across viewports. | NX, CATIA |
| MV17.5 | **Multi-Window** | Multiple OS windows with independent contexts. | CATIA, NX, Rhino |
| MV17.6 | **Picture-in-Picture** | Inset overview/detail viewport. | NX, CATIA |
| MV17.7 | **Model/Layout Tabs** | Model space vs paper space switching. | AutoCAD |

---

## 18. Image & Document Export

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| EX18.1 | **Screenshot** | PNG, JPEG, BMP, TIFF at viewport/custom resolution. | Universal |
| EX18.2 | **High-Res Offscreen** | Arbitrary resolution (8K+, 300 DPI print). | SolidWorks, OCCT, KeyShot |
| EX18.3 | **Transparent Background** | PNG with alpha for compositing. | KeyShot |
| EX18.4 | **Vector Export** | SVG/PDF 2D vector from 3D view. | AutoCAD, Rhino, OCCT HLR |
| EX18.5 | **3D PDF** | Interactive 3D in PDF (U3D/PRC). | HOOPS Publish, SolidWorks |
| EX18.6 | **glTF / USD Export** | Scene with geometry + materials + lights. | Modern standard |
| EX18.7 | **HDR Export** | EXR/HDR for compositing. | KeyShot, Blender |
| EX18.8 | **Batch Rendering** | Queued multi-view automated rendering. | KeyShot |

---

## 19. 3D-to-2D Projection (Drawing Generation)

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| DG19.1 | **Orthographic Projection** | Front/Top/Right/Iso 2D views from 3D. | AutoCAD SOLVIEW, SolidWorks |
| DG19.2 | **Hidden-Line View** | 2D projection with dashed/removed hidden lines. | All CAD drawing |
| DG19.3 | **Section View** | 2D cross-section with hatch. | SolidWorks, NX |
| DG19.4 | **Detail View** | Magnified region (circular/rectangular). | All CAD drawing |
| DG19.5 | **Auxiliary View** | Projection onto inclined plane. | AutoCAD, SolidWorks |
| DG19.6 | **Break View** | Shortened with break lines. | SolidWorks, NX |
| DG19.7 | **Associative Views** | Auto-update when 3D changes. | SolidWorks, NX, CATIA |

---

## 20. Ray Tracing & Photorealistic Rendering

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| RT20.1 | **Real-Time RT** | HW RT cores for reflections/shadows/GI. | NVIDIA RTX, DXR, Vulkan RT |
| RT20.2 | **Path Tracing** | Unbiased progressive for reference quality. | OptiX, Cycles, Arnold |
| RT20.3 | **Denoising** | AI/NLM denoiser for rapid convergence. | OptiX Denoiser, Intel OIDN |
| RT20.4 | **Caustics** | Light focus through refractive/reflective. | Path tracer |
| RT20.5 | **Global Illumination** | Indirect diffuse (irradiance cache, path-traced). | KeyShot, V-Ray |
| RT20.6 | **Hybrid Raster+RT** | Rasterize primary; RT reflections/shadows/GI. | UE5 Lumen |

---

## 21. AR / VR / XR Immersive Rendering

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| XR21.1 | **Stereo Rendering** | Left/right eye with configurable IPD. | OpenXR, SteamVR |
| XR21.2 | **Low-Latency Pipeline** | < 20ms motion-to-photon (single-pass stereo, late-latch). | OpenXR best practice |
| XR21.3 | **Foveated Rendering** | Reduced peripheral resolution (fixed or eye-tracked). | Meta Quest, PSVR2 |
| XR21.4 | **Pass-Through AR** | Composite virtual model with camera feed. | Apple Vision Pro, Quest 3 |
| XR21.5 | **Controller Interaction** | 6-DOF controller input for model manipulation. | OpenXR, TechViz |
| XR21.6 | **Room-Scale Navigation** | Physical walking mapped to model navigation. | All VR systems |
| XR21.7 | **Hand Tracking** | Natural hand interaction (pinch, grab). | Apple Vision Pro, Quest |

---

## 22. Cloud & Remote Rendering

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| CR22.1 | **Server-Side Rendering** | GPU on cloud server; compressed video stream to client. | HOOPS Communicator, CloudXR |
| CR22.2 | **Pixel Streaming** | H.264/H.265 frame streaming with input forwarding. | UE5 Pixel Streaming |
| CR22.3 | **WebGPU / WebGL Client** | Browser-based rendering for zero-install. | HOOPS Communicator, Onshape |
| CR22.4 | **Hybrid Rendering** | Server: heavy geometry; client: UI/annotations. | HOOPS Communicator |
| CR22.5 | **Adaptive Quality** | Dynamic resolution/quality by bandwidth. | Cloud gaming tech |
| CR22.6 | **Multi-Tenant GPU** | Multiple users sharing GPU with isolation. | NVIDIA vGPU |

---

## 23. Collaborative & Multi-User

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| CO23.1 | **Shared Viewport** | Synchronized camera across users. | Onshape, HOOPS Communicator |
| CO23.2 | **Independent Viewpoints** | Each user has independent camera, same model. | Onshape |
| CO23.3 | **Real-Time Markup** | Multi-user annotation overlay with attribution. | HOOPS Communicator |
| CO23.4 | **Presence Indicators** | Other users' cursor/viewport in 3D. | Onshape, Figma-style |
| CO23.5 | **Version Comparison** | Side-by-side or overlay of model versions. | Onshape, SolidWorks PDM |
| CO23.6 | **Design Review Session** | Structured review with issue tracking + 3D views. | HOOPS, Autodesk BIM 360 |

---

## 24. Digital Twin & IoT Overlay

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| DT24.1 | **Sensor Overlay** | Live IoT values on 3D model surfaces. | NVIDIA Omniverse, MindSphere |
| DT24.2 | **Heatmap from Sensors** | Interpolated color map from sparse readings. | Azure Digital Twins, ThingWorx |
| DT24.3 | **Alert Visualization** | Blinking/color highlight for threshold breaches. | Industrial IoT platforms |
| DT24.4 | **Time Slider** | Historical sensor data scrub with 3D sync. | NVIDIA Omniverse, GE Digital |
| DT24.5 | **State Machine Viz** | Operational state indicators (running/stopped/fault). | Siemens, PTC |

---

## 25. Additive Manufacturing Visualization

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| AM25.1 | **Slice Preview** | Individual build layer + toolpath overlay. | Cura, PrusaSlicer |
| AM25.2 | **Support Structure Display** | Support structures distinct from part geometry. | Materialise Magics, Netfabb |
| AM25.3 | **Lattice / Infill Viz** | Internal lattice and infill patterns. | nTopology, Materialise |
| AM25.4 | **Build Orientation Analysis** | Overhang angle color map vs build direction. | Netfabb, Materialise |
| AM25.5 | **Distortion Prediction** | Thermal distortion simulation overlay. | ANSYS Additive, Simufact |

---

## 26. Accessibility & Theming

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| AC26.1 | **Dark / Light Theme** | System-matching or user-selected. | All modern software |
| AC26.2 | **High-Contrast Mode** | WCAG 2.2 compliant for visually impaired. | Windows HC, WCAG |
| AC26.3 | **Color-Blind Palettes** | Deuteranopia-safe defaults (viridis, cividis). | Accessibility best practice |
| AC26.4 | **Configurable Colors** | User-changeable highlight/selection/background. | All professional CAD |
| AC26.5 | **Font Size Scaling** | Respect system DPI and user preferences. | Accessibility requirement |
| AC26.6 | **Keyboard Navigation** | Full viewport control via keyboard. | AutoCAD, all CAD |

---

## 27. Platform & GPU Backend

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| PL27.1 | **Vulkan** | Tier 1. Modern low-overhead (Windows/Linux/Android). | Industry standard |
| PL27.2 | **Direct3D 12** | Tier 1. Windows + Xbox. | Microsoft |
| PL27.3 | **Metal** | macOS / iOS / visionOS. | Apple |
| PL27.4 | **WebGPU** | Browser-native zero-install. | W3C standard |
| PL27.5 | **OpenGL / ES** | Compatibility fallback for legacy/embedded. | Widely deployed |
| PL27.6 | **Headless / Offscreen** | No display server (CI, batch, cloud). | EGL, OSMesa, Vulkan headless |
| PL27.7 | **Multi-GPU** | SFR/AFR for extreme workloads. | NVLink, Vulkan device groups |
| PL27.8 | **Mobile GPU** | Tile-based renderer optimizations. | Filament, Vulkan mobile |

---

## 28. API & Integration Requirements

| ID | Feature | Description | Reference |
|----|---------|-------------|-----------|
| API28.1 | **C++ Core API** | Thread-safe, RAII-based primary interface. | HOOPS, OCCT, Filament |
| API28.2 | **C Wrapper** | C89-compatible flat API for FFI bindings. | HOOPS, VTK |
| API28.3 | **Scene Graph API** | Hierarchical nodes (transform, visibility, material). | HOOPS, OCCT AIS, USD |
| API28.4 | **Render Graph API** | Declarative frame graph for pass scheduling. | Filament, UE5 RDG |
| API28.5 | **Custom Shader Hook** | Inject custom shaders into pipeline. | OCCT, HOOPS |
| API28.6 | **Plugin / Extension** | Runtime rendering extensions. | HOOPS, OCCT |
| API28.7 | **Callback / Events** | Pre/post-render, selection, resize, frame-complete. | All SDK renderers |
| API28.8 | **State Serialization** | Save/restore camera, lights, materials, display. | HOOPS HSF, OCCT |
| API28.9 | **UI Framework Agnostic** | Qt, wxWidgets, WinUI, GLFW, SDL, native. | HOOPS, OCCT, Filament |
| API28.10 | **Scripting Integration** | Python/Lua/JS automation. | ParaView, Blender |
| API28.11 | **Debug / Profiling API** | Frame timing, draw calls, tri count, VRAM exposed. | Filament, RenderDoc |

---

## 29. Performance Metrics & Budgets

### 29.1 Frame Rate Targets

| Scenario | Target FPS | Max Frame Time | Notes |
|----------|-----------|----------------|-------|
| Interactive (< 1M tris) | >= 60 | <= 16.7 ms | Standard desktop 1080p |
| Interactive (1M-10M tris) | >= 30 | <= 33.3 ms | LOD + culling active |
| Interactive (10M-100M tris) | >= 15 | <= 66.7 ms | GPU-driven + streaming LOD |
| Interactive (100M+ tris) | >= 10 | <= 100 ms | Out-of-core + virtualized |
| Static design review | >= 60 | <= 16.7 ms | After progressive refinement |
| VR/XR | >= 90 | <= 11.1 ms | Per-eye; < 20ms motion-to-photon |
| CAE contour (10M elements) | >= 30 | <= 33.3 ms | During rotation |
| Point cloud (1B points) | >= 20 | <= 50 ms | Hierarchical LOD active |

### 29.2 Latency Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Input-to-display | <= 50 ms | Mouse move to visual update |
| Selection feedback | <= 100 ms | Click to highlight |
| Progressive first frame | <= 2 s | 100M-tri model time to first render |
| Mode switch | <= 500 ms | Wireframe to shaded |
| Resize re-render | <= 1 frame | No black flash |

### 29.3 Memory Budgets

| Resource | Budget | Notes |
|----------|--------|-------|
| Base VRAM (empty scene) | <= 50 MB | Pipeline, defaults, UBOs |
| VRAM per 1M tris (shaded) | <= 40 MB | VB + IB + normals |
| VRAM per 1M tris (PBR textured) | <= 100 MB | Including material textures |
| GBuffer 1080p (4 MRT) | ~32 MB | RGBA8 + RGBA16F + D32 + RG16F |
| GBuffer 4K (4 MRT) | ~128 MB | Same at 3840x2160 |
| Shadow cascade (4x2K) | ~64 MB | 4 x 2048^2 x D32 |
| Point cloud (1B, LOD) | <= 4 GB | Streaming budget |
| CPU scene graph | <= 1 KB/object | For 1M objects |

### 29.4 Throughput Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Triangle throughput | >= 100M tris/s | Sustained during navigation |
| Draw call throughput | >= 10K/frame | With batching; 100K+ GPU-driven |
| Texture upload | >= 1 GB/s | Staging to GPU |
| Mesh upload | >= 2 GB/s | Streaming vertex/index |
| Pick query | <= 1 ms | GPU picking readback |

### 29.5 Scalability Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Max tris (interactive) | 100M+ | GPU-driven + LOD + streaming |
| Max tris (static) | 1B+ | Virtualized geometry |
| Max unique parts | 1M+ | Instancing + culling |
| Max lights (clustered) | 1K+ | Clustered forward or tiled deferred |
| Max viewports | 8+ | Multi-view layout |
| Max point cloud points | 10B+ | Out-of-core streaming |
| Max CAE elements | 100M+ | LOD + adaptive refinement |

---

## 30. Quality Metrics

| ID | Metric | Target | Notes |
|----|--------|--------|-------|
| Q30.1 | **Edge aliasing** | No jaggies at 100% 1080p | MSAA 4x or TAA minimum |
| Q30.2 | **Wireframe precision** | Sub-pixel accurate, no gaps | Resolution-independent curves |
| Q30.3 | **Depth precision** | No Z-fighting | Reverse-Z + polygon offset |
| Q30.4 | **Color accuracy** | sRGB output, linear internal | Correct gamma |
| Q30.5 | **HDR range** | >= 10 stops before tonemapping | RGBA16F intermediate |
| Q30.6 | **Shadow acne** | Zero self-shadow artifacts | Proper bias + normal offset |
| Q30.7 | **Temporal stability** | No flickering in static view | TAA or stable rasterization |
| Q30.8 | **Energy conservation** | PBR never reflects > received | Normalized BRDF |
| Q30.9 | **Transparency** | Correct blend order or OIT | No sorting artifacts |
| Q30.10 | **Text legibility** | Readable at intended size | MSDF or direct curve |

---

## 31. Gizmo & Manipulator

| ID | Feature | Description | Reference |
|----|---------|-------------|----------|
| GZ31.1 | **Translation Gizmo** | 3-axis arrow handles + 3 plane-constrained squares for move. Screen-space constant size. | SolidWorks, NX, Blender, Unity |
| GZ31.2 | **Rotation Gizmo** | 3 circular rings (X/Y/Z) + trackball sphere + screen-plane ring. Arc sweep preview. | SolidWorks, Blender, Unity |
| GZ31.3 | **Scale Gizmo** | 3-axis cube handles + uniform center handle. Non-uniform and uniform modes. | Blender, 3ds Max, Unity |
| GZ31.4 | **Combined Gizmo** | Translate + rotate + scale merged into single widget (mode toggle or simultaneous). | Fusion 360, Onshape, Blender |
| GZ31.5 | **Pivot Point Control** | Gizmo origin at: object center, bounding box center, world origin, user-defined point, selection centroid. | All CAD, Blender |
| GZ31.6 | **Coordinate Space Toggle** | World / Local (object) / Parent / Screen / Custom UCS coordinate frame for gizmo orientation. | AutoCAD UCS, SolidWorks, Blender |
| GZ31.7 | **Snap-to-Grid** | Gizmo operations snap to configurable grid increments (linear and angular). | AutoCAD, SolidWorks, Blender |
| GZ31.8 | **Snap-to-Geometry** | Snap to vertex / edge midpoint / face center / endpoint / intersection during gizmo drag. | AutoCAD OSNAP, SolidWorks, NX |
| GZ31.9 | **Numeric Input** | Type exact value during gizmo drag (distance, angle, scale factor). Dynamic input tooltip near cursor. | AutoCAD Dynamic Input, Blender |
| GZ31.10 | **Constraint Handle** | Single-axis, plane-constrained, and free movement modes via handle selection. | All 3D editors |
| GZ31.11 | **Mate / Assembly Constraint Gizmo** | Specialized drag handles showing DOF allowed by assembly mates (translate along axis, revolve around axis). | Onshape, SolidWorks, NX |
| GZ31.12 | **Section Plane Gizmo** | Draggable handle on clipping plane normal + rotation ring for interactive section positioning. | HOOPS, Navisworks, Rhino |
| GZ31.13 | **Light Gizmo** | Manipulator for light position, direction, cone angle, area size. Visual feedback of light shape. | KeyShot, UE5, Blender |
| GZ31.14 | **Camera Gizmo** | Manipulator showing camera frustum, focal length, near/far planes for scene cameras. | Blender, UE5 |
| GZ31.15 | **Ghost / Preview During Drag** | Semi-transparent preview of object at target position during gizmo manipulation. | Onshape, Fusion 360 |
| GZ31.16 | **Undo / Redo Integration** | Every gizmo operation generates an undo-able command. | All professional CAD |
| GZ31.17 | **Multi-Object Gizmo** | Single gizmo controlling multiple selected objects simultaneously (shared pivot). | Blender, SolidWorks, NX |

---

## 32. Viewport Overlay & HUD

| ID | Feature | Description | Reference |
|----|---------|-------------|----------|
| OV32.1 | **ViewCube / Orientation Cube** | Interactive cube widget for quick orthographic/isometric view switching. Drag to orbit. | AutoCAD ViewCube, Fusion 360, CATIA, NX |
| OV32.2 | **Axis Triad / Orientation Indicator** | RGB XYZ mini-axes in viewport corner showing current orientation. | All CAD, Abaqus, Blender |
| OV32.3 | **Compass Rose** | North/South/East/West ring around ViewCube or independent for GIS/AEC orientation. | AutoCAD, Revit, Civil 3D |
| OV32.4 | **Ground Grid** | Infinite-feel reference grid on XZ or XY plane. Major/minor lines, configurable spacing, fade with distance. | All CAD, Blender, Unity |
| OV32.5 | **World Origin Marker** | Visual indicator at (0,0,0) with axis arrows. | Rhino, Blender, SolidWorks |
| OV32.6 | **UCS / Work Plane Indicator** | Display current User Coordinate System or active work plane with labeled axes. | AutoCAD UCS, SolidWorks, NX |
| OV32.7 | **Construction Plane / Datum Display** | Semi-transparent planes for datum features, reference geometry, sketch planes. | SolidWorks, CATIA, NX, Creo |
| OV32.8 | **Scale Bar** | On-screen ruler showing real-world distance at current zoom level. Auto-updates with zoom. | AutoCAD Map, GIS tools, 3D-Tool |
| OV32.9 | **Coordinate Readout** | Live XYZ coordinates of cursor position projected onto active plane or snapped geometry. | AutoCAD status bar, all CAD |
| OV32.10 | **Dynamic Input Tooltip** | Near-cursor tooltip showing distance, angle, coordinates during drawing/editing commands. | AutoCAD Dynamic Input, NanoCAD |
| OV32.11 | **Crosshair Cursor** | Full-screen or bounded crosshair with configurable size and color. | AutoCAD, all 2D/3D CAD |
| OV32.12 | **Snap Indicator** | Visual marker (colored dot/icon) at active snap point during cursor movement. | AutoCAD OSNAP markers, SolidWorks |
| OV32.13 | **Status Bar** | Bottom bar showing: current coordinates, mode toggles (ortho/snap/grid), units, selection count. | AutoCAD, SolidWorks, all CAD |
| OV32.14 | **FPS / Performance Overlay** | Optional HUD showing frame rate, draw call count, triangle count, VRAM usage. | Debug mode in all engines |
| OV32.15 | **Progress Indicator** | Loading/computing progress bar or spinner overlaid on viewport during heavy operations. | All CAD, Blender |
| OV32.16 | **Notification / Toast** | Transient overlay messages for operation feedback (save, export, error, warning). | Modern UI practice |
| OV32.17 | **Measurement Overlay** | Persistent on-screen dimension lines from interactive measure tool (distance, angle, radius). | FreeCAD Measure, CATIA, 3D-Tool |
| OV32.18 | **Bounding Box Overlay** | Display AABB or OBB of selected objects with dimensions. | Glovius, 3D-Tool, all CAD |
| OV32.19 | **Center of Mass Marker** | Visual indicator at computed center of mass for selected body/assembly. | SolidWorks, NX, 3D-Tool |
| OV32.20 | **Safe Frame / Camera Mask** | Show render-resolution frame inside viewport (for screenshot/video composition). | Blender, 3ds Max, KeyShot |
| OV32.21 | **Navigation Bar** | Floating toolbar with orbit/pan/zoom/walk/fly/viewcube toggles. | AutoCAD Navigation Bar, NX |
| OV32.22 | **Mini-Map / Overview** | Small inset showing entire model extent with current view frustum indicator. | GIS tools, Navisworks |

---

## 33. Scene Tree & Property Panel

| ID | Feature | Description | Reference |
|----|---------|-------------|----------|
| ST33.1 | **Feature Tree** | Hierarchical list of parametric construction history (extrude, fillet, hole, pattern). | SolidWorks, CATIA, Creo, NX |
| ST33.2 | **Assembly Tree** | Component/sub-assembly hierarchy with instance count and constraints. | SolidWorks, CATIA, NX, Onshape |
| ST33.3 | **Scene Graph Browser** | Visual tree of scene nodes (transforms, groups, LOD, switch). | HOOPS, OCCT AIS, USD |
| ST33.4 | **Visibility Toggle** | Per-node eye icon to show/hide components in tree. | All CAD, Blender |
| ST33.5 | **Transparency Toggle** | Per-node transparency override from tree panel. | SolidWorks, NX |
| ST33.6 | **Color / Material Override** | Per-node appearance override from tree context menu. | SolidWorks, CATIA, NX |
| ST33.7 | **Tree-to-3D Synchronization** | Selecting tree node highlights geometry; selecting geometry scrolls tree. Bidirectional. | All CAD, HOOPS |
| ST33.8 | **Search / Filter** | Text search and filter in tree (by name, type, property, material). | NX, CATIA, Blender |
| ST33.9 | **Drag-and-Drop Reorder** | Rearrange tree hierarchy via drag-and-drop (for scene graph, not parametric history). | Blender, USD editors |
| ST33.10 | **Property Panel** | Selected-object properties: name, transform, material, mass, volume, surface area, bounding box. | All CAD, 3D-Tool, Glovius |
| ST33.11 | **Mass Properties** | Computed volume, surface area, center of mass, moments of inertia for selected body. | SolidWorks, NX, CATIA, FreeCAD |
| ST33.12 | **Multi-Select Properties** | Aggregate properties when multiple objects selected (total mass, combined bounding box). | SolidWorks, NX |
| ST33.13 | **Configuration / Design Table** | Switch between named configurations (suppressed features, different dimensions). | SolidWorks Configurations, Creo Family Table |
| ST33.14 | **Layer / Group Management** | Assign objects to named layers with per-layer visibility, color, line type. | AutoCAD layers, Rhino layers |
| ST33.15 | **Freeze / Lock** | Prevent accidental selection or modification of specific tree nodes. | AutoCAD lock, Blender lock |
| ST33.16 | **Isolate / Focus** | Temporarily hide everything except selected node(s) for focused editing. | SolidWorks Isolate, NX Show Only |

---

## 34. Interactive Measurement Tools

| ID | Feature | Description | Reference |
|----|---------|-------------|----------|
| IM34.1 | **Point-to-Point Distance** | Measure linear distance between two picked points with snap. | All CAD, FreeCAD Measure |
| IM34.2 | **Edge / Curve Length** | Measure arc length of selected edge or curve. | CATIA Measure, SolidWorks |
| IM34.3 | **Face-to-Face Distance** | Minimum distance between two faces (parallel or non-parallel). | CATIA Measure Between, NX |
| IM34.4 | **Angle Measurement** | Measure angle between two edges, faces, or planes. | All CAD |
| IM34.5 | **Radius / Diameter** | Measure radius or diameter of circular edge/face. | All CAD |
| IM34.6 | **Wall Thickness** | Minimum material thickness at clicked point (bidirectional ray-cast). | SolidWorks, 3D-Tool |
| IM34.7 | **Clearance / Gap** | Minimum distance between two non-touching bodies. | 3D-Tool, NX Check Geometry |
| IM34.8 | **Cumulative Measurement** | Chain multiple point-to-point measurements with running total. | FreeCAD Measure, AutoCAD |
| IM34.9 | **Persistent Dimensions** | Measurements remain visible as overlay until explicitly cleared. | FreeCAD, CATIA |
| IM34.10 | **Measurement Export** | Export measurement results to clipboard, CSV, or report. | 3D-Tool, Glovius |

---

## 35. Spatial Coordinate Precision (Large World Coordinates)

| ID | Feature | Description | Reference |
|----|---------|-------------|----------|
| LW35.1 | **Camera-Relative Rendering (RTE)** | CPU stores transforms in FP64; GPU receives camera-relative FP32 coordinates. Eliminates catastrophic cancellation at >10km from origin. Sub-mm precision at 100km. | UE5 Large World Coordinates, Cesium, HOOPS |
| LW35.2 | **Double-Precision Scene Graph** | All scene node transforms (position, orientation) stored as FP64 (double) in CPU-side ECS. FP32 conversion only at GPU upload boundary. | Unigine, UE5 LWC |
| LW35.3 | **Origin Rebasing** | Periodic camera-origin shift to keep camera near FP32 origin. Transparent to rendering pipeline (only SceneBuffer upload changes). | UE5 World Origin Shift, Cesium |
| LW35.4 | **FP64 GPU Compute** | Double-precision compute shaders for measurement, mass properties, distance queries. WebGPU fallback: Double-Single (2xFP32) emulation. | Vulkan shaderFloat64, GL ARB_gpu_shader_fp64 |
| LW35.5 | **Multi-Scale Rendering** | Seamless rendering from millimeter-scale detail to kilometer-scale plant/factory layout without Z-fighting or jitter. | Navisworks, Bentley, AEC/BIM tools |

---

## 36. Industrial Text Engine & Typographic Precision

| ID | Feature | Description | Reference |
|----|---------|-------------|----------|
| TX36.1 | **Text Shaping Engine** | HarfBuzz integration for OpenType feature processing: ligatures, kerning, contextual alternates, mark positioning. Correct rendering of Arabic, Devanagari, Thai, and CJK scripts. | HarfBuzz, ICU, Pango |
| TX36.2 | **Font Fallback Chain** | Dynamic multi-font fallback: when primary font lacks a glyph, automatically search fallback chain (system fonts, engineering symbol fonts). Runtime glyph atlas generation for on-demand codepoints. | All modern text engines, Skia, DirectWrite |
| TX36.3 | **Path-Based Text Rendering** | GPU direct Bezier curve rendering (Slug/GreenLightning algorithm) for arbitrarily large text and 2D drawing export. Fragment shader winding-number evaluation. Zero aliasing at any zoom. | Slug (Lengyel 2017), GPU Gems |
| TX36.4 | **CJK Large Glyph Set** | Support for CJK Unified Ideographs (>80K codepoints). Virtual atlas paging with LRU eviction — no pre-computation of entire character set. | FreeType, HarfBuzz, all CJK CAD |
| TX36.5 | **BiDi & Complex Script Layout** | UAX #9 bidirectional algorithm for mixed LTR+RTL text (e.g., Arabic dimension values in English drawings). Paragraph-level reordering before shaping. | FriBidi, ICU, STEP AP242 multilingual PMI |
| TX36.6 | **Engineering Standards Compliance** | Text rendering must support ISO 3098 (lettering on technical drawings), ASME Y14.2 (line conventions), including mono-stroke fonts for CNC engraving. | ISO 3098, ASME Y14.2, AutoCAD SHX |

---

## 37. Scientific Volume Rendering

| ID | Feature | Description | Reference |
|----|---------|-------------|----------|
| VR37.1 | **Direct Volume Rendering (DVR)** | GPU ray-marching through 3D scalar fields. Front-to-back compositing. Adaptive step size by gradient magnitude. Entry/exit via AABB intersection. | ParaView, VTK, ANSYS CFD-Post |
| VR37.2 | **Transfer Functions** | 1D (scalar→RGBA) and 2D (scalar×gradient→RGBA) transfer function editor. Preset library for medical (bone/soft/air) and CFD (temperature/pressure/density). Real-time interactive editing. | ParaView, Amira, 3D Slicer |
| VR37.3 | **Multi-Volume Compositing** | Up to 4 overlapping 3D volumes with independent transfer functions. Depth-correct compositing with scene geometry (mesh + volume interleaving). | ParaView, VTK |
| VR37.4 | **Volume Clipping** | Section plane integration — clip planes (§8) applied to volume data. Reveals internal cross-section with color-mapped cut face. | ANSYS CFD-Post, ParaView |
| VR37.5 | **Empty Space Skipping** | Min/max octree acceleration for transparent regions. Early ray termination at opacity saturation. | GPU Gems, VTK |
| VR37.6 | **Volume Data Formats** | Import: NIfTI (.nii), raw binary, VTK structured grid, OpenVDB, DICOM series. Resolution: up to 512^3 real-time, 1024^3 with LOD. | Medical imaging, CFD post |

---

## 38. Topology Mapping & Rendering Determinism

| ID | Feature | Description | Reference |
|----|---------|-------------|----------|
| TM38.1 | **Topology ID Mapping** | Every rendered triangle/line carries B-Rep topology IDs (FaceId, EdgeId, VertexId) via per-primitive SSBO or VisBuffer encoding. O(1) lookup from pixel to topology entity. | OCCT AIS, HOOPS, Parasolid |
| TM38.2 | **Sub-Element Picking** | GPU pick returns not just objectId but full topology path: Body→Shell→Face→Edge→Vertex with parametric UV on face. No CPU geometry re-intersection needed. | SolidWorks, NX, CATIA |
| TM38.3 | **Persistent Naming** | Topology IDs survive model edits (fillet, chamfer, boolean) via persistent naming from geometry kernel. Renderer selection state maps to persistent names, not volatile indices. | OCCT TNaming, Parasolid persistent ID |
| TM38.4 | **Deterministic Rendering** | Identical scene state + identical camera → bit-exact framebuffer output across frames. No random seed variation, no uninitialized memory, no non-deterministic GPU scheduling artifacts. Required for visual regression CI. | All CI-gated renderers |
| TM38.5 | **Selection Highlight Stability** | Highlighted topology entity remains visually stable across tessellation LOD changes and camera movements. No flickering or "lost selection" on LOD transition. | All CAD |

---

## Appendix A: Industry Competitor Feature Matrix

| Category | HOOPS | OCCT | SolidWorks | CATIA | NX | Rhino | KeyShot | ParaView |
|----------|:-----:|:----:|:----------:|:-----:|:--:|:-----:|:-------:|:--------:|
| Display Modes (S1) | 3 | 2 | 3 | 3 | 3 | 3 | 1 | 1 |
| PBR Materials (S3) | 2 | 1 | 3 | 3 | 2 | 2 | 3 | 1 |
| IBL + Env (S4) | 3 | 1 | 2 | 3 | 2 | 3 | 3 | 1 |
| Selection (S6) | 3 | 3 | 3 | 3 | 3 | 3 | 1 | 2 |
| PMI/GD&T (S7) | 3 | 2 | 3 | 3 | 3 | 1 | - | - |
| Section/Explode (S8) | 3 | 2 | 3 | 3 | 3 | 2 | 2 | 2 |
| CAE Post (S12) | - | - | 1 | 2 | 2 | - | - | 3 |
| Large Model (S13) | 3 | 2 | 2 | 3 | 3 | 2 | 1 | 2 |
| Point Cloud (S14) | 3 | 1 | 1 | 2 | 2 | 2 | - | 2 |
| Ray Tracing (S20) | 2 | 2 | 2 | 3 | 2 | 2 | 3 | 2 |
| XR (S21) | 2 | 1 | 1 | 2 | 2 | 1 | 1 | 1 |
| Cloud/Remote (S22) | 3 | 0 | 1 | 3 | 2 | 0 | 0 | 2 |
| Collaboration (S23) | 3 | 0 | 1 | 2 | 1 | 0 | 0 | 1 |

> Scale: 0 = none, 1 = basic, 2 = good, 3 = industry-leading, - = not applicable

---

## Appendix B: Feature Count Summary

| Section | Count | Priority Breakdown |
|---------|------:|-------------------|
| 1. Display Modes | 13 | 8 must-have, 5 nice-to-have |
| 2. Geometric Rendering | 12 | 10 must-have, 2 advanced |
| 3. Material & Shading | 18 | 8 must-have, 10 progressive |
| 4. Lighting & Environment | 13 | 7 must-have, 6 progressive |
| 5. Camera & Projection | 13 | 9 must-have, 4 advanced |
| 6. Selection & Picking | 12 | 9 must-have, 3 advanced |
| 7. Annotation & PMI | 13 | 7 must-have, 6 progressive |
| 8. Section & Explode | 10 | 7 must-have, 3 advanced |
| 9. Transparency | 6 | 3 must-have, 3 advanced |
| 10. Post-Processing | 12 | 5 must-have, 7 progressive |
| 11. Analysis Viz | 10 | 4 must-have, 6 progressive |
| 12. CAE Post-Process | 17 | 8 must-have, 9 advanced |
| 13. Large Model | 14 | 8 must-have, 6 frontier |
| 14. Point Cloud | 8 | 4 must-have, 4 advanced |
| 15. Line & Edge | 10 | 7 must-have, 3 advanced |
| 16. Animation | 7 | 3 must-have, 4 progressive |
| 17. Multi-View | 7 | 4 must-have, 3 advanced |
| 18. Export | 8 | 4 must-have, 4 progressive |
| 19. Drawing Generation | 7 | 5 must-have, 2 advanced |
| 20. Ray Tracing | 6 | 2 must-have, 4 frontier |
| 21. XR | 7 | 2 must-have, 5 frontier |
| 22. Cloud Rendering | 6 | 2 must-have, 4 frontier |
| 23. Collaboration | 6 | 2 must-have, 4 progressive |
| 24. Digital Twin | 5 | 1 must-have, 4 frontier |
| 25. Additive Mfg | 5 | 2 must-have, 3 advanced |
| 26. Accessibility | 6 | 4 must-have, 2 progressive |
| 27. Platform/Backend | 8 | 3 must-have, 5 progressive |
| 28. API/Integration | 11 | 6 must-have, 5 progressive |
| 29. Performance | 30 | 20 must-have, 10 target |
| 30. Quality | 10 | 8 must-have, 2 progressive |
| 31. Gizmo & Manipulator | 17 | 10 must-have, 7 progressive |
| 32. Viewport Overlay & HUD | 22 | 12 must-have, 10 progressive |
| 33. Scene Tree & Property | 16 | 10 must-have, 6 progressive |
| 34. Measurement Tools | 10 | 7 must-have, 3 progressive |
| 35. Spatial Coordinate Precision | 5 | 4 must-have, 1 advanced |
| 36. Industrial Text Engine | 6 | 4 must-have, 2 advanced |
| 37. Scientific Volume Rendering | 6 | 3 must-have, 3 advanced |
| 38. Topology Mapping & Determinism | 5 | 4 must-have, 1 progressive |
| **TOTAL** | **417** | |

---

## Appendix C: Glossary

| Term | Definition |
|------|-----------|
| **B-Rep** | Boundary Representation: solid modeling via vertices, edges, faces, shells |
| **BRDF** | Bidirectional Reflectance Distribution Function |
| **DSPBR** | Dassault Systemes Enterprise PBR Shading Model |
| **EDL** | Eye-Dome Lighting: depth-based ambient shading for point clouds |
| **GD&T** | Geometric Dimensioning and Tolerancing (ASME Y14.5 / ISO GPS) |
| **GGX** | Trowbridge-Reitz microfacet distribution |
| **GTAO** | Ground Truth Ambient Occlusion |
| **HLR** | Hidden-Line Removal |
| **HZB** | Hierarchical Z-Buffer for occlusion culling |
| **IBL** | Image-Based Lighting |
| **LOD** | Level of Detail |
| **LTC** | Linearly Transformed Cosines (area light technique) |
| **MRT** | Multiple Render Targets |
| **MSDF** | Multi-channel Signed Distance Field |
| **NPR** | Non-Photorealistic Rendering |
| **OIT** | Order-Independent Transparency |
| **PMI** | Product and Manufacturing Information |
| **PBR** | Physically Based Rendering |
| **SH** | Spherical Harmonics |
| **SSAO** | Screen-Space Ambient Occlusion |
| **SSR** | Screen-Space Reflections |
| **TAA** | Temporal Anti-Aliasing |
| **UCS** | User Coordinate System: user-defined local coordinate frame |
| **ViewCube** | Interactive 3D orientation widget (Autodesk patent, now widely adopted) |
| **WCS** | World Coordinate System: fixed global coordinate frame |

---

*Document generated via 5-round iterative research + supplementary Gizmo/Overlay/Tree round. Last updated: 2026-03-16.*
