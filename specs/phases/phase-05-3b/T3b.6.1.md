# T3b.6.1 — Bloom: Extract + 6-Level Gaussian Mip Chain + Composite

**Phase**: 05-3b
**Component**: 6 — Bloom
**Roadmap Ref**: `rendering-pipeline-architecture.md` S14.3 Bloom
**Status**: Complete
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.1.2 | Tech Debt B | Not Started | FrameResources |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `Bloom.h` | **public** | **M** | `Bloom` -- brightness extract, 6-level separable Gaussian, additive composite |
| `bloom.slang` | internal | L | Compute (T1/T2) + fragment fallback (T3/T4) shaders |

- Brightness extract: threshold HDR at luminance > 1.0, downsample to 1/2
- 6-level Gaussian blur chain: separable 9-tap per level (1/2 to 1/64)
- Upsample + additive blend back up
- Composite: lerp(HDR, HDR + bloom, bloomIntensity)
- Push constants: bloomThreshold, bloomIntensity, bloomRadius
- Compute path (T1/T2): dispatch per mip level
- Fragment path (T3/T4): fullscreen triangle per mip level
- Budget: less than 0.5ms T1/T2, less than 0.8ms T3/T4

### Downstream Consumers

- T3b.16.2: demo integrates bloom pass
- Phase 7a-2: bloom remains active in full post-process chain

### Technical Direction

- RenderGraph node: conditional (disabled = zero cost)
- 6 transient textures at mip resolutions (reuse via aliasing where possible)
- Separable Gaussian: horizontal + vertical per level = 12 dispatches total
- Karis average on first downsample to prevent firefly artifacts (bright sub-pixel features)

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/Bloom.h` | **public** | **M** | Interface |
| Create | `src/miki/rendergraph/Bloom.cpp` | internal | L | Implementation |
| Create | `shaders/rendergraph/bloom_extract.slang` | internal | L | Brightness extract |
| Create | `shaders/rendergraph/bloom_blur.slang` | internal | L | Separable Gaussian |
| Create | `shaders/rendergraph/bloom_composite.slang` | internal | L | Additive composite |
| Create | `tests/unit/test_bloom.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define Bloom interface (Create/Setup/AddToGraph/Execute)
- [x] **Step 2**: Implement brightness extract + Karis average
- [x] **Step 3**: Implement 6-level downsample/upsample chain
- [x] **Step 4**: Implement composite pass
- [x] **Step 5**: Fragment fallback for T3/T4
- [x] **Step 6**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(Bloom, CreateReturnsValid)` | Positive | factory success |
| `TEST(Bloom, ExtractThreshold)` | Positive | pixels below threshold = black |
| `TEST(Bloom, MipChainDimensions)` | Positive | each level half of previous |
| `TEST(Bloom, CompositeAddsBloom)` | Positive | output brighter than input for bright pixels |
| `TEST(Bloom, ZeroIntensity_NoEffect)` | Boundary | bloomIntensity=0 passes through unchanged |
| `TEST(Bloom, KarisAverage_NoFirefly)` | Positive | single bright pixel does not dominate |
| `TEST(Bloom, ComputePath_T1)` | Positive | compute dispatch on Tier1 |
| `TEST(Bloom, EndToEnd_BrightScene)` | Integration | bright sphere produces visible bloom halo |
| `TEST(Bloom, Create_NullDevice_Error)` | Error | null device returns error |
| `TEST(Bloom, 1x1Input_NoMipChain)` | Boundary | 1x1 input produces no mip chain, passthrough |
| `TEST(Bloom, MoveSemantics)` | State | move transfers pipeline + mip textures, source empty |
| `TEST(Bloom, FragmentPath_T3)` | Positive | Tier3 uses fragment path instead of compute |
