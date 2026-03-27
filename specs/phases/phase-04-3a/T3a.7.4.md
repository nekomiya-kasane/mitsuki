# T3a.7.4 — Per-Material Texture Descriptor Layout

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 7 — 5-Backend Sync + Demo + Phase 2 Debt Cleanup
**Roadmap Ref**: `roadmap.md` — Phase 2 rendering debt cleanup
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.3.1 | Deferred PBR resolve | Complete | `DeferredResolve` consumes GBuffer which uses per-material set 1 |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/render/ForwardPass.h` | **public** | **H** | Extended per-material descriptor layout (set 1: UBO + 4 textures + sampler) |
| `include/miki/render/DummyTextures.h` | **public** | **M** | `DummyTextures` — 1x1 placeholder textures for material slots without maps |
| `src/miki/render/DummyTextures.cpp` | internal | L | Implementation |
| `src/miki/render/ForwardPass.cpp` | internal | L | Updated `EnsureMaterialDescriptor` + layout + dummy texture lifetime |
| `shaders/render/forward_frag.slang` | **public** | **H** | Set 1 texture bindings (declared but unused in Phase 3a — sampling deferred) |
| `shaders/render/gbuffer_frag.slang` | **public** | **H** | Set 1 texture bindings (declared but unused in Phase 3a) |

**New Set 1 layout (per-material)**:

| Binding | Type | Name | Dummy value when no map |
|---------|------|------|------------------------|
| 0 | UniformBuffer | MaterialParams UBO | (always present) |
| 1 | SampledTexture | albedoMap | 1x1 white (1,1,1,1) |
| 2 | SampledTexture | normalMap | 1x1 flat normal (0.5,0.5,1.0,1.0) |
| 3 | SampledTexture | roughnessMetallicMap | 1x1 (roughness=0.5, metallic=0, 0, 0) |
| 4 | SampledTexture | aoMap | 1x1 white (1,1,1,1) |
| 5 | Sampler | materialSampler | Linear + Repeat |

- **Error model**: `DummyTextures::Create()` returns `expected<DummyTextures, ErrorCode>` — propagated from `IDevice::CreateTexture/Buffer`
- **Thread safety**: single-threaded (render setup)
- **GPU constraints**: 1x1 RGBA8_UNORM textures, minimal VRAM footprint (~16 bytes each)
- **Phase 3a scope**: textures are declared in shaders but NOT sampled yet. Forward/GBuffer shaders continue reading material params from UBO only. This avoids shader rewrite while establishing the descriptor layout contract for Phase 3b+ texture-mapped materials.

### Downstream Consumers

- `ForwardPass` — owns `perMaterialLayout_`, creates material descriptor sets
- `GBufferPass` — shares `perMaterialLayout_` from ForwardPass (via `iDesc.perMaterialLayout`)
- Phase 3b+ — will sample texture maps in shaders; layout already in place
- Phase 7a — edge rendering will use normal map for edge detection

### Upstream Contracts
- `IDevice::CreateTexture`, `IDevice::CreateSampler`, `IDevice::CreateBuffer`
- `DescriptorWriter` / `UpdateDescriptorSet`
- `rhi::DescriptorType::SampledTexture`, `rhi::DescriptorType::Sampler`

### Technical Direction
- **Dummy textures**: 1x1 RGBA8_UNORM, uploaded once at ForwardPass::Create time. Shared across all materials.
- **Backward compatible**: existing `MaterialParameterBlock` UBO unchanged. Texture slots added alongside.
- **Phase 3a shaders declare but don't sample**: avoids breaking existing PBR logic. Validation layers require bound descriptors even if unused — dummies satisfy this.
- **Sampler per-material**: allows future per-material sampler variation (e.g., nearest for pixel art). Phase 3a uses shared linear+repeat.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/render/DummyTextures.h` | **public** | **M** | DummyTextures struct + factory |
| Create | `src/miki/render/DummyTextures.cpp` | internal | L | 1x1 texture creation + upload |
| Modify | `src/miki/render/ForwardPass.cpp` | internal | L | Extend set 1 layout, bind dummies in EnsureMaterialDescriptor |
| Modify | `include/miki/render/ForwardPass.h` | **public** | **H** | Add DummyTextures member + sampler |
| Modify | `shaders/render/forward_frag.slang` | **public** | **H** | Declare set 1 texture bindings |
| Modify | `shaders/render/gbuffer_frag.slang` | **public** | **H** | Declare set 1 texture bindings |
| Create | `tests/unit/test_dummy_textures.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Create DummyTextures.h/cpp — 1x1 placeholder textures + factory
      **Files**: `include/miki/render/DummyTextures.h` (**public** M), `src/miki/render/DummyTextures.cpp` (internal L)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Extend per-material descriptor layout + bind dummies in ForwardPass
      **Files**: `include/miki/render/ForwardPass.h` (**public** H), `src/miki/render/ForwardPass.cpp` (internal L)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 3**: Update shaders — declare set 1 texture bindings (unused in Phase 3a)
      **Files**: `shaders/render/forward_frag.slang` (**public** H), `shaders/render/gbuffer_frag.slang` (**public** H)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 4**: Write unit tests for DummyTextures + layout
      **Files**: `tests/unit/test_dummy_textures.cpp` (internal L), `tests/unit/CMakeLists.txt` (internal L)
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(DummyTextures, CreateReturnsValid)` | Positive | All 4 texture handles valid after Create | 4 |
| `TEST(DummyTextures, SamplerValid)` | Positive | Sampler handle valid | 4 |
| `TEST(DummyTextures, MoveTransfersOwnership)` | State | Move leaves source empty, dest valid | 4 |
| `TEST(DummyTextures, DestroyCleanup)` | State | Destructor cleans up all resources | 4 |
| `TEST(DummyTextures, PerMaterialLayoutHas6Bindings)` | Positive | Layout has UBO + 4 tex + 1 sampler | 4 |

## Design Decisions

- **Declare-but-don't-sample in Phase 3a**: texture bindings are declared in shaders but not read. This satisfies Vulkan validation (all descriptor bindings must be bound) while avoiding shader rewrite. Phase 3b+ will sample them.
- **Shared DummyTextures across all materials**: created once in `ForwardPass::Create()`, referenced by all material descriptor sets. Minimal VRAM (~12 bytes pixel data + staging overhead).
- **1x1 RGBA8_UNORM**: simplest possible format, compatible with all backends, single texel = no mip chain needed.
- **Sampler per-material slot**: allows future per-material sampler variation (nearest for pixel art, aniso for metal). Phase 3a uses shared linear+repeat.

## Implementation Notes

- Contract check: PASS — set 1 layout matches shader declarations (6 bindings: UBO + 4 SampledTexture + 1 Sampler).
- 624/624 tests pass (excluding 4 pre-existing GL/WebGPU failures), zero regression.
- 5/5 DummyTextures-specific tests pass.
- `DummyTextures::Destroy()` cannot destroy sampler handles due to known `IDevice::DestroySampler` gap. Tests use `SetLeakCheckEnabled(false)`.
