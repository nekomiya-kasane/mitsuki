# T1a.3.1 — RhiTypes + Handle + ExternalContext + Format

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: RHI Core — Injection Architecture
**Roadmap Ref**: `roadmap.md` L326, L372 — `RhiTypes.h`, typed `Handle<Tag>`, `ExternalContext` variant, `Format` enum, `PipelineStage`
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.2.1 | ErrorCode + Result + Types | Not Started | `ErrorCode`, `Result<T>`, `float3/4/4x4` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/RhiTypes.h` | **public** | **H** | `Handle<Tag>`, `TextureHandle`, `BufferHandle`, `PipelineHandle`, `Format`, `PipelineStage`, `ExternalContext`, `DeviceConfig`, descriptors |
| `include/miki/rhi/Format.h` | **public** | **H** | `Format` enum (pixel formats: R8, RGBA8, RGBA16F, D32F, BC1-7, etc.) |
| `tests/unit/test_rhi_types.cpp` | internal | L | Handle lifecycle, ExternalContext variant tests |

- **Error model**: Types are value types — no error returns. `ExternalContext` validation happens in `IDevice::CreateFromExisting`.
- **Thread safety**: All types are immutable value types — thread-safe by design.
- **GPU constraints**: `Handle<Tag>` is 8 bytes. Descriptors `alignas(16)` where GPU-shared.
- **Invariants**: `Handle<Tag>` null handle has index == 0 and generation == 0. `Format::Undefined` == 0.

### Downstream Consumers

- `RhiTypes.h` (**public**, heat **H**):
  - T1a.3.2 (same Phase): `IDevice` uses `Handle<Tag>`, `ExternalContext`, `TextureHandle`, `BufferHandle`
  - T1a.3.3 (same Phase): `GpuCapabilityProfile` references `Format` for supported format queries
  - T1a.5.1, T1a.6.1, T1a.8.1 (same Phase): Backend devices use all handle/format types
  - Phase 1b: GL/WebGPU backends use same types
  - Phase 2+: Every rendering phase uses these types
- `Format.h` (**public**, heat **H**):
  - T1a.7.1: `OffscreenTarget` creation uses `Format`
  - Phase 2: GBuffer formats, material textures
  - Phase 3a: Render graph resource descriptors

### Upstream Contracts

- T1a.2.1: `ErrorCode` (enum class : uint32_t), `Result<T>` (std::expected alias), `float3/4/4x4` types

### Technical Direction

- **Typed handles**: `Handle<Tag>` pattern — zero-cost type safety. `struct TextureTag {}; using TextureHandle = Handle<TextureTag>;`. 8 bytes: `[generation:16][index:32][type:16]`.
- **ExternalContext as variant**: `std::variant<VulkanExternalContext, D3D12ExternalContext, OpenGlExternalContext, WebGpuExternalContext>`. Each holds API-specific pointers/handles.
- **Format enum**: Comprehensive pixel format list. Maps 1:1 to `VkFormat` / `DXGI_FORMAT` / GL internal format / WGSL format. Conversion functions are backend-internal.
- **No API-specific includes in public headers**: `RhiTypes.h` must NOT `#include <vulkan/vulkan.h>` etc. Use `void*` or opaque handles in `ExternalContext` fields; backend casts internally.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rhi/RhiTypes.h` | **public** | **H** | Core RHI types — 10+ consumers |
| Create | `include/miki/rhi/Format.h` | **public** | **H** | Pixel format enum |
| Create | `tests/unit/test_rhi_types.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define `Handle<Tag>` + concrete handle aliases + `Format` enum
      **Files**: `RhiTypes.h` (**public** H), `Format.h` (**public** H)

      **Signatures** (`RhiTypes.h`):

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `Handle<Tag>` | `{ _value: uint64_t }` — `[generation:16\|index:32\|type:16]` | `constexpr`, `[[nodiscard]]` accessors |
      | `Handle<Tag>::IsValid` | `() const noexcept -> bool` | `[[nodiscard]]` `constexpr` |
      | `Handle<Tag>::Index` | `() const noexcept -> uint32_t` | `[[nodiscard]]` `constexpr` |
      | `Handle<Tag>::Generation` | `() const noexcept -> uint16_t` | `[[nodiscard]]` `constexpr` |
      | `TextureHandle` | `using TextureHandle = Handle<TextureTag>` | — |
      | `BufferHandle` | `using BufferHandle = Handle<BufferTag>` | — |
      | `PipelineHandle` | `using PipelineHandle = Handle<PipelineTag>` | — |
      | `SamplerHandle` | `using SamplerHandle = Handle<SamplerTag>` | — |
      | `ShaderHandle` | `using ShaderHandle = Handle<ShaderTag>` | — |
      | `PipelineStage` | `enum class : uint32_t { TopOfPipe, VertexShader, FragmentShader, ComputeShader, Transfer, BottomOfPipe, ... }` | — |
      | `AccessFlags` | `enum class : uint32_t { None, ShaderRead, ShaderWrite, TransferRead, TransferWrite, ColorAttachmentWrite, DepthStencilWrite, ... }` | — |
      | `TextureLayout` | `enum class : uint32_t { Undefined, General, ColorAttachment, DepthStencilAttachment, ShaderReadOnly, TransferSrc, TransferDst, PresentSrc }` | — |

      **Signatures** (`Format.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `Format` | `enum class : uint32_t { Undefined=0, R8_UNORM, RG8_UNORM, RGBA8_UNORM, RGBA8_SRGB, BGRA8_UNORM, BGRA8_SRGB, R16_FLOAT, RG16_FLOAT, RGBA16_FLOAT, R32_FLOAT, RG32_FLOAT, RGBA32_FLOAT, R32_UINT, D16_UNORM, D32_FLOAT, D24_UNORM_S8_UINT, D32_FLOAT_S8_UINT, BC1_UNORM, BC3_UNORM, BC5_UNORM, BC7_UNORM, ... }` | — |
      | `FormatInfo` | `(Format) -> FormatDesc { bytesPerPixel, blockSize, isCompressed, isDepth, isStencil, channelCount }` | `[[nodiscard]]` `constexpr` |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | `sizeof(Handle<Tag>)` | `== 8` |
      | `Format::Undefined` | `== 0` |
      | Namespace | `miki::rhi` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Define `ExternalContext` + `DeviceConfig`
      **Files**: `RhiTypes.h` (**public** H) — append to same file

      **Signatures**:

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `VulkanExternalContext` | `{ instance:void*, physicalDevice:void*, device:void*, graphicsQueueFamily:u32, graphicsQueueIndex:u32, computeQueueFamily:u32, computeQueueIndex:u32, transferQueueFamily:u32, transferQueueIndex:u32, surface:void* }` | All `void*` to avoid Vulkan includes |
      | `D3D12ExternalContext` | `{ device:void*, factory:void*, commandQueue:void* }` | All `void*` |
      | `OpenGlExternalContext` | `{ getProcAddress:void* }` | Function pointer |
      | `WebGpuExternalContext` | `{ device:void* }` | — |
      | `ExternalContext` | `std::variant<VulkanExternalContext, D3D12ExternalContext, OpenGlExternalContext, WebGpuExternalContext>` | — |
      | `DeviceConfig` | `{ preferredBackend:BackendType, enableValidation:bool, enableProfiling:bool }` | — |
      | `BackendType` | `enum class : uint8_t { Vulkan, D3D12, OpenGL, WebGPU, Mock }` | — |
      | `NativeImageHandle` | `{ handle:void*, type:BackendType }` | For `ImportSwapchainImage` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 3**: Unit tests for Handle and ExternalContext
      **Files**: `tests/unit/test_rhi_types.cpp` (internal L)
      Cover: Handle null/valid, generation increment, index extraction, type safety (TextureHandle != BufferHandle at type level), ExternalContext variant visit, Format info queries.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(Handle, NullIsInvalid)` | Unit | Step 1 — null handle | 1 |
| `TEST(Handle, ValidHandle)` | Unit | Step 1 — constructed handle | 1 |
| `TEST(Handle, GenerationIndex)` | Unit | Step 1 — field extraction | 1 |
| `TEST(Handle, SizeIs8)` | Unit | Step 1 — sizeof == 8 | 1 |
| `TEST(Format, UndefinedIsZero)` | Unit | Step 1 — Format::Undefined == 0 | 1 |
| `TEST(Format, InfoBytesPerPixel)` | Unit | Step 1 — correct BPP | 1 |
| `TEST(ExternalContext, VulkanVariant)` | Unit | Step 2 — variant holds Vulkan | 2 |
| `TEST(ExternalContext, D3D12Variant)` | Unit | Step 2 — variant holds D3D12 | 2 |
| `TEST(ExternalContext, VisitAll)` | Unit | Step 2 — visit all alternatives | 2 |

## Design Decisions

- **INTERFACE library**: `miki::rhi` is header-only (INTERFACE target), depends on `miki::core`.
- **Handle bit layout**: `[generation:16 | index:32 | type:16]` — generation in MSB for easy age comparison, 32-bit index supports ~4 billion pool slots.
- **Format.h separated**: Large enum + constexpr FormatInfo in own header to reduce include weight for consumers that only need Format.
- **Extra handle tags**: Added `RenderPassTag` and `FramebufferTag` beyond spec minimum — needed by T1a.3.2 (ICommandBuffer).
- **Extra tests**: 16 RHI tests (beyond spec's 9) covering equality, null equality, depth/stencil flags, compressed flags, DeviceConfig defaults, NativeImageHandle.

## Implementation Notes

- Contract check: PASS (26/26 items)
- Build: 2/2 new targets, 0 errors
- Tests: 36/36 pass (16 RhiTypes + 17 Foundation + 3 Toolchain)
- No TODO/STUB/FIXME in task files
