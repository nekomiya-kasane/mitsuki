# T1b.5.1 — SlangCompiler Quad-Target (GLSL 4.30 + WGSL)

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: Slang Compiler (quad-target)
**Roadmap Ref**: `roadmap.md` L1149 — Extend `SlangCompiler` to SPIR-V + DXIL + GLSL 4.30 + WGSL
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.10.1 | SlangCompiler (SPIR-V + DXIL) | Complete | `SlangCompiler::Compile()`, `ShaderCompileDesc`, `ShaderBlob`, `ShaderTarget` |
| T1a.11.1 | SlangFeatureProbe | Complete | `SlangFeatureProbe::RunAll()`, `ProbeReport` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/shader/SlangCompiler.cpp` | internal | **H** | Extended `Compile()` to handle `ShaderTarget::GLSL` and `ShaderTarget::WGSL` |
| `include/miki/shader/SlangCompiler.h` | **public** | **H** | New `CompileQuadTarget()` method for 4-target compilation |
| `include/miki/shader/ShaderTypes.h` | **public** | **H** | `ShaderTarget::GLSL` and `ShaderTarget::WGSL` already defined — no change needed |
| `tests/unit/test_slang_quad_target.cpp` | internal | L | Quad-target compilation tests |

- **Error model**: `std::expected<T, ErrorCode>` — same as existing SlangCompiler
- **Thread safety**: single-owner, same as SlangCompiler
- **Invariants**: `Compile(desc{target=GLSL})` produces valid GLSL 4.30 source; `Compile(desc{target=WGSL})` produces valid WGSL source. `CompileQuadTarget()` produces all 4 blobs from one source file.

### Downstream Consumers

- `SlangCompiler.h` (**public**, heat **H**):
  - T1b.5.2 (same Phase): probe tests compile shaders to GLSL + WGSL targets
  - T1b.6.1 (same Phase): hot-reload recompiles to all active targets
  - T1b.2.2 (same Phase): GL backend consumes GLSL blob for `glShaderSource` + `glCompileShader`
  - T1b.3.2 (same Phase): WebGPU backend consumes WGSL blob for `wgpu::Device::CreateShaderModule`
  - T1b.7.1 (same Phase): triangle demo compiles to target matching active backend
  - Phase 2: all backends compile shaders via quad-target
  - Phase 11c: GL hardening validates GLSL output

### Upstream Contracts

- T1a.10.1: `SlangCompiler::Compile(ShaderCompileDesc) -> Result<ShaderBlob>`
  - Source: `include/miki/shader/SlangCompiler.h`
- T1a.10.1: `ShaderTarget::GLSL`, `ShaderTarget::WGSL` (already in enum)
  - Source: `include/miki/shader/ShaderTypes.h`

### Technical Direction

- **Slang GLSL backend**: Slang supports GLSL output via `SLANG_GLSL` target. Profile: `glsl_430`. Slang translates `[[vk::push_constant]]` to a UBO block in GLSL output. `[[vk::binding(N,S)]]` maps to `layout(binding=N)` in GLSL.
- **Slang WGSL backend**: Slang supports WGSL via `SLANG_WGSL` target. Push constants → uniform block at group 0 binding 0. Storage buffers use `@group(N) @binding(M)` syntax.
- **CompileQuadTarget**: new convenience method — compiles one source to all 4 targets in a single Slang session. Returns `std::array<ShaderBlob, 4>` indexed by `ShaderTarget`. More efficient than 4 separate `Compile()` calls (shared module parsing).
- **GLSL source blob**: for GLSL and WGSL, `ShaderBlob::data` contains UTF-8 source text (not binary bytecode). The GL/WebGPU backend will consume it as text.
- **CI validation**: all test shaders compiled to all 4 targets in CI. Compilation failure = test failure.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/shader/SlangCompiler.h` | **public** | **H** | Add `CompileQuadTarget()` |
| Modify | `src/miki/shader/SlangCompiler.cpp` | internal | **H** | GLSL + WGSL target handling in `Compile()`, `CompileQuadTarget()` impl |
| Create | `tests/unit/test_slang_quad_target.cpp` | internal | L | Quad-target tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Add `CompileQuadTarget()` to `SlangCompiler.h`
      **Files**: `SlangCompiler.h` (**public** H)

      **Signatures** (`SlangCompiler.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `SlangCompiler::CompileQuadTarget` | `(path, entryPoint, stage) -> Result<std::array<ShaderBlob, 4>>` | `[[nodiscard]]` |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Array index | `static_cast<size_t>(ShaderTarget::SPIRV)` = 0, DXIL = 1, GLSL = 2, WGSL = 3 |
      | Namespace | `miki::shader` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Implement GLSL + WGSL targets in `Compile()`
      **Files**: `SlangCompiler.cpp` (internal)
      Add `SLANG_GLSL` / `SLANG_WGSL` profile selection in the Slang session setup.
      GLSL profile: `glsl_430`. WGSL: `wgsl`. Output is text source in `ShaderBlob::data`.
      **Acceptance**: `Compile({target=GLSL})` and `Compile({target=WGSL})` produce non-empty blobs
      `[verify: compile]`

- [x] **Step 3**: Implement `CompileQuadTarget()`
      **Files**: `SlangCompiler.cpp` (internal)
      Single Slang session, 4 target code generations. Returns array of blobs.
      **Acceptance**: all 4 blobs non-empty for `triangle.slang`
      `[verify: compile]`

- [x] **Step 4**: Write unit tests
      **Files**: `test_slang_quad_target.cpp` (internal L)
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(SlangQuadTarget, CompileGLSLProducesOutput)` | Positive | GLSL blob non-empty | 2-4 |
| `TEST(SlangQuadTarget, CompileWGSLProducesOutput)` | Positive | WGSL blob non-empty | 2-4 |
| `TEST(SlangQuadTarget, GLSLContainsVersionDirective)` | Positive | output starts with `#version 430` | 2-4 |
| `TEST(SlangQuadTarget, WGSLContainsEntryPoint)` | Positive | WGSL output contains `@vertex` or `@fragment` | 2-4 |
| `TEST(SlangQuadTarget, QuadTargetReturns4Blobs)` | Positive | all 4 array elements non-empty | 3-4 |
| `TEST(SlangQuadTarget, InvalidSourceReturnsError)` | Error | syntax error → error for all targets | 2-4 |
| `TEST(SlangQuadTarget, PushConstantMapsToUBO_GLSL)` | Positive | GLSL output has UBO block for push constants | 2-4 |
| `TEST(SlangQuadTarget, PushConstantMapsToUBO_WGSL)` | Positive | WGSL output has uniform at group 0 binding 0 | 2-4 |
| `TEST(SlangQuadTarget, EmptySourceReturnsError)` | Boundary | empty source path → error | 2-4 |
| `TEST(SlangQuadTarget, BlobTargetFieldMatchesRequest)` | State | returned blob's `target` field equals requested ShaderTarget | 2-4 |
| `TEST(SlangQuadTarget, EndToEnd_TriangleQuadCompile)` | **Integration** | `triangle.slang` compiles to all 4 targets | 1-4 |

## Design Decisions

- **Session-per-target in CompileQuadTarget**: each target gets its own `slang::ISession` because Slang sessions are single-target. The source string is shared (parsed once in memory, but Slang re-parses per session). True single-parse-multi-codegen would require Slang multi-target sessions which are not well-supported for mixed binary/text output.
- **kTargetCount as static constexpr**: exposed on the class for consumers to use in array sizing without magic numbers.
- **Profile strings**: `glsl_430` and `wgsl` are the canonical Slang profile names. Verified against Slang source code.
- **No ShaderTypes.h changes**: `ShaderTarget::GLSL` and `ShaderTarget::WGSL` were already defined in Phase 1a, just unused.

## Implementation Notes

- Contract check: PASS (9/9 items match)
- Test count: 11 (7 Positive, 1 Error, 1 Boundary, 1 State, 1 Integration)
- Build: 0 errors, warnings only from third-party headers (VMA, RenderDoc)
- Pre-existing failures: 2 PlaygroundE2E tests (CompileDefaultFragSPIRV, CompileSimplePrintComputeSPIRV) — unrelated to this task
- Total test count after this task: 236 (225 + 11)
