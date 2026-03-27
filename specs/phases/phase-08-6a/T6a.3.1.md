# T6a.3.1 — HiZ Pyramid Compute

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: HiZ Pyramid
**Roadmap Ref**: `roadmap.md` L1740 — HiZ pyramid from previous frame depth
**Status**: Complete
**Current Step**: All done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.0.1 | RHI Extensions | Complete | `DispatchIndirect()`, compute pipeline |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/HiZPyramid.h` | **public** | **H** | `HiZPyramid::Create()`, `Build()`, `GetTexture()`, `GetMipCount()` |
| `shaders/vgeo/hiz_downsample.slang` | internal | L | Single-pass or multi-pass min/max downsample compute |
| `src/miki/vgeo/HiZPyramid.cpp` | internal | L | Mip chain creation + dispatch |

- **Error model**: `Create()` → `Result<HiZPyramid>`. `Build()` is void.
- **Thread safety**: stateful (owns mip chain texture + pipeline). Single-owner.
- **GPU constraints**: R32F mip chain, each level = ceil(prevW/2) × ceil(prevH/2). Conservative max (not min) for occlusion culling with Reverse-Z.
- **Invariants**: `Build()` must be called after depth buffer is populated. Mip 0 = full-res depth copy. Each mip = max of 2×2 parent texels.
- **DepthPrePass integration** (per `rendering-pipeline-architecture.md` §4.1 Pass #1): HiZ requires a populated depth buffer as input. In Phase 6a, DepthPrePass reuses the same task→mesh pipeline (T6a.5.1+T6a.5.2) but with a **depth-only PSO** (no VisBuffer color attachment, depthWrite=true, colorWrite=none). First frame bootstraps from Phase 3a GBuffer depth; subsequent frames use previous-frame depth for two-phase occlusion culling. DepthPrePass is NOT a separate Task — it is a configuration variant of the task+mesh pipeline handled in T6a.5.1/T6a.8.1 render graph assembly.

### Downstream Consumers

- `HiZPyramid.h` (**public**, heat **H**):
  - T6a.5.1 (Task Shader): reads HiZ mip chain for per-meshlet occlusion test
  - Phase 6b: `GPU Culling` reuses HiZ for LOD selection occlusion
  - Phase 7a-2: `SSR` hi-Z ray march reads the same pyramid

### Upstream Contracts
- Phase 3a: `GBuffer` produces D32F depth texture (GBufferLayout.depth)
- Phase 3a: `RenderGraphBuilder::CreateTexture()` for transient mip chain
- T6a.0.1: `IDevice::CreateTextureView()` for per-mip storage views, `IDevice::CreateComputePipeline()`

### Technical Direction
- **Multi-pass compute downsample** (Phase 6a): 1 dispatch per mip level, pipeline barrier between levels. Simple, universally correct, <0.3ms budget. SPD single-pass upgrade deferred to Phase 14 (see `roadmap.md` Appendix B).
- **Reverse-Z convention**: miki uses Reverse-Z (near=1.0, far=0.0). HiZ stores **max** depth per tile (not min) — an object is occluded if its nearest depth > HiZ max at that tile.
- **`VK_SAMPLER_REDUCTION_MODE_MAX` sampler** (Vulkan 1.2 core): free hardware 2×2 max in texture unit — zero ALU cost per texel. Exposed via `GetSampler()` for downstream culling shaders. Pattern proven in zeux/niagara and vkguide.dev.
- **Non-pow2 correct 3×3 kernel** on odd-dimension levels (Mike Turitzin 2020 method): preserves texture-coordinate-space accuracy. Critical for CAD where thin features must not be falsely occluded. Most competitors ignore this.
- **Texture format**: R32F (storage + sampled). Matches D32_SFLOAT depth. Per-mip storage views via `CreateTextureView`.
- **Mip 0**: copy/blit from depth buffer to R32F mip 0 (not re-render).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/vgeo/HiZPyramid.h` | **public** | **H** | HiZ interface |
| Create | `shaders/vgeo/hiz_downsample.slang` | internal | L | SPD downsample |
| Create | `src/miki/vgeo/HiZPyramid.cpp` | internal | L | Mip chain + dispatch |
| Create | `tests/unit/test_hiz_pyramid.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define HiZPyramid.h + HiZPyramid.cpp skeleton (heat H)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `HiZPyramid::Create` | `(IDevice&, SlangCompiler&, uint32_t maxWidth, uint32_t maxHeight) -> Result<HiZPyramid>` | `[[nodiscard]]` static |
      | `HiZPyramid::Build` | `(ICommandBuffer&, TextureHandle depthTexture, uint32_t width, uint32_t height) -> void` | — |
      | `HiZPyramid::GetTexture` | `() const noexcept -> TextureHandle` | `[[nodiscard]]` |
      | `HiZPyramid::GetMipCount` | `() const noexcept -> uint32_t` | `[[nodiscard]]` |
      | `HiZPyramid::GetMipView` | `(uint32_t mip) const noexcept -> TextureHandle` | `[[nodiscard]]` per-mip storage view |
      | `HiZPyramid::GetSampler` | `() const noexcept -> SamplerHandle` | `[[nodiscard]]` reduction-mode MAX sampler for culling |

      Create():
      - Compile `hiz_downsample.slang` compute shader
      - Create pipeline layout (2 bindings: src sampled image, dst storage image) + push constants (8B: levelWidth, levelHeight)
      - Create R32F texture with full mip chain + per-mip storage views
      - Create `VK_SAMPLER_REDUCTION_MODE_MAX` sampler
      - RAII: move-only, destructor destroys all resources

      `[verify: compile]`

- [x] **Step 2**: Implement hiz_downsample.slang compute shader

      - Workgroup size 16×16 (256 threads)
      - Each thread writes one texel of the destination mip
      - Sample source mip via reduction sampler (free hardware max of 2×2)
      - Non-pow2 handling: if source dimension is odd, sample 3×3 kernel (Mike Turitzin method)
        - Push constants carry `prevLevelWidth` and `prevLevelHeight` for odd-dimension detection
      - `imageStore(dstMip, ivec2(pos), vec4(maxDepth))`

      `[verify: compile]`

- [x] **Step 3**: Implement Build() C++ multi-pass dispatch

      - Mip 0: CopyTextureToTexture (D32F → R32F) or compute copy
      - Mip 1..N: for each level, bind src=mipView[i-1] + sampler, dst=mipView[i], dispatch ceil(w/16)×ceil(h/16), barrier
      - Pipeline barrier between each mip pass (compute→compute)

      `[verify: compile]`

- [x] **Step 4**: Add to CMakeLists.txt + basic compilation test
      `[verify: compile]`

- [x] **Step 5**: Unit tests with GPU readback verification

      - CreateValid, MipCount, Build_ProducesCorrectMax, Build_ReverseZ, Build_NonPowerOfTwo, Perf, EndToEnd
      - Parametric: 10+ size variants including non-pow2 (1920×1080, 2560×1440, 3840×2160, 1024×768, 800×600, 1×1, 2×2, 3×3, 64×64, 7×5)

      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(HiZPyramid, CreateValid)` | Positive | Create returns valid instance | 1-2 |
| `TEST(HiZPyramid, MipCount_1920x1080)` | Positive | ceil(log2(max(1920,1080)))+1 mip levels | 1-3 |
| `TEST(HiZPyramid, Build_ProducesCorrectMax)` | Positive | known depth pattern → readback mip 1 matches expected max | 2-3 |
| `TEST(HiZPyramid, Build_ReverseZ_MaxNotMin)` | Positive | with Reverse-Z, HiZ stores max (closest = largest value) | 2-3 |
| `TEST(HiZPyramid, Build_NonPowerOfTwo)` | Boundary | odd-sized depth buffer handled | 2-3 |
| `TEST(HiZPyramid, Perf_4K_Under300us)` | Benchmark | 3840×2160 build < 0.3ms | 2-3 |
| `TEST(HiZPyramid, EndToEnd_BuildAndSample)` | **Integration** | Build → sample mip in compute → readback → verify | 2-3 |

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Downsample approach | Multi-pass (1 dispatch/mip) | Simple, universally correct, <0.3ms. SPD single-pass deferred to Phase 14 |
| Binding model | 2× StorageTexture (imageLoad/imageStore) | Avoids CombinedImageSampler layout conflict (GENERAL vs SHADER_READ_ONLY). Both bindings in GENERAL layout |
| Mip 0 semantics | Half-res of depth buffer | Matches UE5 Nanite / niagara / vkguide convention. Full-res depth is the depth attachment itself |
| Non-pow2 handling | 3×3 kernel on odd dimensions (Turitzin 2020) | Every dst texel samples 3 src texels when src dimension is odd, not just edge texels. Critical for CAD thin features |
| Reduction sampler | `VK_SAMPLER_REDUCTION_MODE_MAX` | Free hardware max — zero ALU cost per texel lookup for downstream culling shaders |
| Push constants | 8B: `{srcWidth, srcHeight}` | Minimal per-dispatch state. Shader derives dstWidth/dstHeight and odd-dimension flags |
| Descriptor model | Push descriptor set (no descriptor pool) | One-shot per-mip binding, no pool management overhead |

## Implementation Notes

- **Barrier fix (2026-03-22)**: Original `Build()` used execution-only barrier (empty `textureBarriers` span) between mip dispatches. This violated Vulkan spec — storage image writes require explicit memory barrier (`ShaderWrite→ShaderRead`) for visibility. Fixed by inserting per-mip `TextureBarrier` with `General→General` layout transition. Small-size tests passed by coincidence (GPU cache UB); 128x128 `EndToEnd` test exposed the data corruption.
- **Test readback fix (2026-03-22)**: `ReadbackMip` originally passed mip view handle to `CopyTextureToBuffer`, but Vulkan backend resolves view→parent image and uses `iCopyInfo.mipLevel` (default 0). Fixed to use parent texture handle + explicit `.mipLevel = mip`.
- **Test upload fix (2026-03-22)**: `MakeDepthTexture` was missing `Undefined→TransferDst` layout transition before `CopyBufferToTexture`. Added explicit barrier.
- 21 tests: 5 positive, 1 boundary (2×2), 1 non-pow2 (3×3), 11 parametric size sweep, 1 end-to-end, 1 reverse-Z, 1 mip view validity. All pass on Vulkan with validation enabled.
- **Contract check: PASS** (2026-03-22). 13/13 signature items match. 3/3 consumers satisfied. 7/7 direction items followed.
