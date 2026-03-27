# T6a.6.1 — Visibility Buffer — R32G32_UINT Atomic Write + Clear Pass

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: Visibility Buffer + Material Resolve
**Roadmap Ref**: `roadmap.md` L1741 — Visibility Buffer 64-bit atomic
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.5.2 | Mesh Shader | Not Started | Mesh shader outputs VisBuffer payload per-pixel |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/VisibilityBuffer.h` | **public** | **H** | `VisibilityBuffer::Create()`, `Clear()`, `GetTexture()`, `GetFormat()`, decode helpers |
| `shaders/vgeo/visbuf_clear.slang` | internal | L | Compute clear pass (0xFFFFFFFF fill) |
| `src/miki/vgeo/VisibilityBuffer.cpp` | internal | L | Texture creation + clear dispatch |

- **Error model**: `Create()` → `Result<VisibilityBuffer>`. `Clear()` is void.
- **Thread safety**: stateful. Single-owner.
- **GPU constraints**: R32G32_UINT format (2×32-bit per pixel). Written by mesh shader as color attachment. Read by material resolve as sampled texture. Clear value = `{0xFFFFFFFF, 0xFFFFFFFF}` (invalid sentinel).
- **Invariants**: after `Clear()`, every pixel = invalid sentinel. After mesh shader geometry pass, pixels with valid geometry have `instanceId != 0xFFFFFFFF` and `primitiveId != 0xFFFFFFFF`. materialId is NOT stored in VisBuffer — resolved via `GpuInstance[instanceId].materialId`.

### Downstream Consumers

- `VisibilityBuffer.h` (**public**, heat **H**):
  - T6a.6.2 (Material Resolve): reads VisBuffer texture to resolve materials per-pixel
  - T6a.7.1 (SW Raster): writes to same VisBuffer via SSBO atomic path
  - Phase 7a-2 (Picking): readback VisBuffer pixel at click location for instant pick
  - Phase 7a-2 (Box/Lasso Select): compute shader reads VisBuffer region for collect

### Upstream Contracts
- T6a.5.2: mesh shader writes `uint2(R32, G32)` per-pixel to VisBuffer color attachment
- T6a.0.1: `IDevice::CreateTexture()` with `TextureUsage::ColorAttachment | Storage | Sampled`
- Phase 3a: `RenderGraphBuilder::CreateTexture()` for transient VisBuffer

### Technical Direction
- **Format**: `R32G32_UINT` — 8 bytes/pixel. At 4K = 3840×2160 × 8B = ~63MB. Acceptable for Tier1.
- **Payload encoding** (per `rendering-pipeline-architecture.md` §5.4): `R32 = instanceId (full 32-bit)`, `G32 = primitiveId (full 32-bit)`. materialId is **NOT** stored in VisBuffer — resolved at material resolve time via `GpuInstance[instanceId].materialId` indirection. Full 32-bit supports 4G instances/primitives (vs 16M with 24-bit packing). Simpler encoding (no bitfield packing/unpacking).
- **Clear**: compute shader fills entire texture with sentinel `{0xFFFFFFFF, 0xFFFFFFFF}` at frame start. Faster than render pass clear for storage textures.
- **RenderGraph integration**: VisBuffer is a transient RG resource created per-frame. Geometry pass writes it, material resolve reads it. Barrier: `ColorAttachment → ComputeShaderRead` between geometry and resolve.
- **Decode helpers**: `DecodeInstanceId(uint2 payload) -> uint32_t` (= `payload.x`), `DecodePrimitiveId(uint2 payload) -> uint32_t` (= `payload.y`), `IsValidPixel(uint2 payload) -> bool` (= `payload.x != 0xFFFFFFFF`). Both C++ (for readback) and Slang (for shader) versions. No `DecodeMaterialId` — materialId resolved via SceneBuffer.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/vgeo/VisibilityBuffer.h` | **public** | **H** | VisBuffer management + decode |
| Create | `shaders/vgeo/visbuf_clear.slang` | internal | L | Clear compute |
| Create | `src/miki/vgeo/VisibilityBuffer.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_visibility_buffer.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define VisibilityBuffer.h (heat H)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `VisibilityBuffer::Create` | `(IDevice&, SlangCompiler&, uint32_t width, uint32_t height) -> Result<VisibilityBuffer>` | `[[nodiscard]]` static |
      | `VisibilityBuffer::Clear` | `(ICommandBuffer&) -> void` | Compute clear to sentinel |
      | `VisibilityBuffer::GetTexture` | `() const noexcept -> TextureHandle` | `[[nodiscard]]` |
      | `VisibilityBuffer::Resize` | `(uint32_t width, uint32_t height) -> void` | Recreate on resolution change |
      | `DecodeInstanceId` | `(uint2 payload) -> uint32_t` | `[[nodiscard]]` constexpr, `payload.x` |
      | `DecodePrimitiveId` | `(uint2 payload) -> uint32_t` | `[[nodiscard]]` constexpr, `payload.y` |
      | `IsValidPixel` | `(uint2 payload) -> bool` | `[[nodiscard]]` constexpr, `payload.x != 0xFFFFFFFF` |

      `[verify: compile]`

- [ ] **Step 2**: Implement VisibilityBuffer.cpp + visbuf_clear.slang
      `[verify: compile]`

- [ ] **Step 3**: Unit tests
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(VisBuffer, CreateValid)` | Positive | Create returns valid, texture handle non-null | 1-2 |
| `TEST(VisBuffer, ClearFillsSentinel)` | Positive | After clear, readback all pixels = 0xFFFFFFFF | 2-3 |
| `TEST(VisBuffer, DecodeInstanceId)` | Positive | `uint2{42, 99}` → instanceId=42 | 1,3 |
| `TEST(VisBuffer, DecodePrimitiveId)` | Positive | `uint2{42, 99}` → primitiveId=99 | 1,3 |
| `TEST(VisBuffer, IsValidPixel_Sentinel)` | Boundary | 0xFFFFFFFF → false | 1,3 |
| `TEST(VisBuffer, IsValidPixel_Valid)` | Positive | non-sentinel → true | 1,3 |
| `TEST(VisBuffer, Resize_Recreates)` | Positive | Resize changes dimensions, old texture destroyed | 2-3 |
| `TEST(VisBuffer, Payload_MaxValues)` | Boundary | instanceId=0xFFFFFFFE, primitiveId=0xFFFFFFFE both decode correctly | 1,3 |
| `TEST(VisBuffer, MoveSemantics)` | State | Move leaves source empty | 1-3 |
| `TEST(VisBuffer, EndToEnd_WriteAndReadback)` | **Integration** | Render known geometry → read VisBuffer → verify instanceId/primitiveId | 2-3 |

## Design Decisions

*(Fill during implementation.)*

## Implementation Notes

*(Fill during implementation.)*
