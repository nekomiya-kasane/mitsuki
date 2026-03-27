# T1b.2.2 — OpenGlCommandBuffer (Deferred Cmd Recording, Push Constant UBO)

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: OpenGL Tier4 Backend
**Roadmap Ref**: `roadmap.md` L1146 — `OpenGlCommandBuffer` deferred command recording, push constant UBO emulation
**Status**: Complete
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1b.2.1 | OpenGlDevice | Complete | `OpenGlDevice`, `GlHandlePool` — resolve handles to GLuint |
| T1a.3.2 | ICommandBuffer | Complete | `ICommandBuffer` virtual interface |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/opengl/OpenGlCommandBuffer.h` | shared | **M** | `OpenGlCommandBuffer` — `ICommandBuffer` impl with deferred command list |
| `src/miki/rhi/opengl/OpenGlCommandBuffer.cpp` | internal | L | Command recording + flush implementation |
| `tests/unit/test_opengl_cmdbuf.cpp` | internal | L | Unit tests |

- **Error model**: `std::expected<T, ErrorCode>` — `Begin()`/`End()` can fail
- **Thread safety**: single-owner, same as all ICommandBuffer implementations
- **Invariants**: commands recorded between `Begin()` and `End()` are stored in `std::vector<GlCommand>`. `Submit()` on device iterates and executes each GL call. Push constants emulated via 128B UBO at binding 0 (`glBufferSubData`).

### Downstream Consumers

- `OpenGlCommandBuffer.h` (shared, heat **M**):
  - T1b.4.1 (same Phase): OffscreenTarget GL tests need command buffer for rendering
  - T1b.7.1 (same Phase): triangle demo GL path needs full draw command sequence
  - Phase 2: forward rendering GL path uses all command types
  - Phase 3a: render graph GL executor issues FBO bind/unbind + `glMemoryBarrier` through command buffer
  - Phase 11c: GL hardening exercises all command paths

### Upstream Contracts

- T1a.3.2: `ICommandBuffer` — `Begin()`, `End()`, `BeginRendering(RenderingInfo)`, `EndRendering()`, `BindPipeline()`, `PushConstants()`, `Draw()`, `DrawIndexed()`, `Dispatch()`, `PipelineBarrier()`, `SetViewport()`, `SetScissor()`
  - Source: `include/miki/rhi/ICommandBuffer.h`
- T1b.2.1: `OpenGlDevice` — provides `GlHandlePool` for handle→GLuint, GL function pointers
  - Source: `src/miki/rhi/opengl/OpenGlDevice.h`

### Technical Direction

- **Deferred command model**: `GlCommand` is a `std::variant` of typed command structs (`GlCmdBindPipeline`, `GlCmdDraw`, `GlCmdBarrier`, etc.). Recorded into `std::vector<GlCommand>`. Flushed sequentially on `Submit()`.
- **Push constant → UBO emulation**: create a persistent 128B UBO at device init. `PushConstants()` records a `GlCmdPushConstants` that does `glBufferSubData` on flush. Bound at UBO binding 0. Slang `[[vk::push_constant]]` maps to UBO block in GLSL output.
- **BeginRendering → FBO bind**: `RenderingInfo` → resolve to FBO with matching attachments. `EndRendering` → unbind FBO. FBO cache keyed by attachment set.
- **Barrier → glMemoryBarrier**: `PipelineBarrierInfo` maps to `GL_SHADER_STORAGE_BARRIER_BIT`, `GL_TEXTURE_FETCH_BARRIER_BIT`, etc.
- **No state tracking**: GL is stateful, but we defer commands and replay. Each `GlCmdBindPipeline` calls `glUseProgram`. Each `GlCmdDraw` calls `glDrawArrays`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/opengl/OpenGlCommandBuffer.h` | shared | **M** | Command buffer class + GlCommand variant |
| Create | `src/miki/rhi/opengl/OpenGlCommandBuffer.cpp` | internal | L | Record + flush impl |
| Create | `tests/unit/test_opengl_cmdbuf.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Define `GlCommand` variant + `OpenGlCommandBuffer` class
      **Files**: `OpenGlCommandBuffer.h` (shared M)

      **Signatures** (`OpenGlCommandBuffer.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `OpenGlCommandBuffer` | class implementing `ICommandBuffer` | — |
      | All `ICommandBuffer` virtuals | (see `ICommandBuffer.h`) | override |
      | `GlCommand` | `std::variant<GlCmdBeginRendering, GlCmdEndRendering, GlCmdBindPipeline, GlCmdDraw, GlCmdDrawIndexed, GlCmdDispatch, GlCmdBarrier, GlCmdPushConstants, GlCmdSetViewport, GlCmdSetScissor, GlCmdBindVertexBuffer, GlCmdBindIndexBuffer, GlCmdCopyBufferToTexture, GlCmdCopyBuffer>` | — |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 2**: Implement command recording + push constant UBO flush
      **Files**: `OpenGlCommandBuffer.cpp` (internal L)
      Each `ICommandBuffer` method pushes the corresponding `GlCmd*` struct into the command list.
      `Flush()` (called by `OpenGlDevice::Submit`) iterates and executes GL calls.
      Push constant UBO: `glBufferSubData` into a persistent 128B buffer at binding 0.
      **Acceptance**: compiles, push constant data arrives at shader
      `[verify: compile]`

- [x] **Step 3**: Write unit tests
      **Files**: `test_opengl_cmdbuf.cpp` (internal L)
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(OpenGlCmdBuf, BeginEndSucceeds)` | Positive | lifecycle works | 1-3 |
| `TEST(OpenGlCmdBuf, RecordDrawCommands)` | Positive | Draw/DrawIndexed recorded | 1-3 |
| `TEST(OpenGlCmdBuf, PushConstantsRecorded)` | Positive | push constants stored in command list | 1-3 |
| `TEST(OpenGlCmdBuf, BarrierMapsToGlBarrier)` | Positive | barrier command recorded with correct GL bits | 1-3 |
| `TEST(OpenGlCmdBuf, EmptyCommandList)` | Boundary | begin + end with no commands is valid | 1-3 |
| `TEST(OpenGlCmdBuf, RecordBeforeBeginFails)` | Error | Draw() before Begin() → error | 1-3 |
| `TEST(OpenGlCmdBuf, PushConstantSizeTracked)` | State | push constant offset + size recorded correctly in command | 1-3 |
| `TEST(OpenGlCmdBuf, EndToEnd_RecordAndFlush)` | **Integration** | record full draw sequence → flush → GL state correct | 1-3 |

## Design Decisions

- **GlCommand = 14-alternative std::variant**: `GlCmdBeginRendering`, `GlCmdEndRendering`, `GlCmdBindPipeline`, `GlCmdDraw`, `GlCmdDrawIndexed`, `GlCmdDispatch`, `GlCmdBarrier`, `GlCmdPushConstants`, `GlCmdSetViewport`, `GlCmdSetScissor`, `GlCmdBindVertexBuffer`, `GlCmdBindIndexBuffer`, `GlCmdCopyBufferToTexture`, `GlCmdCopyBuffer`.
- **Push constant UBO**: 128B persistent buffer (`GL_DYNAMIC_STORAGE_BIT`) at UBO binding 0. `NamedBufferSubData` for partial updates. Created per command buffer, not per device.
- **Index type tracked at flush time**: `DrawIndexed` doesn't carry index type — tracked via `currentIndexType_` set by `ExecuteBindIndexBuffer`.
- **No move semantics**: `OpenGlCommandBuffer` holds `OpenGlDevice&` reference, preventing move assignment. Copy/move both deleted.
- **Silent drop on !recording_**: Commands issued before `Begin()` are silently ignored (no error return since void interface). `End()` before `Begin()` returns `InvalidState`.
- **Barrier mapping**: `AccessFlags` → `GLbitfield` via `mapAccess()`. Falls back to `GL_ALL_BARRIER_BITS` if no specific flags mapped.
- **BeginRendering**: Default framebuffer path only (viewport + clear). FBO cache deferred to T1b.4.1 (OffscreenTarget GL).

## Implementation Notes

- **Files created**: `OpenGlCommandBuffer.h`, `OpenGlCommandBuffer.cpp`, `test_opengl_cmdbuf.cpp`
- **Files modified**: `OpenGlDevice.cpp` (wired `CreateCommandBuffer` + `Submit`), `opengl/CMakeLists.txt` (added source), `tests/unit/CMakeLists.txt` (added test target)
- **Test count**: 8 tests, all pass on GL-capable host
- **Contract check**: PASS (8/8 items, all consumers satisfied)
