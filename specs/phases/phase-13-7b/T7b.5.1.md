# T7b.5.1 — Sketch Entity Types + SDF Render (Lines/Arcs/Splines/Circles)

**Phase**: 13-7b
**Component**: Sketch Renderer
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

None (L0 task).

## Scope

Per arch spec Sketch Renderer Details: GPU rendering of 2D sketch entities from `IKernel::Import` or live `IKernel::CreateSketch`. SDF anti-aliased rendering reusing Phase 7a-1 SDF line infrastructure.

- **Entity types**: line segments (SDF AA), circular arcs (SDF parametric), full circles, ellipses, B-Spline curves (adaptive polyline from `IKernel::EvalCurve`), construction geometry (dashed via LinePattern), reference points (cross/diamond/square markers).
- **Rendering**: Per arch spec §3 Pass #77: "Same SDF line PSO as HLR Render (#30)". Entities projected onto sketch plane, expanded to quads, SDF fragment shader.
- **SketchEntityBuffer SSBO**: per-entity `{uint8 type, float3 params[], float4 color, uint32 constraintFlags}`.

## Steps

- [ ] **Step 1**: Define SketchEntity types + SketchEntityBuffer SSBO
      `[verify: compile]`
- [ ] **Step 2**: Implement SDF render for lines, arcs, circles, splines
      `[verify: compile]`
- [ ] **Step 3**: Tests (entity render correctness, SDF quality)
      `[verify: test]`
