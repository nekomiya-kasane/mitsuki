# T1b.2.1 — OpenGlDevice (CreateFromExisting + CreateOwned + glad)

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: OpenGL Tier4 Backend
**Roadmap Ref**: `roadmap.md` L1146 — `OpenGlDevice` wrapping externally-provided GL 4.3+ context
**Status**: Complete
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.2 | IDevice + ICommandBuffer | Complete | `IDevice` virtual interface, `IDevice::CreateFromExisting(ExternalContext)` |
| T1a.3.3 | GpuCapabilityProfile | Complete | `CapabilityTier::Tier4_OpenGL`, `GpuCapabilityProfile` struct |
| T1a.3.1 | RhiTypes | Complete | `BackendType::OpenGL`, `OpenGlExternalContext`, `Handle<Tag>` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/opengl/OpenGlDevice.h` | shared | **H** | `OpenGlDevice` — `IDevice` impl for GL 4.3+. Used by T1b.2.2, T1b.4.1. |
| `src/miki/rhi/opengl/OpenGlDevice.cpp` | internal | L | Device creation, resource CRUD, glad loader init |
| `src/miki/rhi/opengl/GlHandlePool.h` | shared | **M** | GL-specific handle→GLuint mapping. Used by T1b.2.2. |
| `src/miki/rhi/opengl/CMakeLists.txt` | internal | L | `miki_rhi_opengl` STATIC library |
| `tests/unit/test_opengl_device.cpp` | internal | L | Unit tests |

- **Error model**: `std::expected<T, ErrorCode>` — monadic, no exceptions
- **Thread safety**: single-owner, same as all IDevice implementations. GL context must be current on calling thread.
- **GPU constraints**: GL 4.3+ core profile required (compute shader + SSBO). `GL_KHR_debug` for validation messages.
- **Invariants**: `CreateFromExisting(OpenGlExternalContext)` succeeds if `getProcAddress != nullptr` and GL version >= 4.3. `CreateOwned` creates a hidden GLFW window for headless GL context (test/CI only).

### Downstream Consumers

- `OpenGlDevice.h` (shared, heat **H**):
  - T1b.2.2 (same Phase): `OpenGlCommandBuffer` needs device's GL function pointers + handle pool
  - T1b.4.1 (same Phase): `OffscreenTarget` GL extension needs `CreateTexture()` for FBO attachments
  - Phase 2: `StagingUploader` GL path needs `CreateBuffer()` with `GL_STREAM_DRAW` hint
  - Phase 11c: GL hardening audit uses `OpenGlDevice` as primary device
- `GlHandlePool.h` (shared, heat **M**):
  - T1b.2.2: command buffer maps `BufferHandle` / `TextureHandle` → `GLuint` for GL API calls

### Upstream Contracts

- T1a.3.2: `IDevice` virtual interface
  - `CreateTexture(TextureDesc) -> Result<TextureHandle>`
  - `CreateBuffer(BufferDesc) -> Result<BufferHandle>`
  - `CreateGraphicsPipeline(GraphicsPipelineDesc) -> Result<PipelineHandle>`
  - `Submit(ICommandBuffer&) -> Result<void>`
  - Source: `include/miki/rhi/IDevice.h`
- T1a.3.1: `OpenGlExternalContext { void* getProcAddress; }`
  - Source: `include/miki/rhi/RhiTypes.h`

### Technical Direction

- **glad loader**: use glad2 (GL 4.3 core profile generator). Load via `gladLoadGLContext()` with the provided `getProcAddress`. No GLEW — glad is header-only and multi-context safe.
- **Injection-first**: primary path is `CreateFromExisting(OpenGlExternalContext)` — host makes GL context current, passes `getProcAddress`. miki never creates windows or GL contexts in production. `CreateOwned` is for test/CI only (creates hidden GLFW window).
- **Handle pool**: `GlHandlePool` maps `Handle<Tag>` → `GLuint`. `glGen*` on create, `glDelete*` on destroy. Generation tracking for stale handle detection.
- **Debug callback**: `glDebugMessageCallback` registered on creation if `enableValidation`. Maps GL debug severity → miki log levels.
- **No GLFW dependency**: miki::rhi_opengl depends only on glad. GLFW is only used in `CreateOwned` (test path), linked conditionally.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/opengl/OpenGlDevice.h` | shared | **H** | Device class + GL context wrapper |
| Create | `src/miki/rhi/opengl/OpenGlDevice.cpp` | internal | L | IDevice impl |
| Create | `src/miki/rhi/opengl/GlHandlePool.h` | shared | **M** | Handle<Tag> → GLuint mapping |
| Create | `src/miki/rhi/opengl/GlHandlePool.cpp` | internal | L | Pool impl |
| Create | `src/miki/rhi/opengl/CMakeLists.txt` | internal | L | Static lib target |
| Create | `tests/unit/test_opengl_device.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |
| Modify | `src/CMakeLists.txt` | internal | L | Add opengl subdirectory |

## Steps

- [x] **Step 1**: Create `OpenGlDevice.h` (shared H) — class declaration
      **Files**: `OpenGlDevice.h` (shared H)

      **Signatures** (`OpenGlDevice.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `OpenGlDevice::CreateFromExisting` | `(OpenGlExternalContext) -> Result<unique_ptr<IDevice>>` | `[[nodiscard]]` static |
      | `OpenGlDevice::CreateOwned` | `(DeviceConfig) -> Result<unique_ptr<IDevice>>` | `[[nodiscard]]` static |
      | All `IDevice` virtuals | (see `IDevice.h`) | override |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Create `GlHandlePool` (shared M) — GL resource tracking
      **Files**: `GlHandlePool.h` (shared M), `GlHandlePool.cpp` (internal L)

      **Signatures** (`GlHandlePool.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `GlHandlePool::Allocate` | `(GLuint glId) -> Handle<Tag>` | `[[nodiscard]]` template |
      | `GlHandlePool::Resolve` | `(Handle<Tag>) -> optional<GLuint>` | `[[nodiscard]]` template |
      | `GlHandlePool::Release` | `(Handle<Tag>) -> void` | template |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 3**: Implement `OpenGlDevice.cpp` (internal L) — glad init, resource CRUD, submit
      **Files**: `OpenGlDevice.cpp` (internal L), `CMakeLists.txt` (internal L)
      Initialize glad from `getProcAddress`. Implement `CreateTexture` → `glCreateTextures`,
      `CreateBuffer` → `glCreateBuffers`, `CreateGraphicsPipeline` → `glCreateProgram` + link,
      `Submit` → flush deferred commands (delegates to `OpenGlCommandBuffer`).
      **Acceptance**: device creates successfully with a GL context
      `[verify: compile]`

- [x] **Step 4**: Write unit tests
      **Files**: `test_opengl_device.cpp` (internal L)
      Tests use `CreateOwned` (hidden GLFW window) or `GTEST_SKIP` if no GL available.
      **Acceptance**: all tests pass (or skip gracefully on headless CI)
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(OpenGlDevice, CreateOwnedReturnsValid)` | Positive | factory succeeds on GL-capable host | 3-4 |
| `TEST(OpenGlDevice, GetBackendTypeReturnsOpenGL)` | Positive | correct backend type | 3-4 |
| `TEST(OpenGlDevice, CapabilityTierIsTier4)` | Positive | GL device reports Tier4_OpenGL | 3-4 |
| `TEST(OpenGlDevice, CreateTextureReturnsValid)` | Positive | texture creation via GL | 3-4 |
| `TEST(OpenGlDevice, CreateBufferReturnsValid)` | Positive | buffer creation via GL | 3-4 |
| `TEST(OpenGlDevice, DestroyTextureNoLeak)` | State | destroy releases GL resource | 3-4 |
| `TEST(OpenGlDevice, DestroyInvalidHandleSafe)` | Boundary | double-destroy safe | 3-4 |
| `TEST(OpenGlDevice, WaitIdleNoOp)` | Boundary | `glFinish` completes | 3-4 |
| `TEST(OpenGlDevice, DebugCallbackCaptures)` | Positive | GL debug messages captured when validation enabled | 3-4 |
| `TEST(OpenGlDevice, CreateFromExisting_NullGetProcAddr)` | Error | null getProcAddress → error | 3-4 |
| `TEST(OpenGlDevice, EndToEnd_CreateAndDestroy)` | **Integration** | create device → create resources → destroy → device destroy | 1-4 |

## Design Decisions

- **glad2 generated with MX + loader**: `python -m glad --api="gl:core=4.3" --extensions="GL_KHR_debug,..." c --mx --loader` → `third_party/glad2/`. Each `OpenGlDevice` owns a `GladGLContext` struct for multi-context safety.
- **gl.c compiled as C++**: coca toolchain has no C compiler; `set_source_files_properties(... PROPERTIES LANGUAGE CXX)` used.
- **GlHandlePool is header-only**: template class, no `.cpp` needed (spec listed `GlHandlePool.cpp` but template-only is cleaner).
- **GLSL source compilation**: GL 4.3 does not include `GL_ARB_gl_spirv` (GL 4.6). Shader creation uses `glShaderSource`/`glCompileShader` instead of `glShaderBinary`/`glSpecializeShader`.
- **ErrorCode::DeviceNotReady** used for glad init failures (no `BackendInitFailed` in ErrorCode enum).
- **GL_TEXTURE_MAX_ANISOTROPY_EXT** used as raw constant `0x84FE` since `GL_EXT_texture_filter_anisotropic` not in generated glad header.
- **GLFW forward-declared via extern C**: avoids GLFW header dependency in the library. GLFW linked conditionally only when target exists.

## Implementation Notes

- **Files created**: `OpenGlDevice.h`, `OpenGlDevice.cpp`, `GlHandlePool.h`, `OpenGlDeviceFactory.h`, `CMakeLists.txt`, `test_opengl_device.cpp`
- **Files modified**: `cmake/miki_options.cmake` (added `MIKI_BUILD_OPENGL`), `src/miki/rhi/CMakeLists.txt` (added opengl subdirectory + link), `src/miki/rhi/IDevice.cpp` (added OpenGL dispatch), `tests/unit/CMakeLists.txt` (added test target)
- **Test count**: 11 tests, all pass on GL-capable host, graceful GTEST_SKIP on headless
- **Total project tests**: 280 (269 existing + 11 new OpenGL)
