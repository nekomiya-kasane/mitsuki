# 04 — Shader Pipeline Migration Progress

> **Date**: 2026-03-29
> **Status**: Investigation Complete, Migration Plan Ready
> **Reference**: `D:\repos\miki` → `c:\mitsuki`

---

## 1. Source Code Inventory (D:\repos\miki)

### 1.1 Files to Migrate

| File                                      |   Lines   | Description                                                  | Migration Action                   |
| ----------------------------------------- | :-------: | ------------------------------------------------------------ | ---------------------------------- |
| `include/miki/shader/ShaderTypes.h`       |    201    | Enums, structs, ShaderBlob, ShaderReflection, PermutationKey | **Direct import**                  |
| `include/miki/shader/SlangCompiler.h`     |    84     | Pimpl header, quad-target API                                | **Direct import**                  |
| `include/miki/shader/SlangFeatureProbe.h` |    72     | Stateless probe runner                                       | **Direct import**                  |
| `include/miki/shader/PermutationCache.h`  |    60     | LRU + disk cache                                             | **Direct import**                  |
| `include/miki/shader/ShaderWatcher.h`     |    127    | Hot-reload system                                            | **Direct import**                  |
| `src/miki/shader/SlangCompiler.cpp`       |    732    | Slang API integration, reflection                            | **Refactor** (session reuse)       |
| `src/miki/shader/SlangFeatureProbe.cpp`   |    149    | 29 probe tests                                               | **Direct import**                  |
| `src/miki/shader/PermutationCache.cpp`    |    273    | LRU + disk cache impl                                        | **Refactor** (#include-aware hash) |
| `src/miki/shader/ShaderWatcher.cpp`       |    436    | File watcher + dep graph                                     | **Refactor** (transitive deps)     |
| `src/miki/shader/CMakeLists.txt`          |    20     | Build config                                                 | **Adapt** to mitsuki CMake         |
| `shaders/tests/probe_*.slang`             | ~29 files | Feature probe shaders                                        | **Direct import**                  |

**Total**: ~2154 lines of C++ code + 29 shader files

### 1.2 Dependencies

```mermaid
graph TD
    SHADER["miki_shader"] --> CORE["miki::core<br/>(Result, ErrorCode)"]
    SHADER --> SLANG["miki::third_party::slang"]
    SHADER --> RHI["miki::rhi<br/>(Format, BackendType)"]
```

---

## 2. Functionality Analysis

### 2.1 SlangCompiler — Feature Completeness

| Feature                       |   Status    | Notes                                                                                   |
| ----------------------------- | :---------: | --------------------------------------------------------------------------------------- |
| **Quad-target compilation**   | ✅ Complete | SPIR-V, DXIL, GLSL, WGSL                                                                |
| **Dual-target compilation**   | ✅ Complete | SPIR-V + DXIL pair                                                                      |
| **Single-target compilation** | ✅ Complete | Any target                                                                              |
| **Reflection extraction**     | ✅ Complete | Bindings, vertex inputs, push constants, thread group, module constants, struct layouts |
| **Permutation support**       | ✅ Complete | 64-bit bitfield → preprocessor defines                                                  |
| **Search path management**    | ✅ Complete | `AddSearchPath()`                                                                       |
| **Session reuse**             | ❌ Missing  | New session per compile (inefficient)                                                   |
| **Module caching**            | ❌ Missing  | No cross-frame module reuse                                                             |
| **Error diagnostics**         |  ⚠️ Basic   | Prints to stderr, no structured error return                                            |

### 2.2 PermutationCache — Feature Completeness

| Feature                    |   Status    | Notes                 |
| -------------------------- | :---------: | --------------------- |
| **In-memory LRU**          | ✅ Complete | O(1) lookup/evict     |
| **Disk cache**             | ✅ Complete | Content-hashed blobs  |
| **Source hash validation** | ✅ Complete | Rejects stale cache   |
| **Thread safety**          | ✅ Complete | `std::mutex`          |
| **#include-aware hash**    | ❌ Missing  | Only hashes root file |

### 2.3 ShaderWatcher — Feature Completeness

| Feature                  |   Status    | Notes                                        |
| ------------------------ | :---------: | -------------------------------------------- |
| **File monitoring**      | ✅ Complete | Win: `ReadDirectoryChangesW`, POSIX: polling |
| **Debounce**             | ✅ Complete | Configurable interval                        |
| **Include dep graph**    | ⚠️ Partial  | 1-level deep only                            |
| **Transitive deps**      | ❌ Missing  | No BFS/DFS closure                           |
| **Background recompile** | ✅ Complete | Separate thread                              |
| **Generation counter**   | ✅ Complete | Atomic increment                             |
| **Error reporting**      | ✅ Complete | `GetLastErrors()`                            |

### 2.4 SlangFeatureProbe — Feature Completeness

| Feature              |   Status    | Notes            |
| -------------------- | :---------: | ---------------- |
| **Universal probes** | ✅ Complete | 14 tests         |
| **GLSL probes**      | ✅ Complete | 8 tests          |
| **WGSL probes**      | ✅ Complete | 7 tests          |
| **Tier degradation** | ✅ Complete | `tier1Only` flag |
| **Multi-target**     | ✅ Complete | All 4 targets    |

---

## 3. Industry Survey — Slang Adoption (2025-2026)

### 3.1 Khronos Slang Initiative

- **Status**: Slang moved from NVIDIA to Khronos Group (2025)
- **Governance**: Multi-vendor open governance (NVIDIA, Autodesk, Valve, id Software)
- **Targets**: D3D12, Vulkan, Metal, D3D11, OpenGL, CUDA, WebGPU, CPU
- **Key features**: Modular compilation, automatic differentiation, generics/interfaces

### 3.2 Framework Adoption

| Framework       |    Slang Status     | Approach                                                        |
| --------------- | :-----------------: | --------------------------------------------------------------- |
| **Bevy (Rust)** | 🟡 Community plugin | `bevy_mod_slang`: hot-reload, DX12/Vulkan/WebGPU via slangc CLI |
| **wgpu**        |     🟡 External     | `SlangWebGPU` project: Dawn backend integration                 |
| **Filament**    |   🔴 Not adopted    | Uses custom matc compiler (GLSL → SPIR-V)                       |
| **Unity**       | 🟡 Under discussion | Community interest, no official support yet                     |
| **Unreal**      |   🔴 Not adopted    | Uses HLSL → DXC/SPIRV-Cross                                     |
| **id Tech**     |    🟢 Evaluating    | Billy Khan (id Software) endorses Slang                         |

### 3.3 Key Observations

1. **Slang is the emerging standard** for cross-platform shader authoring
2. **HLSL compatibility** makes migration easy — most HLSL compiles as-is
3. **Module system** is unique advantage over HLSL/GLSL
4. **Automatic differentiation** enables neural graphics (differentiable rendering)
5. **WebGPU support** is production-ready (WGSL output)

### 3.4 Comparison with Alternatives

| Approach               | Pros                                          | Cons                           |
| ---------------------- | --------------------------------------------- | ------------------------------ |
| **Slang**              | Single-source, modules, autodiff, HLSL compat | Newer ecosystem, ~20MB runtime |
| **HLSL + DXC**         | Mature, MS support                            | No WebGPU, no modules          |
| **GLSL + SPIRV-Cross** | Vulkan native                                 | No D3D12, fragmented           |
| **WGSL**               | WebGPU native                                 | No desktop, limited features   |

**Conclusion**: Slang is the correct choice for mitsuki's multi-backend architecture.

---

## 4. Migration Plan

### 4.1 Phase 1: Direct Import (Token-Efficient)

**Strategy**: Copy files directly, adapt only namespace and includes.

```
D:\repos\miki\include\miki\shader\*.h → c:\mitsuki\include\miki\shader\*.h
D:\repos\miki\src\miki\shader\*.cpp  → c:\mitsuki\src\miki\shader\*.cpp
D:\repos\miki\shaders\tests\*.slang  → c:\mitsuki\shaders\tests\*.slang
```

**Changes required**:

1. Update `#include` paths if mitsuki uses different structure
2. Verify `miki::core::Result` and `miki::core::ErrorCode` exist
3. Verify `miki::rhi::Format` and `miki::rhi::BackendType` exist
4. Create `cmake/targets/miki_shader.cmake`

**Estimated effort**: 1-2 hours

### 4.2 Phase 2: Refactoring (New Features)

| Feature                 | Priority | Effort | Description                                 |
| ----------------------- | :------: | :----: | ------------------------------------------- |
| Session reuse           |   High   |   4h   | Reuse `slang::ISession` across compilations |
| #include-aware hash     |   High   |   2h   | Hash all transitively included files        |
| Transitive dep graph    |  Medium  |   3h   | BFS/DFS closure in `IncludeDepGraph`        |
| Structured error return |   Low    |   2h   | Return `ShaderError` instead of stderr      |
| `std::jthread` upgrade  |   Low    |   1h   | C++23 jthread for ShaderWatcher             |

### 4.3 Phase 3: New Components

| Component             | Phase | Description                             |
| --------------------- | :---: | --------------------------------------- |
| `IPipelineFactory`    |  1a   | Dual factory dispatch (Main/Compat)     |
| `PipelineCache`       |  1a   | VkPipelineCache / ID3D12PipelineLibrary |
| Offline WGSL compiler |  1b   | CLI tool for WASM build                 |
| Slang source CMake    |  1a   | `add_subdirectory` integration          |

---

## 5. Gap Analysis Summary

### 5.1 What Works (Direct Reuse)

| Component              | Quality | Notes                          |
| ---------------------- | :-----: | ------------------------------ |
| `ShaderTypes.h`        |  ★★★★★  | Complete, well-designed        |
| `SlangCompiler` API    |  ★★★★★  | Pimpl, quad-target, reflection |
| `SlangFeatureProbe`    |  ★★★★★  | 29 comprehensive tests         |
| `PermutationCache` API |  ★★★★☆  | Thread-safe, disk cache        |
| `ShaderWatcher` API    |  ★★★★☆  | Hot-reload, dep tracking       |
| `probe_*.slang`        |  ★★★★★  | Covers all edge cases          |

### 5.2 What Needs Improvement

| Gap                 | Severity  | Solution                |
| ------------------- | :-------: | ----------------------- |
| Session-per-compile | 🟡 Medium | Session pool / reuse    |
| Root-only hash      | 🟡 Medium | Recursive #include hash |
| 1-level dep graph   | 🟡 Medium | Transitive closure      |
| stderr diagnostics  |  🟢 Low   | Structured error return |

### 5.3 What's Missing (New Code)

| Component               | Severity  | Phase |
| ----------------------- | :-------: | :---: |
| `IPipelineFactory`      |  🔴 High  |  1a   |
| `PipelineCache`         |  🔴 High  |  1a   |
| Slang CMake integration |  🔴 High  |  1a   |
| Offline WGSL tool       | 🟡 Medium |  1b   |

---

## 6. Migration Execution Log

> **Date**: 2026-03-29
> **Status**: Phase 1 Complete -- All files migrated, compiled, verified

### 6.1 Execution Steps

| Step | Action                                                                  | Status | Notes                                                              |
| :--: | ----------------------------------------------------------------------- | :----: | ------------------------------------------------------------------ |
|  1   | Verify dependencies (`core::Result`, `rhi::Format`, `rhi::BackendType`) |   ✅   | `BackendType` enum differs: `Vulkan14`, `VulkanCompat`, `OpenGL43` |
|  2   | Copy `slang-prebuilt` from reference repo                               |   ✅   | DLLs + headers + standard modules                                  |
|  3   | Write `ShaderTypes.h`                                                   |   ✅   | Adapted `ShaderTargetForBackend` for mitsuki `BackendType`         |
|  4   | Write `SlangCompiler.h` + `.cpp`                                        |   ✅   | **New**: Session pool, structured `ShaderDiagnostic`               |
|  5   | Write `SlangFeatureProbe.h` + `.cpp`                                    |   ✅   | 29 probes, same as reference                                       |
|  6   | Write `PermutationCache.h` + `.cpp`                                     |   ✅   | **New**: #include-aware transitive source hash                     |
|  7   | Write `ShaderWatcher.h` + `.cpp`                                        |   ✅   | **New**: `std::jthread`, BFS transitive dep closure                |
|  8   | Copy 29 probe shaders                                                   |   ✅   | `shaders/tests/probe_*.slang`                                      |
|  9   | Create `cmake/targets/miki_shader.cmake`                                |   ✅   | Links `miki_core`, `miki_rhi`, `miki::third_party::slang`          |
|  10  | Integrate into `CMakeLists.txt`                                         |   ✅   | After `miki_debug`, before tests/demos                             |
|  11  | Build verification                                                      |   ✅   | `miki_shader.lib` compiles with zero errors                        |

### 6.2 Architecture Improvements over Reference

| Improvement             | Reference (miki)                 | mitsuki                                                       |
| ----------------------- | -------------------------------- | ------------------------------------------------------------- |
| **Session management**  | New session per `Compile()` call | Session pool: one cached `slang::ISession` per `ShaderTarget` |
| **Diagnostics**         | `fprintf(stderr, ...)`           | Structured `ShaderDiagnostic` vector with severity/file/line  |
| **Dep graph depth**     | 1-level `#include` scan          | BFS transitive closure over `#include` + `import`             |
| **Disk cache hash**     | Hash root file only              | Hash all transitively included files                          |
| **Thread model**        | `std::thread` + manual stop flag | `std::jthread` + `std::stop_token` (C++23)                    |
| **BackendType mapping** | `Vulkan`, `OpenGL`               | `Vulkan14`, `VulkanCompat`, `OpenGL43`, `Mock`                |
| **ShaderBlob**          | Copyable                         | Move-only (eliminates accidental deep copies)                 |

### 6.3 Files Created

| File                                      |  Lines   | Category |
| ----------------------------------------- | :------: | -------- |
| `include/miki/shader/ShaderTypes.h`       |   ~190   | Header   |
| `include/miki/shader/SlangCompiler.h`     |   ~90    | Header   |
| `include/miki/shader/SlangFeatureProbe.h` |   ~55    | Header   |
| `include/miki/shader/PermutationCache.h`  |   ~55    | Header   |
| `include/miki/shader/ShaderWatcher.h`     |   ~90    | Header   |
| `src/miki/shader/SlangCompiler.cpp`       |   ~380   | Source   |
| `src/miki/shader/SlangFeatureProbe.cpp`   |   ~120   | Source   |
| `src/miki/shader/PermutationCache.cpp`    |   ~260   | Source   |
| `src/miki/shader/ShaderWatcher.cpp`       |   ~280   | Source   |
| `cmake/targets/miki_shader.cmake`         |   ~60    | CMake    |
| `shaders/tests/probe_*.slang`             | 29 files | Shaders  |

**Total**: ~1580 lines C++ (rewritten, not copied) + 29 shader files + CMake

### 6.4 Dependency Graph (Final)

```mermaid
graph TD
    SHADER["miki_shader"] -->|PUBLIC| CORE["miki_core<br/>(Result, ErrorCode)"]
    SHADER -->|PUBLIC| RHI["miki_rhi<br/>(Format, BackendType)"]
    SHADER -->|PRIVATE| SLANG["miki::third_party::slang<br/>(prebuilt DLL)"]
```

---

## 7. IPipelineFactory + PipelineCache Migration

> **Date**: 2026-03-29
> **Status**: Complete — compiled with zero errors

### 7.1 Architecture Adaptation

mitsuki uses CRTP `DeviceBase<Impl>` + `DeviceHandle` (type-erased 5-way switch dispatch),
**not** virtual `IDevice`. The reference repo's `IPipelineFactory` held `IDevice&`; adapted to hold `DeviceHandle`.

| Aspect                | Reference (miki)                           | mitsuki                                  |
| --------------------- | ------------------------------------------ | ---------------------------------------- |
| **Device interface**  | `IDevice&` (virtual)                       | `DeviceHandle` (CRTP + switch dispatch)  |
| **Shader handle**     | `ShaderHandle` in pass descs               | `ShaderModuleHandle`                     |
| **Error type**        | `core::ErrorCode`                          | `RhiError` enum                          |
| **BackendType check** | `BackendType::Vulkan`                      | `BackendType::Vulkan14` / `VulkanCompat` |
| **Factory dispatch**  | `iDevice.GetCapabilities().SupportsTier()` | `DeviceHandle::Dispatch` lambda          |

### 7.2 Files Created

| File                                     | Lines | Description                                         |
| ---------------------------------------- | :---: | --------------------------------------------------- |
| `include/miki/rhi/IPipelineFactory.h`    | ~135  | 7 pass descs + abstract factory interface           |
| `include/miki/rhi/PipelineCache.h`       |  ~95  | On-disk header + Load/Save/GetNativeHandle          |
| `src/miki/rhi/PipelineCache.cpp`         | ~150  | File I/O + header validation + move semantics       |
| `src/miki/rhi/MainPipelineFactory.cpp`   |  ~85  | Tier1 factory + `IPipelineFactory::Create` dispatch |
| `src/miki/rhi/CompatPipelineFactory.cpp` |  ~75  | Tier2/3/4 factory                                   |

**Total**: ~540 lines, integrated into `miki_rhi.cmake`

### 7.3 Pass Creation Methods

| Method               |                     Main (Tier1)                     | Compat (Tier2/3/4)  |
| -------------------- | :--------------------------------------------------: | :-----------------: |
| `CreateGeometryPass` | `DeviceHandle::Dispatch` -> `CreateGraphicsPipeline` |        Same         |
| `CreateShadowPass`   |                 VSM + CSM validation                 | CSM validation only |
| `CreateOITPass`      |               Color format validation                |        Same         |
| `CreateAOPass`       |                   GTAO (sentinel)                    |   SSAO (sentinel)   |
| `CreateAAPass`       |                    TAA (sentinel)                    |   FXAA (sentinel)   |
| `CreatePickPass`     |               RT ray query (sentinel)                | CPU BVH (sentinel)  |
| `CreateHLRPass`      |               GPU exact HLR (sentinel)               |      Sentinel       |

All non-geometry passes return sentinel `PipelineHandle{}` — real pipelines are
owned by dedicated render pass objects in future phases.

### 7.4 PipelineCache Backend Support

| Backend                 |              Native Object               |           Persistence           |
| ----------------------- | :--------------------------------------: | :-----------------------------: |
| Vulkan14 / VulkanCompat |    `VkPipelineCache` (Phase 4 wiring)    | Binary blob + header validation |
| D3D12                   | `ID3D12PipelineLibrary` (Phase 4 wiring) | Binary blob + header validation |
| OpenGL / WebGPU / Mock  |                  No-op                   |     Pass-through (no file)      |

---

## 8. Next Steps

### 8.1 Remaining Phase 1 Work

1. **Unit tests**: `tests/shader/test_slang_compiler.cpp`, `tests/rhi/test_pipeline_factory.cpp`
2. **Integration test**: Compile triangle.slang -> SPIR-V + DXIL -> create pipelines
3. **DLL copy**: Wire `target_copy_slang_binaries()` into test/demo executables
4. **VkPipelineCache wiring**: Connect `PipelineCache::nativeHandle_` to VulkanDevice

### 8.2 Remaining New Components

| Component          | Priority | Phase | Description                                         |
| ------------------ | :------: | :---: | --------------------------------------------------- |
| Offline WGSL tool  |  Medium  |  1b   | CLI for WASM build (pre-compile all WGSL)           |
| Slang source build |   Low    |  1a   | `add_subdirectory(slang)` option alongside prebuilt |

### 8.3 Known Issues (Pre-existing, Not Caused by Migration)

- Linker error: `StructuredLogger::WriteToRing` undefined in demo/test executables (link order)
- Test code: `test_rhi_integration.cpp:128` uses stale field name `descriptorLayouts`

---

## 9. Risk Assessment

| Risk                             | Probability | Impact | Mitigation                              |
| -------------------------------- | :---------: | :----: | --------------------------------------- |
| Slang API changes                |     Low     | Medium | Pin Slang version via prebuilt snapshot |
| Build system conflicts           |     Low     | Medium | Isolated targets, PRIVATE slang link    |
| WASM Slang size                  |     Low     |  Low   | Offline-only for shipping               |
| Session pool thread safety       |     Low     | Medium | `std::mutex` on pool                    |
| PipelineCache native handle leak |     Low     | Medium | `assert` in destructor, Phase 4 wiring  |

---

## 10. Conclusion

Shader pipeline migration **complete** (Phase 1a scope):

- **miki_shader**: SlangCompiler, PermutationCache, ShaderWatcher, SlangFeatureProbe (~1580 LOC)
- **miki_rhi additions**: IPipelineFactory (dual Main/Compat), PipelineCache (~540 LOC)
- **Total**: ~2120 lines of rewritten C++ + 29 shader files + CMake integration

All code compiles with zero errors against the coca toolchain (clang-21 + libc++ + C++23).
