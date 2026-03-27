# T6a.7.1 — Software Rasterizer — Compute Fine Raster, uint64 SSBO atomicMax *(Optional)*

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: Software Rasterizer *(optional)*
**Roadmap Ref**: `roadmap.md` L1742 — Software Rasterizer (optional, can defer to 6b)
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.6.1 | Visibility Buffer | Not Started | `VisibilityBuffer::GetTexture()` for resolve output target |
| T6a.5.2 | Mesh Shader | Not Started | Triangle classification: triangles <4px routed to SW path |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/SwRasterizer.h` | **public** | **M** | `SwRasterizer::Create()`, `Rasterize()`, `Resolve()` |
| `shaders/vgeo/sw_raster.slang` | internal | L | Compute shader: bin triangles → fine raster → atomicMax on uint64 SSBO |
| `shaders/vgeo/sw_resolve.slang` | internal | L | Compute shader: copy SSBO → VisBuffer R32G32_UINT |
| `src/miki/vgeo/SwRasterizer.cpp` | internal | L | Pipeline creation + dispatch |

- **Error model**: `Create()` → `Result<SwRasterizer>`. Returns `FeatureNotSupported` if `DeviceFeature::Int64Atomics` unavailable. `Rasterize()`/`Resolve()` are void.
- **Thread safety**: stateful. Single-owner.
- **GPU constraints**: requires `VK_KHR_shader_atomic_int64` / `shaderBufferInt64Atomics`. Uses uint64 SSBO (not image atomics). Packed `{depth:32, payload:32}` — `atomicMax` ensures closest fragment wins (Reverse-Z: largest depth = closest).
- **Invariants**: SW raster output merged into VisBuffer produces identical visual result to HW mesh shader for the same triangles. Performance benefit only for <4px triangles.

### Downstream Consumers

- `SwRasterizer.h` (**public**, heat **M**):
  - T6a.8.1 (Demo): optional path, enabled when `DeviceFeature::Int64Atomics` available
  - Phase 6b: `HybridDispatch` routes small triangles to SW rasterizer

### Upstream Contracts
- T6a.6.1: `VisibilityBuffer::GetTexture()` — resolve target
- T6a.5.2: mesh shader classifies triangles by screen area → small tri buffer
- T6a.0.1: `ICommandBuffer::Dispatch()`, `DeviceFeature::Int64Atomics`
- Phase 4: `ResourceManager::CreateBuffer()` for uint64 SSBO (W×H × 8B)

### Technical Direction
- **Tile-based fine rasterization**: divide screen into 8×8 tiles. For each small triangle, compute bounding tile range, iterate tiles, compute edge functions per pixel, atomicMax to SSBO.
- **Packed uint64**: `upper32 = depth_as_uint32 (Reverse-Z, larger = closer)`, `lower32 = visBufferPayload (instanceId:24 | primitiveId:8 in upper, primitiveId:16 | materialId:16 in lower)`. `atomicMax` on this packed value → closest fragment wins because depth is in the upper 32 bits.
- **Resolve pass**: lightweight compute copies non-zero SSBO entries to VisBuffer R32G32_UINT texture. Clears SSBO after resolve.
- **Feature gate**: if `Int64Atomics` not available, `Create()` returns `FeatureNotSupported`. Mesh shader handles ALL triangles (correct, slightly suboptimal for tiny triangles). Zero functional regression.
- **Performance target**: 10-15% GPU time reduction on Nanite-grade scenes (many <4px triangles). Marginal benefit for typical CAD (most triangles >4px). Worth implementing for Phase 6b 100M+ tri target.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/vgeo/SwRasterizer.h` | **public** | **M** | Optional SW raster |
| Create | `shaders/vgeo/sw_raster.slang` | internal | L | Fine rasterization compute |
| Create | `shaders/vgeo/sw_resolve.slang` | internal | L | SSBO → VisBuffer resolve |
| Create | `src/miki/vgeo/SwRasterizer.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_sw_rasterizer.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define SwRasterizer.h (heat M)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `SwRasterizer::Create` | `(IDevice&, SlangCompiler&, uint32_t width, uint32_t height) -> Result<SwRasterizer>` | `[[nodiscard]]` static, returns FeatureNotSupported if no Int64Atomics |
      | `SwRasterizer::Rasterize` | `(ICommandBuffer&, BufferHandle smallTriBuffer, uint32_t triCount) -> void` | — |
      | `SwRasterizer::Resolve` | `(ICommandBuffer&, TextureHandle visBuffer) -> void` | Copies SSBO → VisBuffer |
      | `SwRasterizer::IsAvailable` | `(IDevice const&) -> bool` | `[[nodiscard]]` static, checks Int64Atomics |

      `[verify: compile]`

- [ ] **Step 2**: Implement shaders + C++ dispatch
      `[verify: compile]`

- [ ] **Step 3**: Unit tests
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(SwRasterizer, Create_WithInt64Atomics)` | Positive | Create succeeds on Tier1 with Int64Atomics (GTEST_SKIP) | 1-2 |
| `TEST(SwRasterizer, Create_WithoutInt64Atomics)` | Error | Create returns FeatureNotSupported on Mock | 1-2 |
| `TEST(SwRasterizer, IsAvailable_Tier1)` | Positive | Returns true on capable hardware | 1,3 |
| `TEST(SwRasterizer, Rasterize_SingleTriangle)` | Positive | 1 small triangle → correct pixel in SSBO | 2-3 |
| `TEST(SwRasterizer, Resolve_CopiesCorrectly)` | Positive | SSBO values appear in VisBuffer after resolve | 2-3 |
| `TEST(SwRasterizer, DepthTest_ClosestWins)` | Positive | Two overlapping triangles → closest visible | 2-3 |
| `TEST(SwRasterizer, EndToEnd_HybridParity)` | **Integration** | SW raster output matches HW mesh shader for same triangles | 2-3 |

## Design Decisions

*(Fill during implementation.)*

## Implementation Notes

*(Fill during implementation.)*
