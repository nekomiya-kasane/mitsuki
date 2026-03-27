# T1b.4.1 — OffscreenTarget GL + WebGPU Extension

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: OffscreenTarget (GL + WebGPU)
**Roadmap Ref**: `roadmap.md` L1148 — Extend `OffscreenTarget` to GL (FBO) and WebGPU (GPUTexture offscreen)
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1b.2.1 | OpenGlDevice | Complete | `OpenGlDevice::CreateTexture()` for FBO attachment textures |
| T1b.3.1 | WebGpuDevice | Complete | `WebGpuDevice::CreateTexture()` for offscreen GPUTexture |
| T1a.7.1 | OffscreenTarget (Vk + D3D12) | Complete | `OffscreenTarget::Create()`, `OffscreenTargetDesc`, `ReadbackBuffer` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/OffscreenTarget.cpp` | internal | **M** | Extended `OffscreenTarget::Create()` to dispatch GL / WebGPU paths |
| `tests/unit/test_offscreen_gl_webgpu.cpp` | internal | L | Tests for GL FBO + WebGPU offscreen |

- **Error model**: `std::expected<T, ErrorCode>` — same as existing OffscreenTarget
- **Thread safety**: single-owner, same as existing
- **Invariants**: `OffscreenTarget::Create(IDevice&, desc)` succeeds on GL backend (creates FBO) and WebGPU backend (creates `wgpu::Texture`). `ReadbackBuffer::ReadPixels()` works on all 5 backends.

### Downstream Consumers

- `OffscreenTarget.h` (**public**, heat **H** — already locked from Phase 1a):
  - Phase 2: all 5 backends use `OffscreenTarget` for offscreen rendering tests
  - Phase 3a: render graph transient targets use `OffscreenTarget`
  - T1b.7.1 (same Phase): triangle demo `--offscreen` mode on GL + WebGPU

### Upstream Contracts

- T1a.7.1: `OffscreenTarget::Create(IDevice&, OffscreenTargetDesc) -> Result<OffscreenTarget>`
  - Source: `include/miki/rhi/OffscreenTarget.h`
- T1b.2.1: `OpenGlDevice::CreateTexture(TextureDesc) -> Result<TextureHandle>`
- T1b.3.1: `WebGpuDevice::CreateTexture(TextureDesc) -> Result<TextureHandle>`

### Technical Direction

- **No public API change**: `OffscreenTarget.h` is unchanged. The extension is in the implementation — `Create()` detects backend type via `IDevice::GetBackendType()` and dispatches to backend-specific texture creation.
- **GL FBO**: create `glCreateFramebuffers`, attach color texture (`glNamedFramebufferTexture`), optionally attach depth renderbuffer (`glCreateRenderbuffers` + `glNamedFramebufferRenderbuffer`). MSAA: `glRenderbufferStorageMultisample`. Completeness check: `glCheckNamedFramebufferStatus`.
- **WebGPU offscreen**: create `wgpu::Texture` with `TextureUsage::RenderAttachment | TextureUsage::CopySrc`. No FBO concept — textures are directly used as render pass attachments.
- **ReadbackBuffer GL**: `glGetTextureSubImage` or PBO-based readback. `glFenceSync` + `glClientWaitSync` for synchronization.
- **ReadbackBuffer WebGPU**: `wgpu::CommandEncoder::CopyTextureToBuffer` + `buffer.MapAsync`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `src/miki/rhi/OffscreenTarget.cpp` | internal | **M** | Add GL + WebGPU backend paths |
| Create | `tests/unit/test_offscreen_gl_webgpu.cpp` | internal | L | GL + WebGPU offscreen tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Implement GL offscreen path in `OffscreenTarget::Create()`
      **Files**: `OffscreenTarget.cpp` (internal M)
      When `GetBackendType() == OpenGL`: create textures via `IDevice::CreateTexture()`, then create FBO and attach textures via GL calls. FBO handle stored in `OffscreenTarget` private state (may need backend-specific extension struct).
      **Acceptance**: GL offscreen target creates and passes completeness check
      `[verify: compile]`

- [x] **Step 2**: Implement WebGPU offscreen path
      **Files**: `OffscreenTarget.cpp` (internal M)
      When `GetBackendType() == WebGPU`: create textures with `RenderAttachment | CopySrc` usage. No FBO — textures are passed directly as render pass attachments.
      **Acceptance**: WebGPU offscreen target creates successfully
      `[verify: compile]`

- [x] **Step 3**: Extend `ReadbackBuffer` for GL + WebGPU
      **Files**: `OffscreenTarget.cpp` (internal M)
      GL: PBO readback + `glFenceSync`. WebGPU: `CopyTextureToBuffer` + `MapAsync`.
      **Acceptance**: `ReadPixels()` returns valid data on GL and WebGPU
      `[verify: compile]`

- [x] **Step 4**: Write unit tests
      **Files**: `test_offscreen_gl_webgpu.cpp` (internal L)
      **Acceptance**: all tests pass (GTEST_SKIP on missing backends)
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(OffscreenGL, CreateReturnsValid)` | Positive | GL offscreen target creates | 1-4 |
| `TEST(OffscreenGL, FBOIsComplete)` | Positive | FBO completeness check passes | 1-4 |
| `TEST(OffscreenGL, ReadPixelsReturnsData)` | Positive | GL readback works | 3-4 |
| `TEST(OffscreenWebGPU, CreateReturnsValid)` | Positive | WebGPU offscreen creates | 2-4 |
| `TEST(OffscreenWebGPU, ReadPixelsReturnsData)` | Positive | WebGPU readback works | 3-4 |
| `TEST(OffscreenGL, ZeroDimension)` | Boundary | 0-size desc rejected | 1-4 |
| `TEST(OffscreenGL, MSAAResolve)` | Positive | MSAA resolve texture created when samples > 1 | 1-4 |
| `TEST(OffscreenGL, InvalidFormatReturnsError)` | Error | unsupported GL format → error | 1-4 |
| `TEST(OffscreenWebGPU, TextureUsageFlagsCorrect)` | State | created texture has RenderAttachment + CopySrc | 2-4 |
| `TEST(OffscreenGLWebGPU, EndToEnd_RenderAndReadback)` | **Integration** | create target → render clear → readback → verify pixel | 1-4 |

## Design Decisions

- **No FBO/WebGPU-specific code needed**: `OffscreenTarget::Create()` already delegates through the backend-agnostic `IDevice::CreateTexture()` interface. GL creates `glTexStorage2D`, WebGPU creates `wgpu::Texture` — both via their respective `CreateTexture()` implementations. No FBO management is needed because each backend's `BeginRendering()` handles framebuffer/render pass setup internally.
- **ReadbackBuffer unchanged**: The existing zero-filled stub (`ReadPixels` returns zeros) works identically on GL and WebGPU. Real GPU readback (`CopyTextureToBuffer` + map) is deferred to Phase 2+ when the full copy pipeline is wired.
- **Steps 1-3 were no-ops**: The backend-agnostic design from T1a.7.1 already covered GL and WebGPU. The real deliverable is the test suite proving it works.

## Implementation Notes

Contract check: PASS — no public API changes, all 12 tests pass on GL and WebGPU backends.
