# T7b.9.1 — Iso-Parameter Lines (CPU NURBS Iso-u/v Eval → Polyline → SDF Render)

**Phase**: 13-7b
**Component**: Iso-Parameter Lines
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

Per roadmap: "CPU pre-computation of iso-u/v curves from IKernel NURBS surface evaluation at configurable density (default: 10 iso-lines per parameter direction). Output: polyline vertex buffer per-surface. GPU rendering: SDF line pass (same pipeline as HLR — shared EdgeBuffer + LinePattern SSBO). Per-surface toggling via GpuInstance.flags. Budget: <0.5ms @100K iso-lines."

- **CPU**: `IKernel::EvalSurface(surfaceId, u_or_v_fixed, paramRange)` → polyline points. Convert to `EdgeBuffer` format (EdgeType::Construction, LineType::Dotted).
- **GPU**: Feed into Phase 7a-1 `EdgeRenderer::Render()` — same SDF pipeline, same LinePattern SSBO.
- **Toggle**: Per-surface via `GpuInstance.flags` bit or per-view toggle.

## Steps

- [ ] **Step 1**: Implement CPU iso-curve evaluation → EdgeBuffer conversion
      `[verify: compile]`
- [ ] **Step 2**: Wire into SDF render pipeline (reuse EdgeRenderer)
      `[verify: compile]`
- [ ] **Step 3**: Tests (iso-line count, spacing accuracy, SDF render quality)
      `[verify: test]`
