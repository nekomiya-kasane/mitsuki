# T1a.10.1 — SlangCompiler (Dual-Target, Reflection, Cache, Permutation)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: Slang Compiler
**Roadmap Ref**: `roadmap.md` L333 — `SlangCompiler` SPIR-V + DXIL dual-target, reflection, disk cache, permutations, Pimpl ABI
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.1.2 | Third-party deps | Complete | `miki::third_party::slang` CMake target |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/shader/SlangCompiler.h` | **public** | **H** | `SlangCompiler` — dual-target compilation (SPIR-V + DXIL). Reflection. Disk cache. Pimpl for ABI stability. |
| `include/miki/shader/ShaderTypes.h` | **public** | **H** | `ShaderBlob`, `ShaderReflection`, `ShaderTarget`, `ShaderPermutationKey`, `PermutationCache` |
| `src/miki/shader/SlangCompiler.cpp` | internal | L | Implementation: Slang session, compilation, reflection extraction |
| `src/miki/shader/PermutationCache.cpp` | internal | L | Lazy compile, background thread, disk cache |
| `tests/unit/test_shader.cpp` | internal | L | Compilation, reflection, cache tests (renamed from spec's `test_slang_compiler.cpp`) |

- **Error model**: `Result<ShaderBlob>` for compilation. `Result<ShaderReflection>` for reflection. Compile errors include diagnostic messages.
- **Thread safety**: `SlangCompiler` is single-owner. `PermutationCache` is thread-safe (concurrent lookups, background compilation).
- **GPU constraints**: N/A (CPU-side compilation)
- **Invariants**: Compilation produces valid SPIR-V and DXIL for same source. Reflection data matches both targets. Disk cache invalidated on source change (content hash).

### Downstream Consumers

- `SlangCompiler.h` (**public**, heat **H**):
  - T1a.11.1 (same Phase): `SlangFeatureProbe` uses compiler for regression tests
  - T1a.5.2 (same Phase): Vulkan pipeline creation uses SPIR-V blobs
  - T1a.6.2 (same Phase): D3D12 pipeline creation uses DXIL blobs
  - T1a.12.1 (same Phase): Triangle demo compiles triangle shaders
  - Phase 1b: Extend to GLSL 4.30 + WGSL (quad-target)
  - Phase 2+: All shader compilation goes through `SlangCompiler`
- `ShaderTypes.h` (**public**, heat **H**):
  - All phases: `ShaderBlob`, `ShaderReflection` used by pipeline creation
  - Phase 1b: `ShaderTarget` extended with GLSL/WGSL

### Upstream Contracts

- T1a.1.2: `miki::third_party::slang` library target (source-compiled Slang)

### Technical Direction

- **Pimpl for ABI stability**: `SlangCompiler` public header has only Pimpl pointer. Implementation details hidden.
- **Dual-target in Phase 1a**: SPIR-V + DXIL. Phase 1b adds GLSL 4.30 + WGSL (quad-target).
- **Reflection**: Extract binding info, push constant layout, root signature, vertex input layout from compiled shader.
- **Disk cache**: `ShaderPermutationKey` (64-bit bitfield) -> compiled blob. Cache keyed by `(source hash, permutation key, target)`. Stored in `cache/shaders/` directory.
- **PermutationCache**: Lazy compile on first request. Background thread for precompilation. Thread-safe concurrent access.
- **Slang integration**: Slang is consumed as prebuilt binaries in `third_party/slang-prebuilt/` (not source-compiled). `slang_imported` is a CMake IMPORTED SHARED target linking `slang-compiler.lib/.dll`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/shader/SlangCompiler.h` | **public** | **H** | Shader compiler interface |
| Create | `include/miki/shader/ShaderTypes.h` | **public** | **H** | Shader data types |
| Create | `src/miki/shader/SlangCompiler.cpp` | internal | L | Slang API integration |
| Create | `src/miki/shader/PermutationCache.cpp` | internal | L | Cache + background compile |
| Create | `tests/unit/test_shader.cpp` | internal | L | Tests (named `test_shader` to cover ShaderTypes + SlangCompiler + PermutationCache) |

## Steps

- [x] **Step 1**: Define ShaderTypes (ShaderBlob, ShaderReflection, ShaderTarget, PermutationKey)
      **Files**: `ShaderTypes.h` (**public** H)

      **Signatures** (`ShaderTypes.h`):

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `ShaderTarget` | `enum class : u8 { SPIRV, DXIL, GLSL, WGSL }` | — |
      | `ShaderStage` | `enum class : u8 { Vertex, Fragment, Compute, Mesh, Amplification, RayGen, ClosestHit, Miss, AnyHit, Intersection }` | — |
      | `ShaderBlob` | `{ data:vector<uint8_t>, target:ShaderTarget, stage:ShaderStage, entryPoint:string }` | Move-only |
      | `ShaderReflection` | `{ bindings:vector<BindingInfo>, pushConstantSize:u32, vertexInputs:vector<VertexInputInfo>, threadGroupSize:uint3 }` | — |
      | `BindingInfo` | `{ set:u32, binding:u32, type:BindingType, count:u32, name:string }` | — |
      | `BindingType` | `enum class : u8 { UniformBuffer, StorageBuffer, SampledTexture, StorageTexture, Sampler, CombinedImageSampler }` | — |
      | `VertexInputInfo` | `{ location:u32, format:Format, offset:u32, name:string }` | — |
      | `ShaderPermutationKey` | `{ bits:u64 }` — 64-bit bitfield for permutation variants | `constexpr` operators |
      | `ShaderCompileDesc` | `{ sourcePath:path, entryPoint:string, stage:ShaderStage, target:ShaderTarget, permutation:ShaderPermutationKey, defines:span<pair<string,string>> }` | — |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::shader` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Implement SlangCompiler (Pimpl + dual-target compilation + reflection)
      **Files**: `SlangCompiler.h` (**public** H), `SlangCompiler.cpp` (internal L)

      **Signatures** (`SlangCompiler.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `SlangCompiler::Create` | `static () -> Result<SlangCompiler>` | `[[nodiscard]]` |
      | `SlangCompiler::Compile` | `(ShaderCompileDesc const&) -> Result<ShaderBlob>` | `[[nodiscard]]` |
      | `SlangCompiler::CompileDualTarget` | `(path, entryPoint, stage) -> Result<pair<ShaderBlob, ShaderBlob>>` | `[[nodiscard]]` — SPIR-V + DXIL |
      | `SlangCompiler::Reflect` | `(ShaderCompileDesc const&) -> Result<ShaderReflection>` | `[[nodiscard]]` — changed from `ShaderBlob` to `ShaderCompileDesc` because reflection requires re-compiling from source |
      | `SlangCompiler::AddSearchPath` | `(path) -> void` | For `#include` resolution |
      | `~SlangCompiler` | | Pimpl destructor |

      **Acceptance**: compiles; can compile a trivial Slang shader to SPIR-V and DXIL
      `[verify: compile]`

- [x] **Step 3**: Implement PermutationCache (disk cache + lazy compile)
      **Files**: `PermutationCache.cpp` (internal L)
      Cache keyed by `(source_hash, permutation_key, target)`. Disk persistence in `cache/shaders/`. LRU eviction. Background thread for precompilation.
      **Acceptance**: cache hit avoids recompilation; disk cache survives restart
      `[verify: compile]`

- [x] **Step 4**: Unit tests
      **Files**: `tests/unit/test_shader.cpp` (internal L)
      Cover: SPIR-V compilation, DXIL compilation, dual-target, reflection extraction (bindings, push constants), permutation cache hit/miss, disk cache persistence, compile error diagnostic.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(ShaderTypes, TargetEnumValues)` | Unit | Step 1 — enum values | 1 |
| `TEST(ShaderTypes, StageEnumValues)` | Unit | Step 1 — enum values | 1 |
| `TEST(ShaderTypes, BlobMoveOnly)` | Unit | Step 1 — move semantics | 1 |
| `TEST(ShaderTypes, PermutationKeyBits)` | Unit | Step 1 — bit set/get | 1 |
| `TEST(ShaderTypes, CompileDescDefaults)` | Unit | Step 1 — default values | 1 |
| `TEST(SlangCompiler, CreateSucceeds)` | Unit | Step 2 — factory | 2 |
| `TEST(SlangCompiler, CompileComputeSPIRV)` | Unit | Step 2 — SPIR-V output valid | 2 |
| `TEST(SlangCompiler, CompileComputeDXIL)` | Unit | Step 2 — DXIL output valid (DXBC header check) | 2 |
| `TEST(SlangCompiler, CompileVertexSPIRV)` | Unit | Step 2 — vertex SPIR-V | 2 |
| `TEST(SlangCompiler, CompileInvalidPathFails)` | Unit | Step 2 — error path | 2 |
| `TEST(SlangCompiler, CompileDualTargetWorks)` | Unit | Step 2 — both targets from same source | 2 |
| `TEST(SlangCompiler, AddSearchPath)` | Unit | Step 2 — search path API | 2 |
| `TEST(SlangCompiler, ReflectionBindings)` | Unit | Step 2 — binding extraction | 2 |
| `TEST(SlangCompiler, ReflectionPushConstants)` | Unit | Step 2 — push constant size | 2 |
| `TEST(SlangCompiler, ReflectionThreadGroupSize)` | Unit | Step 2 — compute thread group size | 2 |
| `TEST(SlangCompiler, ReflectionVertexInputs)` | Unit | Step 2 — vertex input extraction | 2 |
| `TEST(SlangCompiler, ReflectionInvalidPathFails)` | Unit | Step 2 — reflection error path | 2 |
| `TEST(PermutationCache, InsertAndRetrieve)` | Unit | Step 3 — basic cache | 3 |
| `TEST(PermutationCache, CacheMissTriggersCompile)` | Unit | Step 3 — lazy compile | 3 |
| `TEST(PermutationCache, DifferentPermutationsSeparate)` | Unit | Step 3 — key isolation | 3 |
| `TEST(PermutationCache, ClearEmptiesCache)` | Unit | Step 3 — clear API | 3 |
| `TEST(PermutationCache, LRUEviction)` | Unit | Step 3 — eviction | 3 |
| `TEST(PermutationCache, InvalidCompileReturnsError)` | Unit | Step 3 — error propagation | 3 |
| `TEST(PermutationCache, DifferentTargetsSeparate)` | Unit | Step 3 — target isolation | 3 |
| `TEST(PermutationCache, DiskPersistence)` | Unit | Step 3 — survives restart | 3 |
| `TEST(PermutationCache, DiskCacheStalenessDetection)` | Unit | Step 3 — content hash stale detection | 3 |

## Design Decisions

- **Reflect() takes ShaderCompileDesc, not ShaderBlob**: Reflection requires re-compiling from source via Slang's reflection API. A compiled blob is opaque bytecode and does not carry the metadata needed. This deviates from the original spec signature.
- **Prebuilt Slang, not source-compiled**: Slang is consumed as prebuilt DLLs (`slang-compiler.dll`, `slang-llvm.dll`, etc.) in `third_party/slang-prebuilt/`. Source compilation was too fragile with the coca toolchain. CMake IMPORTED SHARED target `slang_imported` wraps it.
- **CreateSessionForDesc**: Internal helper that creates a Slang session configured with defines (from `ShaderCompileDesc.defines`) and permutation bits (mapped to `MIKI_PERMUTATION_BIT_N=1` preprocessor macros). Used by both `Compile()` and `Reflect()`.
- **CreateSessionForTarget**: Thin wrapper around `CreateSessionForDesc` for backward compat with `CompileDualTarget()` which has no full `ShaderCompileDesc`.
- **SlangTypeToFormat helper**: Maps Slang scalar type + column count to `miki::rhi::Format` for vertex input reflection.
- **Disk cache with content hash**: `.hash` sidecar file alongside blob file. On read, current source hash compared with stored hash to detect stale cache entries.
- **PermutationCache Pimpl**: Uses `std::unique_ptr<Impl>` for ABI stability, same as `SlangCompiler`.
- **Test file named `test_shader.cpp`**: Covers ShaderTypes, SlangCompiler, and PermutationCache in one file (spec originally listed `test_slang_compiler.cpp`).
- **ctest PATH**: Uses `ENVIRONMENT_MODIFICATION` with `path_list_prepend` to avoid replacing system PATH. Both Slang DLL dir and coca toolchain bin dir are prepended.

## Implementation Notes

- Contract check: PASS. All signatures match except `Reflect()` which was intentionally changed (see Design Decisions).
- `PermutationCacheConfig` added to `ShaderTypes.h` (not in original spec) to configure disk cache path, max entries, and enable flag.
- `PermutationCache.h` added as a public header (not in original spec) to expose the cache interface.
- 26 tests total (5 ShaderTypes + 12 SlangCompiler + 9 PermutationCache), all passing on `debug` build path.
- DXIL tests validate DXBC container magic bytes (`'D','X','B','C'`).
- SPIR-V tests validate magic number (`0x07230203`).
- Disk persistence test creates temp directory, writes cache, destroys cache, recreates, verifies hit.
- Staleness test modifies source file after caching, verifies cache miss on next access.
- Both `debug` (Vulkan) and `debug-d3d12` build paths compile with zero errors.
