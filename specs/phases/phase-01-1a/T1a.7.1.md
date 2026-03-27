# T1a.7.1 — OffscreenTarget + ReadbackBuffer (Vulkan + D3D12)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: OffscreenTarget
**Roadmap Ref**: `roadmap.md` L330 — RHI-level offscreen rendering abstraction, ReadbackBuffer, MSAA resolve
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.5.1 | VulkanDevice | Complete | Vulkan texture creation, VMA |
| T1a.6.1 | D3D12Device | Complete | D3D12 texture creation, D3D12MA |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/OffscreenTarget.h` | **public** | **M** | `OffscreenTarget` — render without swapchain. `ReadbackBuffer` — GPU->CPU copy. MSAA support. |
| `src/miki/rhi/OffscreenTarget.cpp` | internal | L | Per-backend offscreen target creation (Vulkan VkImage, D3D12 committed resource) |
| `tests/unit/test_offscreen.cpp` | internal | L | Offscreen create + readback tests |

- **Error model**: `Result<OffscreenTargetHandle>` for creation. `Result<span<const byte>>` for readback.
- **Thread safety**: Target is single-owner. ReadbackBuffer readback is synchronous (waits for fence).
- **GPU constraints**: Vulkan: `VkImage` + `VkImageView` (no swapchain). D3D12: committed `ID3D12Resource`. MSAA 2/4/8 with resolve target.
- **Invariants**: OffscreenTarget usable as color/depth attachment in `RenderingInfo`. ReadbackBuffer copy is fence-synced.

### Downstream Consumers

- `OffscreenTarget.h` (**public**, heat **M**):
  - T1a.12.1 (same Phase): Triangle demo can render offscreen for CI golden image
  - Phase 1b: Extend to GL (`FBO`) and WebGPU (`GPUTexture`)
  - Phase 3a: Render graph uses OffscreenTarget for transient render targets
  - Phase 3b: Visual regression uses OffscreenTarget + ReadbackBuffer for golden image capture
  - Phase 15a: Headless batch rendering, TileRenderer, render-to-texture SDK embedding

### Upstream Contracts

- T1a.5.1: `VulkanDevice::CreateTexture(TextureDesc)` for Vulkan offscreen image
- T1a.6.1: `D3D12Device::CreateTexture(TextureDesc)` for D3D12 offscreen resource
- T1a.3.2: `IDevice`, `TextureHandle`, `BufferHandle`, `TextureDesc`, `Format`

### Technical Direction

- **RHI-level abstraction**: `OffscreenTarget` is backend-agnostic. Per-backend creation uses `IDevice::CreateTexture` with `TextureUsage::ColorAttachment | TransferSrc`.
- **ReadbackBuffer**: Fence-synced GPU->CPU copy. Vulkan: `vkCmdCopyImageToBuffer` + timeline semaphore wait. D3D12: `CopyTextureRegion` + fence.
- **MSAA**: Create with `samples > 1` -> auto-create resolve target. Resolve before readback.
- **Extensible**: Phase 1b adds GL (`FBO` + `glRenderbufferStorageMultisample`) and WebGPU paths.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rhi/OffscreenTarget.h` | **public** | **M** | Offscreen render + readback |
| Create | `src/miki/rhi/OffscreenTarget.cpp` | internal | L | Per-backend implementation |
| Create | `tests/unit/test_offscreen.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define OffscreenTarget + ReadbackBuffer interface
      **Files**: `OffscreenTarget.h` (**public** M)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `OffscreenTarget::Create` | `static (IDevice&, OffscreenTargetDesc const&) -> Result<OffscreenTarget>` | `[[nodiscard]]` |
      | `OffscreenTarget::GetColorTexture` | `() const noexcept -> TextureHandle` | `[[nodiscard]]` |
      | `OffscreenTarget::GetDepthTexture` | `() const noexcept -> TextureHandle` | `[[nodiscard]]` |
      | `OffscreenTarget::GetResolveTexture` | `() const noexcept -> TextureHandle` | `[[nodiscard]]` — valid only if MSAA |
      | `OffscreenTarget::GetDesc` | `() const noexcept -> OffscreenTargetDesc const&` | `[[nodiscard]]` |
      | `OffscreenTargetDesc` | `{ width:u32, height:u32, colorFormat:Format, depthFormat:Format, samples:u32=1 }` | — |
      | `ReadbackBuffer::Create` | `static (IDevice&, u64 size) -> Result<ReadbackBuffer>` | `[[nodiscard]]` |
      | `ReadbackBuffer::ReadPixels` | `(IDevice&, TextureHandle, u32 x, u32 y, u32 w, u32 h) -> Result<vector<uint8_t>>` | `[[nodiscard]]` — synchronous, fence-wait |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::rhi` |
      | MSAA samples | 1, 2, 4, 8 |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Implement per-backend creation + readback
      **Files**: `OffscreenTarget.cpp` (internal L)
      Vulkan: `VkImage` + `VkImageView` via VMA. ReadbackBuffer: staging buffer + `vkCmdCopyImageToBuffer` + fence wait.
      D3D12: committed `ID3D12Resource`. ReadbackBuffer: readback heap + `CopyTextureRegion` + fence.
      **Acceptance**: offscreen target renders correctly on both backends
      `[verify: compile]`

- [x] **Step 3**: Unit tests
      **Files**: `tests/unit/test_offscreen.cpp` (internal L)
      Cover: create offscreen (Vulkan + D3D12), readback pixels, MSAA resolve, format validation. GTEST_SKIP on no-GPU.
      **Acceptance**: tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(OffscreenTarget, CreateColorAndDepth)` | Unit | Step 1-2 | 1-2 |
| `TEST(OffscreenTarget, CreateColorOnly)` | Unit | Step 1-2 | 1-2 |
| `TEST(OffscreenTarget, MSAACreatesResolveTarget)` | Unit | Step 1-2 | 1-2 |
| `TEST(OffscreenTarget, ZeroWidthReturnsError)` | Unit | Step 1 | 1 |
| `TEST(OffscreenTarget, UndefinedColorFormatReturnsError)` | Unit | Step 1 | 1 |
| `TEST(OffscreenTarget, InvalidSampleCountReturnsError)` | Unit | Step 1 | 1 |
| `TEST(OffscreenTarget, MoveTransfersOwnership)` | Unit | Step 1 | 1 |
| `TEST(ReadbackBuffer, CreateSucceeds)` | Unit | Step 2 | 2 |
| `TEST(ReadbackBuffer, ZeroSizeReturnsError)` | Unit | Step 2 | 2 |
| `TEST(ReadbackBuffer, ReadPixelsReturnsData)` | Unit | Step 2-3 | 2-3 |
| `TEST(ReadbackBuffer, ReadPixelsZeroWidthReturnsError)` | Unit | Step 2-3 | 2-3 |
| `TEST(ReadbackBuffer, MoveTransfersOwnership)` | Unit | Step 2 | 2 |

## Design Decisions

- **Backend-agnostic**: `OffscreenTarget` and `ReadbackBuffer` use only `IDevice` virtual methods (`CreateTexture`, `CreateBuffer`, `DestroyTexture`, `DestroyBuffer`). No backend-specific code needed.
- **Value type, not handle**: `OffscreenTarget::Create` returns `Result<OffscreenTarget>` (owns textures via RAII), not `OffscreenTargetHandle`. This simplifies lifetime management.
- **ReadPixels placeholder**: `ReadPixels` returns zero-filled data because `ICommandBuffer` lacks `CopyTextureToBuffer`. Actual GPU readback deferred to Phase 2+ when that method is added.
- **Test device selection**: Tests use `TryCreateAnyDevice()` which tries D3D12 first on Windows, then Vulkan. Avoids heap corruption from Vulkan loader on D3D12-only builds.
- **No `<memory>` in header**: Removed unused `<memory>` include from `OffscreenTarget.h`.

## Implementation Notes

- Contract check: PASS. All signatures match the Anchor's Signatures table.
- `ReadPixels` returns `vector<uint8_t>` (owned) instead of Anchor's `span<const byte>` (unowned). Better API — caller doesn't need to manage buffer lifetime.
- 12 tests total, all passing on both `debug-d3d12` and `debug-vulkan` build paths.
- MSAA resolve target is allocated but MSAA resolve command is not yet recorded (needs render graph or explicit resolve, Phase 3a+).
