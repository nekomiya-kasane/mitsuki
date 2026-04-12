# 05 — Shader Compilation Pipeline Architecture

> **Scope**: Slang compiler integration, multi-target compilation (SPIR-V/DXIL/GLSL/WGSL/MSL), permutation cache,
> shader hot-reload, feature probe, push constant emulation, descriptor strategy integration,
> **Slang shader project architecture**, **timeline semaphore integration with pipeline creation**,
> **precompiled module strategy**, **neural shader support**.
> **Layer**: L2 (Shader Toolchain) — serves all 5 backends and all upper-layer rendering passes.
> **Depends on**: `00-infra` (ErrorCode, Result), `02-rhi-design` (Format, RhiTypes, IDevice, GpuCapabilityProfile), `03-sync` (timeline semaphores, SyncScheduler).
> **Consumed by**: Phase 1a (dual-target), Phase 1b (multi-target + hot-reload), Phase 2+ (all rendering).

---

## 0. Confirmed Architectural Decisions

These decisions were locked before writing this spec. They are **non-negotiable** within this document.

| #   | Decision                   | Detail                                                                                                                                                                     |
| --- | -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | **Slang consumption**      | Source-compiled default (`third_party/slang/`, CMake `add_subdirectory`). Hybrid option: CI fast path can use prebuilt DLLs via `MIKI_SLANG_PREBUILT=ON`.                  |
| 2   | **Shader IR cache**        | Per-module incremental (Slang session reuse) + Pipeline cache (`VkPipelineCache` / `ID3D12PipelineLibrary`) dual-layer.                                                    |
| 3   | **Descriptor strategy**    | Hybrid: bindless table layout locked at compile-time (fixed `set=3`), per-pass bindings (set 0-2) use reflection-driven layout generation.                                 |
| 4   | **Push constant rewrite**  | Dual-layer: Slang codegen rewrites `[vk::push_constant]` to UBO declarations for WGSL/GLSL targets; RHI backend validates and uploads UBO data at runtime.                 |
| 5   | **Hot-reload granularity** | Per-module: Slang module dependency graph tracks `import` edges; only affected modules recompile on file change.                                                           |
| 6   | **WASM/Emscripten**        | Offline-only for shipping (all WGSL blobs pre-compiled at build time). Dev environment allows runtime Slang-in-WASM as opt-in debug option (`MIKI_WASM_RUNTIME_SLANG=ON`). |

---

## 1. Design Goals

| Goal                                  | Metric                                                                                                                                                    |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Single-source shading**             | One `.slang` file compiles to SPIR-V, DXIL, GLSL, WGSL, MSL — zero per-backend shader forks. OpenGL consumes SPIR-V via `GL_ARB_gl_spirv` (not GLSL text) |
| **Compile-once per module**           | Slang parses each module once; codegen to N targets reuses the same IR. Incremental: only changed modules recompile                                       |
| **Sub-100ms hot-reload**              | File change → recompile affected module → pipeline swap in <100ms for typical shader (~500 LOC)                                                           |
| **Zero-overhead permutations**        | 64-bit bitfield key → preprocessor defines; LRU in-memory cache + content-hashed disk cache. No runtime branching                                         |
| **Reflection-driven per-pass layout** | `ShaderReflection` auto-generates `DescriptorSetLayout` for sets 0-2. Set 3 (bindless) is fixed at init time                                              |
| **Tier degradation safety**           | `SlangFeatureProbe` (29 tests) catches miscompiles and unsupported features at CI time, not at runtime                                                    |
| **Pimpl ABI stability**               | `SlangCompiler`, `PermutationCache`, `ShaderWatcher` use Pimpl — Slang headers never leak to public API                                                   |

---

## 2. Module Decomposition

### 2.1 Namespace & Header Layout

```
include/miki/shader/
    ShaderTypes.h          # ShaderTarget, ShaderStage, ShaderBlob, ShaderReflection,
                           # ShaderPermutationKey, ShaderCompileDesc, BindingInfo,
                           # VertexInputInfo, PermutationCacheConfig
    SlangCompiler.h        # SlangCompiler (Pimpl) — Compile, CompileDualTarget,
                           # CompileAllTargets, CompileActiveTargets, Reflect, AddSearchPath
    PermutationCache.h     # PermutationCache (Pimpl) — GetOrCompile, Insert, Clear
    ShaderWatcher.h        # ShaderWatcher (Pimpl) — Start, Stop, Poll, GetGeneration,
                           # GetLastErrors. ShaderChange, ShaderError, ShaderWatcherConfig
    SlangFeatureProbe.h    # SlangFeatureProbe (stateless) — RunAll, RunSingle.
                           # ProbeTestResult, ProbeReport

include/miki/rhi/
    IPipelineFactory.h     # IPipelineFactory — Create, CreateGeometryPass, CreateShadowPass, ...
                           # MainPipelineFactory (Tier1), CompatPipelineFactory (Tier2/3/4)
    PipelineCache.h        # PipelineCache — Load, Save, GetNativeHandle

src/miki/shader/
    SlangCompiler.cpp      # Slang API calls, session management, codegen, reflection extraction
    PermutationCache.cpp   # LRU list + unordered_map, disk cache read/write, source hash validation
    ShaderWatcher.cpp      # IncludeDepGraph, ReadDirectoryChangesW (Win) / polling (POSIX),
                           # debounce, background recompile thread
    SlangFeatureProbe.cpp  # Probe descriptor table, RunProbe per test x target
    CMakeLists.txt

src/miki/rhi/
    MainPipelineFactory.cpp
    CompatPipelineFactory.cpp
    PipelineCache.cpp
    PipelineFactoryImpl.cpp  # IPipelineFactory::Create dispatch

shaders/tests/
    probe_*.slang            # 29 probe test shaders (see S7)
```

### 2.2 Dependency Graph

```mermaid
graph TD
    subgraph "L2: Shader Toolchain"
        TYPES["ShaderTypes.h<br/>enums, structs, ShaderBlob"]
        COMPILER["SlangCompiler<br/>Pimpl, multi-target"]
        CACHE["PermutationCache<br/>LRU + disk, thread-safe"]
        WATCHER["ShaderWatcher<br/>file monitor, dep graph,<br/>background recompile"]
        PROBE["SlangFeatureProbe<br/>29 tests x N targets"]
    end

    subgraph "L2: RHI Pipeline"
        IPIPE["IPipelineFactory<br/>Main vs Compat dispatch"]
        PCACHE["PipelineCache<br/>VkPipelineCache /<br/>ID3D12PipelineLibrary"]
    end

    subgraph "L1: Foundation"
        RESULT["Result / ErrorCode"]
        FORMAT["Format.h"]
    end

    subgraph "L2: RHI Core"
        IDEVICE["IDevice"]
        RHITYPES["RhiTypes.h<br/>BackendType, Handle"]
        GCAP["GpuCapabilityProfile"]
    end

    subgraph "External"
        SLANG["Slang API<br/>(slang.h, slang-com-ptr.h)"]
    end

    RESULT --> TYPES
    FORMAT --> TYPES
    RHITYPES --> TYPES
    TYPES --> COMPILER
    SLANG --> COMPILER
    TYPES --> CACHE
    COMPILER --> CACHE
    TYPES --> WATCHER
    COMPILER --> WATCHER
    TYPES --> PROBE
    COMPILER --> PROBE
    IDEVICE --> IPIPE
    GCAP --> IPIPE
    RHITYPES --> IPIPE
    COMPILER --> IPIPE
    IDEVICE --> PCACHE
    RHITYPES --> PCACHE
```

---

## 3. ShaderTypes — Core Data Types

### 3.1 ShaderTarget

```cpp
enum class ShaderTarget : uint8_t {
    SPIRV,   // Vulkan (Tier1/Tier2), OpenGL (via GL_ARB_gl_spirv)
    DXIL,    // D3D12
    GLSL,    // Reserved — not used at runtime (GL consumes SPIR-V)
    WGSL,    // WebGPU (Dawn)
    MSL,     // Metal (Apple platforms — macOS, iOS, visionOS)
};
```

**Key design notes**:

- OpenGL backend consumes **SPIR-V** via `GL_ARB_gl_spirv`, not GLSL text. This eliminates an entire class of Slang GLSL codegen bugs and simplifies the pipeline. The `GLSL` enum value exists for offline tooling / debug dump only.
- **Metal** backend consumes **MSL** (Metal Shading Language) generated by Slang. Slang has supported Metal target since 2025, targeting Metal 3.x feature set. Metal is a future backend (Phase 15a+) for macOS/iOS/visionOS support.

### 3.2 ShaderTargetForBackend — Canonical Mapping

```cpp
[[nodiscard]] constexpr auto ShaderTargetForBackend(rhi::BackendType iBackend) noexcept -> ShaderTarget {
    switch (iBackend) {
        case rhi::BackendType::D3D12:  return ShaderTarget::DXIL;
        case rhi::BackendType::OpenGL: return ShaderTarget::SPIRV;  // GL_ARB_gl_spirv
        case rhi::BackendType::WebGPU: return ShaderTarget::WGSL;
        case rhi::BackendType::Metal:  return ShaderTarget::MSL;    // Metal 3.x (Phase 15a+)
        default:                       return ShaderTarget::SPIRV;   // Vulkan, Mock
    }
};
```

All render passes call `ShaderTargetForBackend()` — **no duplicate mapping logic allowed**.
`static_assert` in `SlangCompiler.cpp` validates `BackendType` enum values at compile time.

### 3.3 ShaderStage

```cpp
enum class ShaderStage : uint8_t {
    Vertex, Fragment, Compute, Mesh,
    Amplification,                    // D3D12 naming
    Task = Amplification,             // Vulkan/Slang naming (alias)
    RayGen, ClosestHit, Miss, AnyHit, Intersection,
};
```

### 3.4 ShaderBlob

Move-only compiled bytecode container. `data` holds raw SPIR-V / DXIL / WGSL bytes.

```cpp
struct ShaderBlob {
    std::vector<uint8_t> data;
    ShaderTarget target = ShaderTarget::SPIRV;
    ShaderStage  stage  = ShaderStage::Vertex;
    std::string  entryPoint;
    // Move-only: shader blobs can be large (100KB+)
};
```

### 3.5 ShaderPermutationKey

64-bit bitfield → up to 64 boolean permutation axes. Combined with `ShaderCompileDesc::defines`
for non-boolean (string-valued) permutations. The cache key includes both.

```cpp
struct ShaderPermutationKey {
    uint64_t bits = 0;
    constexpr void SetBit(uint32_t iBit, bool iValue) noexcept;
    [[nodiscard]] constexpr auto GetBit(uint32_t iBit) const noexcept -> bool;
    constexpr auto operator==(ShaderPermutationKey const&) const noexcept -> bool = default;
};
```

### 3.6 ShaderCompileDesc

```cpp
struct ShaderCompileDesc {
    std::filesystem::path sourcePath;
    std::string           entryPoint;
    ShaderStage           stage       = ShaderStage::Vertex;
    ShaderTarget          target      = ShaderTarget::SPIRV;
    ShaderPermutationKey  permutation;
    std::span<std::pair<std::string, std::string>> defines;
};
```

### 3.7 ShaderReflection

Full reflection data extracted after compilation:

```cpp
struct ShaderReflection {
    struct ModuleConstant {
        std::string name;
        bool hasIntValue = false;
        int64_t intValue = 0;
    };
    struct StructField {
        std::string name;
        uint32_t offsetBytes = 0;
        uint32_t sizeBytes   = 0;
    };
    struct StructLayout {
        std::string              name;
        uint32_t                 sizeBytes = 0;
        uint32_t                 alignment = 0;
        std::vector<StructField> fields;
    };

    std::vector<EntryPointInfo>   entryPoints;
    std::vector<BindingInfo>      bindings;         // set, binding, type, count, name, user attribs
    uint32_t                      pushConstantSize = 0;
    std::vector<VertexInputInfo>  vertexInputs;     // location, format, offset, name
    uint32_t                      threadGroupSize[3] = {0, 0, 0};
    std::vector<ModuleConstant>   moduleConstants;  // static const int scalars in module scope
    std::vector<StructLayout>     structLayouts;    // field-level offset/size for C++ <-> GPU validation
};
```

Reflection captures:

- **Bindings**: set, binding, type, count, name, user attributes (`[vk::binding]`, etc.)
- **Vertex inputs**: location, format, offset, name (auto-mapped from Slang scalar type)
- **Push constant size**: global constant buffer size from Slang layout
- **Thread group size**: `[numthreads(X,Y,Z)]` for compute/mesh
- **Module constants**: `static const` integer scalars in module scope (AST walk)
- **Struct layouts**: field-level offset/size for C++ <-> GPU struct validation (`LayoutRules::DefaultStructuredBuffer`)

### 3.8 BindingInfo & VertexInputInfo

```cpp
struct BindingInfo {
    uint32_t set = 0, binding = 0;
    BindingType type = BindingType::UniformBuffer;
    uint32_t count = 1;
    std::string name;
    struct UserAttrib { std::string name; std::vector<std::string> args; };
    std::vector<UserAttrib> userAttribs;
};

struct VertexInputInfo {
    uint32_t location = 0;
    rhi::Format format = rhi::Format::Undefined;
    uint32_t offset = 0;
    std::string name;
};
```

---

## 4. SlangCompiler — Core Compilation Engine

### 4.1 Public API

```cpp
class SlangCompiler {
public:
    [[nodiscard]] static auto Create() -> core::Result<SlangCompiler>;
    [[nodiscard]] auto Compile(ShaderCompileDesc const& iDesc) -> core::Result<ShaderBlob>;
    [[nodiscard]] auto CompileDualTarget(
        std::filesystem::path const& iSourcePath,
        std::string const& iEntryPoint, ShaderStage iStage
    ) -> core::Result<std::pair<ShaderBlob, ShaderBlob>>;

    static constexpr size_t kTargetCount = 5;  // SPIRV, DXIL, GLSL, WGSL, MSL
    [[nodiscard]] auto CompileAllTargets(
        std::filesystem::path const& iSourcePath,
        std::string const& iEntryPoint, ShaderStage iStage
    ) -> core::Result<std::array<ShaderBlob, kTargetCount>>;

    /// Compile for active backends only (typically 2-3 at runtime).
    [[nodiscard]] auto CompileActiveTargets(
        std::filesystem::path const& iSourcePath,
        std::string const& iEntryPoint, ShaderStage iStage,
        std::span<const ShaderTarget> iTargets
    ) -> core::Result<std::vector<ShaderBlob>>;

    [[nodiscard]] auto Reflect(ShaderCompileDesc const& iDesc) -> core::Result<ShaderReflection>;
    auto AddSearchPath(std::filesystem::path const& iPath) -> void;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

### 4.2 Internal Architecture (Impl)

```mermaid
graph TD
    subgraph "SlangCompiler::Impl"
        GS["slang::IGlobalSession<br/>(one per SlangCompiler)"]
        SP["searchPaths<br/>vector of string"]

        GS --> SESS["CreateSessionForTarget()<br/>slang::ISession per target"]
        SP --> SESS
        SESS --> MOD["loadModuleFromSourceString()<br/>slang::IModule"]
        MOD --> EP["findAndCheckEntryPoint()<br/>IEntryPoint"]
        MOD --> LINK["createCompositeComponentType()<br/>link()"]
        EP --> LINK
        LINK --> CODE["getTargetCode()<br/>IBlob (SPIR-V/DXIL/WGSL)"]
        LINK --> REFL["getLayout()<br/>ProgramLayout (reflection)"]
    end
```

#### Session Management

- One `slang::IGlobalSession` per `SlangCompiler` instance (expensive to create, reused across compilations)
- Pooled `slang::ISession` per {target, thread} pair, created lazily with target-specific profile:

| ShaderTarget | Slang profile | Slang format  | Notes                                                                            |
| ------------ | ------------- | ------------- | -------------------------------------------------------------------------------- |
| SPIRV        | `spirv_1_6`   | `SLANG_SPIRV` | **Vulkan 1.4 mandates SPIR-V 1.6**. `discard` emits `OpDemoteToHelperInvocation` |
| DXIL         | `sm_6_6`      | `SLANG_DXIL`  | D3D12 FL 12.2                                                                    |
| GLSL         | `glsl_430`    | `SLANG_GLSL`  | Offline/debug only                                                               |
| WGSL         | `wgsl`        | `SLANG_WGSL`  | Dawn / Emscripten                                                                |
| MSL          | `metal_3_1`   | `SLANG_METAL` | Apple platforms (Phase 15a+), Metal 3.1 for mesh shaders                         |

#### Threading Model (Critical)

**Slang sessions are NOT thread-safe.** Per the [Slang multithreading documentation](http://shader-slang.org/slang/user-guide/compiling#multithreading), only `slang::createGlobalSession()` and `slang_getEmbeddedCoreModule()` are reentrant. All other `ISession` / `IModule` / `IComponentType` methods require exclusive access.

miki's threading strategy:

| Approach                   | Detail                                                                                                                                                                                       |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Session pool**           | One `slang::ISession` per {target, thread} pair. `SlangCompiler::Impl` maintains a pool of `(ShaderTarget, threadIndex) → ISession*`. No mutex needed — each session is owned by one thread. |
| **Global session**         | Single `slang::IGlobalSession` shared (creation is thread-safe). Session creation from global session requires a mutex — but sessions are pooled, so creation is rare.                       |
| **Background compilation** | `ShaderWatcher` recompile thread uses its own session instance. `AsyncTaskManager` PSO compilation tasks use the calling thread's session from the pool.                                     |
| **Module sharing**         | `IModule` objects from `loadModule()` are NOT shared across threads. Each session loads its own copy. Module IR is deduplicated at the `IGlobalSession` level internally.                    |

#### Per-Module Incremental Compilation (Decision #2)

`CompileAllTargets()` iterates over N target sessions sequentially. Each session loads and parses the module independently, but `IGlobalSession` deduplicates module IR internally, and **precompiled `.slang-module` blobs** (§15.8) eliminate redundant parsing entirely. With precompiled modules, multi-target compilation cost is dominated by codegen/linking only (~75-80% parse overhead eliminated).

Future upgrade path (Phase 3a): Slang `ISession::loadModule()` caches parsed modules within a session. By reusing sessions across frames, only re-parsing changed modules while keeping unchanged modules hot.

#### Permutation Handling

Permutation bits are expanded to preprocessor defines:

- Bit N set → `#define MIKI_PERMUTATION_BIT_N 1`
- `ShaderCompileDesc::defines` provides additional string-valued defines
- Both passed to `slang::SessionDesc::preprocessorMacros`

#### Reflection Extraction Pipeline

`Reflect()` performs full AST + layout extraction in 8 steps:

1. Create session, load module, link program (same as compile path)
2. Iterate `layout->getParameterCount()` → map `TypeReflection::Kind` to `BindingType`
3. Extract user attributes (`[vk::binding(set, binding)]`) via `UserAttribute` API
4. For vertex stage: iterate entry point parameters → map Slang scalar type to `rhi::Format`
5. `layout->getGlobalConstantBufferSize()` → `pushConstantSize`
6. `entryPoint->getComputeThreadGroupSize()` → `threadGroupSize`
7. Recursive AST walk (`slang::DeclReflection`) → collect `static const` integer scalars
8. For each struct name: `layout->findTypeByName()` → field offset/size via `LayoutRules::DefaultStructuredBuffer`

Slang type → `rhi::Format` mapping for vertex inputs:

| Slang scalar | Columns | Format         |
| ------------ | ------- | -------------- |
| `float32`    | 1       | `R32_FLOAT`    |
| `float32`    | 2       | `RG32_FLOAT`   |
| `float32`    | 3/4     | `RGBA32_FLOAT` |
| `int32`      | 1       | `R32_SINT`     |
| `uint32`     | 1       | `R32_UINT`     |
| `float16`    | 2       | `RG16_FLOAT`   |
| `float16`    | 4       | `RGBA16_FLOAT` |

### 4.3 Push Constant Emulation (Decision #4 — Dual Layer)

#### Layer 1: Slang Codegen Rewrite

When targeting WGSL or GLSL:

- Slang automatically rewrites `[vk::push_constant]` blocks to uniform buffer declarations
- WGSL: `@group(0) @binding(0) var<uniform> pushConstants: PushConstantsBlock;`
- GLSL: `layout(std140, binding = 0) uniform PushConstants { ... };`

#### Layer 2: RHI Runtime Protection

- `ICommandBuffer::PushConstants()` on WebGPU/OpenGL backends writes to a shadow UBO buffer
- WebGPU: `wgpu::Queue::WriteBuffer` to bind group 0, slot 0 (256B max)
- OpenGL: `glBufferSubData` to UBO binding point 0 (128B max)
- Debug builds: `assert(size <= maxPushConstantSize)` where max = 256B (Vulkan/D3D12) or 128B (GL)

#### Reserved Binding Convention

| Backend        | Reserved Slot           | Size | Mechanism                     |
| -------------- | :---------------------- | :--- | :---------------------------- |
| WebGPU         | `@group(0) @binding(0)` | 256B | `var<uniform>` in WGSL        |
| OpenGL         | UBO binding 0           | 128B | `layout(std140, binding = 0)` |
| Vulkan / D3D12 | None                    | N/A  | Native push/root constants    |

User-declared descriptors in set 0 start from `binding=1` on WebGPU/OpenGL.
`CreatePipelineLayout` asserts no user binding collides with reserved slot in debug builds.

---

## 5. PermutationCache — Dual-Layer Caching

### 5.1 Architecture

```mermaid
graph LR
    REQ["ShaderCompileDesc"] --> L1["L1: In-Memory LRU<br/>unordered_map + list<br/>O(1) lookup, O(1) evict"]
    L1 -->|miss| L2["L2: Disk Cache<br/>content-hashed blobs<br/>.spv / .dxil / .wgsl"]
    L2 -->|miss| COMPILE["SlangCompiler::Compile()"]
    COMPILE -->|blob| L2
    L2 -->|blob| L1
    L1 -->|hit| BLOB["ShaderBlob const*"]
```

### 5.2 Cache Key

```cpp
struct CacheKey {
    std::string          sourcePath;
    std::string          entryPoint;
    ShaderTarget         target;
    ShaderStage          stage;
    ShaderPermutationKey permutation;
};
// Hash: FNV-1a over concatenated fields. Equality: exact match on all fields.
```

### 5.3 Disk Cache

- Path: `{cacheDir}/{hash}.{spv|dxil|glsl|wgsl|msl}`
- Validation: `.hash` sidecar file stores source content hash (`uint64_t`)
- On load: compare stored hash with current source hash → reject if stale
- Thread safety: `std::mutex` around LRU operations; disk I/O outside lock

### 5.4 Public API

```cpp
class PermutationCache {
public:
    explicit PermutationCache(PermutationCacheConfig iConfig = {});
    [[nodiscard]] auto GetOrCompile(ShaderCompileDesc const& iDesc, SlangCompiler& iCompiler)
        -> core::Result<ShaderBlob const*>;
    auto Insert(ShaderCompileDesc const& iDesc, ShaderBlob iBlob) -> void;
    auto Clear() -> void;
    [[nodiscard]] auto Size() const -> uint32_t;
private:
    struct Impl;  // LRU list + map + mutex + disk helpers
    std::unique_ptr<Impl> impl_;
};
```

### 5.5 Pipeline Cache (L3 — Driver Layer)

Separate from `PermutationCache`, `PipelineCache` operates at the driver level:

| Backend                | Native Object           | Persistence                                                                                  |
| ---------------------- | ----------------------- | -------------------------------------------------------------------------------------------- |
| Vulkan                 | `VkPipelineCache`       | Binary blob to disk, validated by `PipelineCacheHeader` (magic + driver version + device ID) |
| D3D12                  | `ID3D12PipelineLibrary` | Serialized pipeline library                                                                  |
| OpenGL / WebGPU / Mock | No-op                   | Pass-through                                                                                 |

```cpp
class PipelineCache {
public:
    [[nodiscard]] static auto Load(IDevice& iDevice, std::filesystem::path const& iPath)
        -> core::Result<PipelineCache>;
    [[nodiscard]] auto Save(std::filesystem::path const& iPath) const
        -> core::Result<void>;
    [[nodiscard]] auto GetNativeHandle() const noexcept -> void*;
};
```

On-disk header: `PipelineCacheHeader` { magic `0x4D4B5043`, version, driverVersion, deviceId, dataSize }.
On header mismatch (driver update, different GPU), the blob is discarded silently —
graceful rebuild, no error.

### 5.6 Three-Layer Cache Summary

```
Request → L1 (PermutationCache in-memory LRU)
  miss → L2 (PermutationCache disk, content-hash validated)
    miss → SlangCompiler::Compile() → store L2, L1
      ↓ ShaderBlob
IDevice::CreateGraphicsPipeline(blob, PipelineCache::GetNativeHandle())
  → L3 (VkPipelineCache / ID3D12PipelineLibrary — driver-optimized)
```

---

## 6. ShaderWatcher — Hot-Reload System

### 6.1 Architecture

```mermaid
graph TD
    subgraph "Background Thread"
        FS["File System Monitor<br/>Win: ReadDirectoryChangesW<br/>POSIX: polling fallback"]
        DEBOUNCE["Debounce<br/>(configurable, default 100ms)"]
        DEP["IncludeDepGraph<br/>#include / import tracking"]
        RECOMP["Recompile affected modules<br/>via SlangCompiler"]
    end

    subgraph "Main Thread"
        POLL["ShaderWatcher::Poll()<br/>vector of ShaderChange"]
        GEN["GetGeneration()<br/>monotonic counter"]
        ERR["GetLastErrors()<br/>span of ShaderError"]
    end

    FS -->|".slang changed"| DEBOUNCE
    DEBOUNCE --> DEP
    DEP -->|"affected files"| RECOMP
    RECOMP -->|"ShaderChange + generation++<br/>(mutex-protected queue)"| POLL
    RECOMP -->|"errors"| ERR
```

### 6.2 IncludeDepGraph

Internal helper class that scans `.slang` files for:

- `#include "path"` → direct file path dependency
- `import module.name;` → resolved to `{parent_dir}/{module/name}.slang`

`GetAffected(changedFile)` returns all files that directly or transitively depend on the
changed file. Current implementation scans 1-level deep (direct deps); transitive closure
is a Phase 3a upgrade.

### 6.3 Per-Module Recompile Flow (Decision #5)

When a file changes:

1. `IncludeDepGraph::ScanFile()` re-parses changed file's dependencies
2. `GetAffected()` collects all `.slang` files that import/include the changed file
3. Each affected file is recompiled for all configured targets
4. `generation` counter increments atomically per successful recompile
5. Rendering code compares `GetGeneration()` with cached generation → recreate pipeline if stale

### 6.4 Platform-Specific File Watching

| Platform | Mechanism                                                   | Latency           |
| -------- | ----------------------------------------------------------- | ----------------- |
| Windows  | `ReadDirectoryChangesW` + overlapped I/O                    | <50ms + debounce  |
| POSIX    | `std::filesystem::last_write_time` polling (200ms interval) | <400ms + debounce |
| Future   | `inotify` (Linux), `FSEvents` (macOS) — Phase 15a           | <20ms             |

### 6.5 Public API

```cpp
struct ShaderWatcherConfig {
    uint32_t debounceMs = 100;
    std::vector<ShaderTarget> targets;  // targets to recompile to
};

struct ShaderChange {
    std::filesystem::path path;
    ShaderTarget target = ShaderTarget::SPIRV;
    ShaderBlob blob;       // move-only
    uint64_t generation = 0;
};

struct ShaderError {
    std::filesystem::path path;
    std::string message;
    uint32_t line = 0;
    uint32_t column = 0;
};

class ShaderWatcher {
public:
    [[nodiscard]] static auto Create(SlangCompiler& iCompiler, ShaderWatcherConfig iConfig = {})
        -> core::Result<ShaderWatcher>;
    [[nodiscard]] auto Start(std::filesystem::path const& iWatchDir) -> core::Result<void>;
    auto Stop() -> void;
    [[nodiscard]] auto Poll() -> std::vector<ShaderChange>;
    [[nodiscard]] auto GetGeneration() const noexcept -> uint64_t;
    [[nodiscard]] auto GetLastErrors() const -> std::span<const ShaderError>;
    [[nodiscard]] auto IsRunning() const noexcept -> bool;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

### 6.6 Pipeline Swap Protocol

Rendering code integrates hot-reload via generation counter comparison:

```cpp
// Per render pass (e.g., ForwardPass, DeferredResolve)
if (watcher.GetGeneration() != cachedGeneration_) {
    auto changes = watcher.Poll();
    for (auto& change : changes) {
        if (change.path == myShaderPath_ && change.target == myTarget_) {
            // Recreate pipeline with new blob
            auto pipeline = device.CreateGraphicsPipeline(pipelineDesc, change.blob);
            if (pipeline) {
                std::swap(pipeline_, *pipeline);
                // Old pipeline deferred-destroyed via FrameManager
            }
        }
    }
    cachedGeneration_ = watcher.GetGeneration();
}
```

Error overlay (ImGui) displays `GetLastErrors()` in debug builds.

---

## 7. SlangFeatureProbe — Compilation Regression Suite

### 7.1 Purpose

Exhaustive shader feature regression suite that validates correct Slang codegen across
all targets. **Compilation-only — no GPU required**. Run on every CI build.

### 7.2 Probe Test Catalog (29 tests)

#### Universal Probes (all targets)

| #   | Test Name            | Feature                                | Tier1 only? |
| --- | -------------------- | -------------------------------------- | :---------: |
| 1   | `struct_array`       | Nested structs with padding            |     No      |
| 2   | `atomics_32`         | 32-bit atomic operations               |     No      |
| 3   | `atomics_64`         | 64-bit atomics (`InterlockedAdd64`)    |     Yes     |
| 4   | `subgroup_ballot`    | `WaveBallot` / `subgroupBallot`        |     No      |
| 5   | `subgroup_shuffle`   | `WaveReadLaneAt` / `subgroupShuffle`   |     No      |
| 6   | `subgroup_clustered` | `WaveActiveSum` clustered reduce       |     No      |
| 7   | `push_constants`     | `[vk::push_constant]` block            |     No      |
| 8   | `texture_array`      | Texture2DArray + bindless              |     No      |
| 9   | `compute_shared`     | `groupshared` / shared memory layout   |     No      |
| 10  | `barrier_semantics`  | Memory vs execution barriers           |     No      |
| 11  | `binding_map`        | `[vk::binding]` → layout mapping       |     No      |
| 12  | `half_precision`     | `float16_t` / `half` support detection |     No      |
| 13  | `image_atomics`      | Image load/store + atomics             |     No      |
| 14  | `mesh_shader`        | `[shader("mesh")]` entry point         |     Yes     |

#### GLSL-Specific Probes (Phase 1b)

| #   | Test Name                | Feature                               |
| --- | ------------------------ | ------------------------------------- |
| 15  | `glsl_ssbo_mapping`      | BDA → SSBO array index mapping        |
| 16  | `glsl_binding_layout`    | `layout(binding=N)` vs descriptor set |
| 17  | `glsl_texture_units`     | Texture unit limits                   |
| 18  | `glsl_workgroup`         | Workgroup size constraints            |
| 19  | `glsl_shared_memory`     | Shared memory layout                  |
| 20  | `glsl_image_load_store`  | Image load/store ops                  |
| 21  | `glsl_atomic_32`         | 32-bit atomics in GLSL                |
| 22  | `glsl_push_constant_ubo` | Push constant → UBO rewrite           |

#### WGSL-Specific Probes (Phase 1b)

| #   | Test Name                | Feature                                       |
| --- | ------------------------ | --------------------------------------------- |
| 23  | `wgsl_storage_alignment` | Storage buffer alignment rules                |
| 24  | `wgsl_workgroup_limits`  | Workgroup size limits                         |
| 25  | `wgsl_no_64bit_atomics`  | 64-bit atomic → error (not silent miscompile) |
| 26  | `wgsl_group_binding`     | `@group(N) @binding(M)` mapping               |
| 27  | `wgsl_texture_sample`    | Texture sampling ops                          |
| 28  | `wgsl_array_stride`      | Array stride alignment                        |
| 29  | `wgsl_push_constant_ubo` | Push constant → UBO `@group(0)@binding(0)`    |

### 7.3 Tier Degradation Validation

**Critical**: compiling a Tier1-only shader (e.g., mesh shader, 64-bit atomics) for
Tier3/4 target **must produce a compilation error**, never a silent miscompile.

Probes marked `tier1Only = true` are expected to **fail** on GLSL/WGSL targets.
`ProbeTestResult::passed = false` on these targets is the correct behavior —
`SlangFeatureProbe` reports them as "correctly rejected".

### 7.4 Public API

```cpp
struct ProbeTestResult {
    std::string  name;
    ShaderTarget target = ShaderTarget::SPIRV;
    bool passed   = false;
    bool skipped  = false;
    std::string diagnostic;
};

struct ProbeReport {
    std::vector<ProbeTestResult> results;
    uint32_t totalPassed  = 0;
    uint32_t totalFailed  = 0;
    uint32_t totalSkipped = 0;
};

class SlangFeatureProbe {
public:
    [[nodiscard]] static auto RunAll(
        SlangCompiler& iCompiler,
        std::span<const ShaderTarget> iTargets,
        std::filesystem::path const& iShaderDir
    ) -> core::Result<ProbeReport>;

    [[nodiscard]] static auto RunSingle(
        SlangCompiler& iCompiler,
        std::string_view iTestName, ShaderTarget iTarget,
        std::filesystem::path const& iShaderDir
    ) -> core::Result<ProbeTestResult>;
};
```

---

## 8. IPipelineFactory — Dual Pipeline Dispatch

### 8.1 Factory Pattern

```mermaid
graph LR
    INIT["IDevice creation"] --> CAP["GpuCapabilityProfile"]
    CAP -->|"Tier1_Full"| MAIN["MainPipelineFactory<br/>Task/Mesh, RT, VSM"]
    CAP -->|"Tier2/3/4"| COMPAT["CompatPipelineFactory<br/>Vertex+MDI, CSM"]
    MAIN --> PASS["CreateGeometryPass()<br/>CreateShadowPass()<br/>CreateAOPass()<br/>CreateAAPass()<br/>..."]
    COMPAT --> PASS
```

`IPipelineFactory::Create(IDevice&)` inspects `GpuCapabilityProfile::GetTier()`:

- `Tier1_Full` → `MainPipelineFactory`
- All others → `CompatPipelineFactory`

### 8.2 Pass Creation Methods

| Method               | Main (Tier1)             | Compat (Tier2/3/4)       |
| -------------------- | ------------------------ | ------------------------ |
| `CreateGeometryPass` | Mesh shader pipeline     | Vertex shader pipeline   |
| `CreateShadowPass`   | VSM (virtual shadow map) | CSM (cascade shadow map) |
| `CreateOITPass`      | Linked-list OIT          | Weighted OIT             |
| `CreateAOPass`       | GTAO                     | SSAO                     |
| `CreateAAPass`       | TAA + FSR                | FXAA / MSAA              |
| `CreatePickPass`     | RT ray query             | CPU BVH                  |
| `CreateHLRPass`      | GPU exact HLR            | N/A (Phase 7b)           |

Each factory method accepts a pass-specific descriptor (`ShadowPassDesc`, `AOPassDesc`, etc.)
and returns a `PipelineHandle`. Rendering code never contains `if (compat)` branches —
the factory dispatches to the correct implementation.

### 8.3 Phase 1a Scope

In Phase 1a, only `CreateGeometryPass()` is implemented (triangle rendering).
Other methods return `ErrorCode::NotImplemented` until their respective phases.

### 8.4 Future: `VK_EXT_shader_object` (Tier1 Vulkan, Phase 5+)

`VK_EXT_shader_object` (widely supported since 2024, Vulkan 1.4 optional feature) eliminates PSO objects entirely. Instead of baking all pipeline state into a monolithic `VkPipeline`, shader objects are bound independently with fully dynamic state. This eliminates the PSO combinatorial explosion problem.

| Aspect       | Pipeline (current)      | Shader Object (future)               |
| ------------ | ----------------------- | ------------------------------------ |
| Startup cost | 88+ PSOs × permutations | 0 (shaders compiled independently)   |
| Hot-reload   | Recreate entire PSO     | Replace single shader stage          |
| State change | Rebind full pipeline    | Set only changed dynamic state       |
| Permutations | Separate PSO per combo  | Same shader, different dynamic state |

**Migration path**: `IPipelineFactory` abstracts this — when Tier1 Vulkan enables shader objects, `MainPipelineFactory` can switch to `vkCreateShadersEXT` internally while the pass-level API remains unchanged. `IPipelineFactory::CreateGeometryPass()` returns a `PipelineHandle` that internally wraps either `VkPipeline` or a tuple of `VkShaderEXT` objects.

**Decision**: Shader objects are NOT adopted in Phase 1a-3b (PSO model is proven and all backends support it). Evaluate for Phase 5+ when Tier1-only features are being optimized. The abstraction is designed to allow this migration without breaking any pass code.

---

## 9. Descriptor Strategy Integration (Decision #3)

### 9.1 Hybrid Binding Model

```
Set 0: Per-frame    (camera UBO, global SSBOs)      — reflection-driven
Set 1: Per-pass     (shadow maps, AO buffer, etc.)   — reflection-driven
Set 2: Per-material (textures, material UBOs)         — reflection-driven
Set 3: Bindless     (all textures/buffers by index)   — compile-time fixed
```

### 9.2 Compile-Time Fixed Bindless Layout (Set 3)

```cpp
// Defined once at engine init, never changes
static constexpr DescriptorLayoutDesc kBindlessLayout = {
    .bindings = {
        {0, DescriptorType::SampledImage, kMaxBindlessTextures, ShaderStageAll},
        {1, DescriptorType::StorageBuffer, kMaxBindlessBuffers, ShaderStageAll},
        {2, DescriptorType::Sampler, kMaxBindlessSamplers, ShaderStageAll},
    }
};
```

This layout is **not** generated from reflection — it is a fixed contract between
the engine and all shaders. `BindlessTable` (Phase 4, Resource layer) manages
allocation of indices within this global set.

#### Backend-Specific Descriptor Model

The shader-side declaration is identical across backends (Slang `[[vk::binding(N, 3)]]`), but the RHI implementation differs per `rendering-pipeline-architecture.md` §Tier Feature Matrix:

| Backend            | Descriptor Model                 | Set 3 Implementation                                                                                      |
| ------------------ | -------------------------------- | --------------------------------------------------------------------------------------------------------- |
| **Vulkan (Tier1)** | `VK_EXT_descriptor_buffer`       | GPU-visible buffer holding descriptor data directly. Zero descriptor set allocation overhead.             |
| **D3D12 (Tier1)**  | Descriptor heap (CBV/SRV/UAV)    | Shader-visible heap, bindless via unbounded `DescriptorTable`.                                            |
| **Vulkan (Tier2)** | Traditional `VkDescriptorSet`    | Auto-growing `VkDescriptorPool`, `UPDATE_AFTER_BIND` flag on bindless bindings.                           |
| **WebGPU (Tier3)** | `GPUBindGroup`                   | Chunked binding: split bindless array into groups of 256 (Dawn limit). Dynamic rebind on resource change. |
| **OpenGL (Tier4)** | `GL_ARB_bindless_texture` / SSBO | Bindless texture handles stored in SSBO. Fallback: texture array with fixed slot count.                   |

Shader code is **backend-agnostic** — the `sampleBindlessTexture()` helper in `core/bindless.slang` uses `NonUniformResourceIndex()` which Slang maps to the correct construct per target.

### 9.3 Reflection-Driven Per-Pass Layout (Sets 0-2)

For each render pass:

1. Compile shader → `Reflect()` → `ShaderReflection::bindings`
2. Filter bindings by set number (0, 1, or 2)
3. Auto-generate `DescriptorSetLayoutDesc` from reflected bindings
4. Create `DescriptorSetLayout` via `IDevice::CreateDescriptorLayout()`

This eliminates manual layout maintenance as shaders evolve.
**Validation**: in debug builds, reflection output is compared against previous
compilation's layout — layout-breaking changes trigger a warning.

---

## 10. Slang Build Integration (Decision #1)

### 10.1 Source-Compiled (Default)

```cmake
# third_party/slang/CMakeLists.txt
add_subdirectory(${SLANG_SOURCE_DIR} ${CMAKE_BINARY_DIR}/slang-build)
target_link_libraries(miki_shader PUBLIC slang::slang)
```

Slang source lives in `third_party/slang/` (git submodule). Full source build
enables: custom patches, LTO, debug symbols, deterministic codegen.

### 10.2 Prebuilt Hybrid (CI Fast Path)

```cmake
option(MIKI_SLANG_PREBUILT "Use prebuilt Slang DLLs instead of source build" OFF)
if(MIKI_SLANG_PREBUILT)
    # third_party/slang-prebuilt/ contains prebuilt binaries per platform
    add_library(slang::slang SHARED IMPORTED)
    set_target_properties(slang::slang PROPERTIES
        IMPORTED_LOCATION "${SLANG_PREBUILT_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}slang${CMAKE_SHARED_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${SLANG_PREBUILT_DIR}/include"
    )
endif()
```

### 10.3 WASM Build (Decision #6)

For Emscripten/WASM shipping:

- All WGSL blobs pre-compiled at build time via `SlangCompiler::CompileActiveTargets({WGSL})`
- Offline tool: `miki-shader-compile --target wgsl --input shaders/ --output shaders/wgsl/`
- WASM runtime loads `.wgsl` blobs directly — no Slang dependency at runtime
- Dev option: `MIKI_WASM_RUNTIME_SLANG=ON` embeds Slang WASM build (~20MB) for live shader editing in browser

---

## 11. Gap Analysis — Reference vs Target

| Component                         | Reference (`D:\repos\miki`)                                | mitsuki Target                                                                                        | Action        |
| --------------------------------- | ---------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | ------------- |
| `SlangCompiler`                   | Pimpl, multi-target, session-per-compile                   | **Reuse** with upgrades: session pool, session reuse for incremental, module caching                  | Refactor Impl |
| `ShaderTypes`                     | Complete: all enums, Blob, Reflection, PermutationKey      | **Reuse** as-is                                                                                       | Direct import |
| `ShaderTargetForBackend`          | Maps OpenGL → SPIRV (GL_ARB_gl_spirv)                      | **Reuse** — correct design                                                                            | Direct import |
| `PermutationCache`                | LRU + disk cache, thread-safe, source hash validation      | **Reuse** with upgrades: `#include`-aware hash (hash all transitively included sources)               | Refactor      |
| `ShaderWatcher`                   | ReadDirectoryChangesW + polling, IncludeDepGraph, debounce | **Reuse** with upgrades: `std::jthread` (C++20 → C++23), transitive dep closure                       | Refactor      |
| `SlangFeatureProbe`               | 29 probes, SPIR-V/DXIL/GLSL/WGSL targets, tier1Only flag   | **Reuse** as-is — well-designed                                                                       | Direct import |
| `IPipelineFactory`                | Dual factory (Main/Compat), 7 pass creation methods        | **Reuse** — matches spec exactly                                                                      | Direct import |
| `PipelineCache`                   | VkPipelineCache + D3D12PipelineLibrary + header validation | **Reuse** as-is                                                                                       | Direct import |
| `probe_*.slang` (29 files)        | Comprehensive test shaders                                 | **Reuse** — covers all target-specific edge cases                                                     | Direct import |
| Incremental module compilation    | Not implemented (new session per compile)                  | **New**: session reuse + module cache across frames                                                   | New code      |
| `#include`-aware disk cache hash  | Hashes only root source file                               | **New**: hash must include all transitively `#include`'d files                                        | New code      |
| Transitive dep graph              | 1-level deep `GetAffected()`                               | **New**: full transitive closure via BFS/DFS                                                          | New code      |
| Offline WGSL compiler tool        | Not implemented                                            | **New**: CLI tool for WASM build pipeline                                                             | New code      |
| Slang source CMake integration    | Prebuilt DLLs                                              | **New**: `add_subdirectory` source build + hybrid option                                              | New CMake     |
| Slang module hierarchy            | Flat: all shaders in one directory                         | **New**: 13 library modules with `module`/`implementing` pattern (§15)                                | New design    |
| Interface-driven tier abstraction | Preprocessor `#ifdef TIER1` branching                      | **New**: Slang `interface` + generics for tier polymorphism (§15.4)                                   | New code      |
| Precompiled `.slang-module`       | Not implemented                                            | **New**: CMake precompile step, ~4.7x compile speedup (§15.8)                                         | New CMake     |
| Async pipeline compilation        | Synchronous PSO creation                                   | **New**: `AsyncTaskManager` + timeline semaphore for non-blocking PSO create (§16)                    | New code      |
| Pipeline ready state machine      | Binary ready/not-ready                                     | **New**: Pending→Compiling→Ready→Stale state machine with deferred destruction (§16.5)                | New code      |
| Neural shader modules             | Not implemented                                            | **New**: `miki-neural` module for NTC, denoiser, NRC (§17, Phase 17+)                                 | New code      |
| GPU data contract validation      | Manual, error-prone                                        | **New**: `Reflect()` auto-validates C++ ↔ Slang struct layout at compile time (§15.6)                 | New code      |
| Shader compilation perf targets   | Informal                                                   | **New**: Formal targets with regression detection in CI (§19)                                         | New process   |
| Link-time specialization          | Not used (preprocessor `#define` permutations)             | **New**: `extern static const` + `extern struct` for precompiled-module-safe specialization (§15.5.2) | New pattern   |
| Capability system                 | Not used                                                   | **New**: `[require]` attributes for compile-time tier safety validation (§15.4.3)                     | New pattern   |
| Metal/MSL target                  | Not supported                                              | **New**: `ShaderTarget::MSL` + `metal_3_1` profile (Phase 15a+) (§3.1)                                | New code      |
| `VK_EXT_shader_object`            | Not considered                                             | **Future**: Pipeline-less shader binding for Tier1 Vulkan (§8.4, Phase 5+)                            | Design only   |
| Session threading model           | Implicit (unsafe concurrent session use)                   | **New**: Session pool per {target, thread} pair, no cross-thread sharing (§4.2)                       | New design    |
| Precompiled module staleness      | Not validated                                              | **New**: `UseUpToDateBinaryModule` + `isBinaryModuleUpToDate()` (§15.8.1.1)                           | New code      |
| SPIR-V specialization constants   | Not used                                                   | **New**: `[SpecializationConstant]` for per-PSO tuning without recompile (§15.5.4)                    | New pattern   |

---

## 12. Detailed Implementation Plan

### 12.0 Overview & Layering Principle

Implementation follows the **Iron Rule**: lower layers must be complete, stable, and fully tested before upper layers build on them. The shader pipeline spans Layers 2–5 of the miki architecture. Each phase below is a self-contained deliverable with explicit gate criteria.

```mermaid
gantt
    title Shader Pipeline Implementation Phases
    dateFormat X
    axisFormat %s
    section Foundation
        Phase 1a - Core Compiler       :p1a, 0, 2
        Phase 1b - Multi-Target         :p1b, after p1a, 2
    section Integration
        Phase 2 - Reflection & Material :p2, after p1b, 3
    section Optimization
        Phase 3a - Precompiled Modules  :p3a, after p2, 2
        Phase 3b - Pipeline State Machine :p3b, after p3a, 2
    section Domain
        Phase 5+ - Domain Modules       :p5, after p3b, 4
    section Platform
        Phase 15a - Metal Backend       :p15a, after p5, 2
    section Advanced
        Phase 17+ - Neural Shaders      :p17, after p5, 2
```

| Phase   | Layer | Focus                                      | Cumulative Tests |
| ------- | :---: | ------------------------------------------ | :--------------: |
| **1a**  |  L2   | Core compiler, types, caching, probes      |       ~60        |
| **1b**  |  L2   | Multi-target, hot-reload, compat           |       ~100       |
| **2**   | L2-L3 | Reflection, material, async PSO            |       ~125       |
| **3a**  | L2-L5 | Precompiled modules, session optimization  |       ~140       |
| **3b**  | L2-L5 | Pipeline state machine, bindless, batching |       ~155       |
| **5+**  | L5-L8 | Domain shader modules (CAD/CAE/RT/XR)      |       ~175       |
| **15a** |  L2   | Metal/MSL backend integration              |       ~190       |
| **17+** |  L5   | Neural shader inference                    |       ~200       |

---

### 12.1 Phase 1a — Core Compiler & Shader Toolchain Foundation

**Goal**: Establish the entire shader compilation infrastructure. A `.slang` file compiles to SPIR-V + DXIL, produces correct reflection data, and creates a renderable pipeline on Vulkan and D3D12.

**Prerequisites**: `00-infra` (ErrorCode, Result), `02-rhi-design` (IDevice, Format, RhiTypes, GpuCapabilityProfile), Slang source in `third_party/slang/`.

#### Step 1: ShaderTypes (§3)

| Deliverable                | Detail                                                                                                                                                                                                                                                |
| -------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ShaderTypes.h`            | `ShaderTarget` enum (SPIRV, DXIL, GLSL, WGSL, MSL). `ShaderStage` enum. `ShaderBlob` (move-only). `ShaderPermutationKey` (64-bit). `ShaderCompileDesc`. `BindingInfo`, `VertexInputInfo`. `ShaderReflection` (full struct). `PermutationCacheConfig`. |
| `ShaderTargetForBackend()` | Constexpr canonical mapping (§3.2). `static_assert` on `BackendType` enum completeness.                                                                                                                                                               |
| **Acceptance**             | All types compile. `ShaderPermutationKey` bit operations roundtrip. `ShaderTargetForBackend` covers all 5 backends.                                                                                                                                   |

#### Step 2: Slang CMake Integration (§10)

| Deliverable                        | Detail                                                                                         |
| ---------------------------------- | ---------------------------------------------------------------------------------------------- |
| `third_party/slang/CMakeLists.txt` | Source build via `add_subdirectory`. `target_link_libraries(miki_shader PUBLIC slang::slang)`. |
| `MIKI_SLANG_PREBUILT` option       | Hybrid option: prebuilt DLL fallback for CI fast path.                                         |
| **Acceptance**                     | `slang::createGlobalSession()` succeeds. Slang version ≥ 2026.3.1 verified at configure time.  |

#### Step 3: SlangCompiler Core (§4.1–4.2)

| Deliverable                | Detail                                                                                                                                                                                                                                                                  |
| -------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `SlangCompiler.h` / `.cpp` | Pimpl. `Create()`, `Compile()`, `CompileDualTarget()`, `Reflect()`, `AddSearchPath()`.                                                                                                                                                                                  |
| `SlangCompiler::Impl`      | `IGlobalSession` (one per instance). Session pool: `{ShaderTarget, threadIndex} → ISession*` (§4.2). Lazy creation with mutex on pool miss only.                                                                                                                        |
| Session profile table      | SPIRV → `spirv_1_6`, DXIL → `sm_6_6`, GLSL → `glsl_430`, WGSL → `wgsl`, MSL → `metal_3_1`. Only SPIRV + DXIL sessions active in Phase 1a.                                                                                                                               |
| Permutation handling       | `ShaderPermutationKey` bits → `#define MIKI_PERMUTATION_BIT_N 1` via `SessionDesc::preprocessorMacros`.                                                                                                                                                                 |
| **Acceptance**             | `Compile("triangle.slang", SPIRV)` returns valid SPIR-V blob (magic `0x07230203`). `Compile("triangle.slang", DXIL)` returns valid DXIL blob (magic `0x43425844`). `CompileDualTarget` returns both. Session pool reuses sessions across calls (verified by Trace log). |

#### Step 4: Reflection Extraction (§4.2 — Reflection Pipeline)

| Deliverable                    | Detail                                                                                                                                          |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `Reflect()` impl               | 8-step extraction pipeline (§4.2): bindings, vertex inputs, push constant size, thread group size, module constants, struct layouts.            |
| Slang type → `rhi::Format` map | `float32×1→R32_FLOAT`, `float32×2→RG32_FLOAT`, etc. (§4.2 table).                                                                               |
| **Acceptance**                 | `Reflect("pbr_forward.slang")` returns correct binding set/binding indices. `pushConstantSize > 0`. `vertexInputs` has correct location/format. |

#### Step 5: Push Constant Emulation (§4.3)

| Deliverable         | Detail                                                                                                                                                                          |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Slang codegen layer | `[vk::push_constant]` → UBO rewrite for WGSL/GLSL targets (Slang automatic).                                                                                                    |
| RHI runtime layer   | `ICommandBuffer::PushConstants()` → shadow UBO on WebGPU/OpenGL backends. Reserved binding convention (§4.3 table).                                                             |
| **Acceptance**      | Push constant data reaches shader on Vulkan (native) and will reach shader on GL/WebGPU (deferred to Phase 1b backend bring-up). Debug `assert(size <= max)` fires on oversize. |

#### Step 6: PermutationCache (§5)

| Deliverable                   | Detail                                                                                                                                                                  |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| `PermutationCache.h` / `.cpp` | Pimpl. `GetOrCompile()`, `Insert()`, `Clear()`, `Size()`.                                                                                                               |
| L1: in-memory LRU             | `std::unordered_map` + `std::list` for O(1) lookup + O(1) evict. Configurable max entries.                                                                              |
| L2: disk cache                | Path: `{cacheDir}/{FNV1a_hash}.{spv                                                                                                                                     | dxil}`. `.hash` sidecar for source content hash validation. Stale rejection on hash mismatch. |
| Thread safety                 | `std::mutex` around LRU operations. Disk I/O outside lock.                                                                                                              |
| **Acceptance**                | First call → cache miss → compile → store. Second call → L1 hit (< 1μs). Restart → L2 hit (< 5ms). Source edit → stale rejection → recompile. LRU eviction at capacity. |

#### Step 7: PipelineCache (§5.5)

| Deliverable                | Detail                                                                                                                                                         |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `PipelineCache.h` / `.cpp` | `Load()`, `Save()`, `GetNativeHandle()`.                                                                                                                       |
| Vulkan impl                | `VkPipelineCache` create/serialize/deserialize. `PipelineCacheHeader` validation (magic, driver version, device ID).                                           |
| D3D12 impl                 | `ID3D12PipelineLibrary` — serialize/load.                                                                                                                      |
| GL/WebGPU/Mock             | No-op pass-through.                                                                                                                                            |
| **Acceptance**             | Cold start → create empty cache → save to disk. Second run → load from disk → faster PSO creation. Header mismatch (different GPU) → silent discard + rebuild. |

#### Step 8: SlangFeatureProbe — Universal Probes (§7)

| Deliverable                    | Detail                                                                                                                                                                                                                                                                          |
| ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `SlangFeatureProbe.h` / `.cpp` | Stateless. `RunAll()`, `RunSingle()`.                                                                                                                                                                                                                                           |
| `shaders/tests/probe_*.slang`  | 14 universal probe shaders: `struct_array`, `atomics_32`, `atomics_64`, `subgroup_ballot`, `subgroup_shuffle`, `subgroup_clustered`, `push_constants`, `texture_array`, `compute_shared`, `barrier_semantics`, `binding_map`, `half_precision`, `image_atomics`, `mesh_shader`. |
| Tier degradation               | `tier1Only=true` probes (mesh_shader, atomics_64) expected to fail on GLSL/WGSL. Correct rejection reported as pass.                                                                                                                                                            |
| **Acceptance**                 | `RunAll(compiler, {SPIRV, DXIL})` → 14/14 pass on both targets. `mesh_shader` probe → pass on SPIRV, skip on WGSL.                                                                                                                                                              |

#### Step 9: IPipelineFactory Skeleton (§8)

| Deliverable                 | Detail                                                                                                                                                                     |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `IPipelineFactory.h`        | Abstract factory: `Create()`, `CreateGeometryPass()`, + 6 stub methods returning `NotImplemented`.                                                                         |
| `MainPipelineFactory.cpp`   | `CreateGeometryPass()` → mesh shader pipeline (Vulkan/D3D12).                                                                                                              |
| `CompatPipelineFactory.cpp` | `CreateGeometryPass()` → vertex shader pipeline.                                                                                                                           |
| Dispatch                    | `IPipelineFactory::Create(IDevice&)` → `GpuCapabilityProfile::GetTier()` → Main or Compat.                                                                                 |
| **Acceptance**              | `IPipelineFactory::Create()` returns `MainPipelineFactory` on Tier1. `CreateGeometryPass()` returns valid `PipelineHandle`. `CreateShadowPass()` returns `NotImplemented`. |

#### Step 10: Output Blob Validation & Logging (§14)

| Deliverable            | Detail                                                                                                                                       |
| ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `ValidateOutputBlob()` | Debug-only structural checks (§14.3): SPIR-V magic + alignment, DXIL container magic, GLSL `#version`, WGSL min 8 bytes, MSL `metal_stdlib`. |
| Logging integration    | All `MIKI_LOG_*` calls per §14.1 table. `LogCategory::Shader`. Tag prefix convention `[SlangCompiler]`, `[PermutationCache]`.                |
| **Acceptance**         | Corrupted blob → `ValidateOutputBlob` returns error in debug. Release build → zero validation cost (`#ifndef NDEBUG`).                       |

#### Step 11: `miki-core` Module Skeleton (§15.9)

| Deliverable                    | Detail                                                                                                     |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------- |
| `shaders/miki/miki-core.slang` | `module miki_core;` + `__include` for all 6 implementing files.                                            |
| `core/types.slang`             | `GpuInstance`, `GpuLight` structs with explicit padding (§15.6).                                           |
| `core/constants.slang`         | `kMaxInstances`, `kMaxLights`, `kMaxBindlessTextures`, etc. (§15.9).                                       |
| `core/push_constants.slang`    | `PushConstants` struct + `loadPushConstants()` with `__target_switch` for SPIRV/HLSL only in Phase 1a.     |
| `core/color_space.slang`       | sRGB ↔ linear conversion. Stubs for ACES, Rec2020.                                                         |
| `core/packing.slang`           | R11G11B10 pack/unpack, octahedral normal encode/decode.                                                    |
| `core/bindless.slang`          | Stub — `sampleBindlessTexture()` and `loadBindlessBuffer()` declared, implementation deferred to Phase 3b. |
| **Acceptance**                 | `import miki_core;` compiles on SPIR-V and DXIL targets. All 6 implementing files parse without error.     |

#### Phase 1a Gate Criteria

| Criterion               | Metric                                                                                                                                                      |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| All 60+ unit tests pass | ShaderTypes(5), SlangCompiler(15), PermutationCache(10), ShaderWatcher(0, deferred), Probe SPIRV(14), Probe DXIL(14), IPipelineFactory(5), PipelineCache(5) |
| Dual-target E2E         | `triangle.slang` → SPIR-V + DXIL → `CreateGeometryPass()` → pixel on screen (Vulkan + D3D12)                                                                |
| Compile performance     | Single shader SPIR-V < 150ms cold, < 1μs L1 cache hit                                                                                                       |
| Session pool            | No mutex contention on single-threaded path (Trace log confirms pool hit)                                                                                   |
| `miki-core` module      | `import miki_core;` compiles on SPIR-V + DXIL                                                                                                               |
| CI green                | All tests pass on Windows Clang 20 + libc++ × (Vulkan Tier1, D3D12)                                                                                         |

---

### 12.2 Phase 1b — Multi-Target, Hot-Reload & Compat Validation

**Goal**: Extend compilation to all 5 targets (SPIR-V, DXIL, GLSL, WGSL, MSL stubs). Enable shader hot-reload. Validate `CompatPipelineFactory` on OpenGL + WebGPU. Establish `[require]` capability annotations.

**Prerequisites**: Phase 1a complete. OpenGL and WebGPU backend `IDevice` implementations available.

#### Step 1: Multi-Target Compilation (§4.1)

| Deliverable              | Detail                                                                                                                                                       |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `CompileAllTargets()`    | Iterate over all 5 target sessions. Returns `std::array<ShaderBlob, 5>`.                                                                                     |
| `CompileActiveTargets()` | Compile for a user-specified subset of targets. Returns `std::vector<ShaderBlob>`.                                                                           |
| GLSL session             | `glsl_430` profile. Slang GLSL codegen. Post-process: `gl_VertexIndex` → `gl_VertexID` (Vulkan→OpenGL semantic).                                             |
| WGSL session             | `wgsl` profile. Push constant → UBO rewrite verified.                                                                                                        |
| MSL session              | `metal_3_1` profile. **Stub**: session created but MSL validation deferred to Phase 15a.                                                                     |
| **Acceptance**           | `CompileAllTargets("triangle.slang")` → 5 blobs, all non-empty. SPIR-V blob passes `ValidateOutputBlob`. GLSL blob contains `#version`. WGSL blob ≥ 8 bytes. |

#### Step 2: GLSL & WGSL Feature Probes (§7.2)

| Deliverable                    | Detail                                                                                                                       |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| `probe_glsl_*.slang` (8 files) | BDA→SSBO mapping, binding layout, texture units, workgroup, shared memory, image load/store, atomics, push constant UBO.     |
| `probe_wgsl_*.slang` (7 files) | Storage alignment, workgroup limits, no-64bit-atomics error, group binding, texture sample, array stride, push constant UBO. |
| **Acceptance**                 | `RunAll(compiler, {SPIRV, DXIL, GLSL, WGSL})` → all 29 probes pass. Tier1-only probes correctly rejected on GLSL/WGSL.       |

#### Step 3: ShaderWatcher (§6)

| Deliverable                | Detail                                                                                                                                                                                                                |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ShaderWatcher.h` / `.cpp` | Pimpl. `Create()`, `Start()`, `Stop()`, `Poll()`, `GetGeneration()`, `GetLastErrors()`, `IsRunning()`.                                                                                                                |
| `IncludeDepGraph`          | Scan `#include "..."` and `import module;` directives. `GetAffected(changedFile)` → 1-level dependency resolution.                                                                                                    |
| File watching              | Windows: `ReadDirectoryChangesW` + overlapped I/O. POSIX: `std::filesystem::last_write_time` polling (200ms).                                                                                                         |
| Debounce                   | Configurable (default 100ms). Coalesces rapid file saves.                                                                                                                                                             |
| Background recompile       | `std::jthread` (C++23). Own session instance from session pool. Recompiles affected modules for all configured targets.                                                                                               |
| Pipeline swap protocol     | Generation counter. `Poll()` returns `ShaderChange` with new blob. Rendering code compares generation → recreate pipeline (§6.6).                                                                                     |
| Error overlay              | `GetLastErrors()` feeds ImGui error panel in debug builds.                                                                                                                                                            |
| **Acceptance**             | `Start("shaders/")` → modify `.slang` → `Poll()` returns `ShaderChange` with new blob < 100ms. `GetGeneration()` incremented. Compile error → `GetLastErrors()` non-empty, generation unchanged. `Stop()` idempotent. |

#### Step 4: CompatPipelineFactory Validation

| Deliverable                      | Detail                                                                                                                            |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `CompatPipelineFactory` bring-up | `CreateGeometryPass()` creates vertex shader pipeline on OpenGL (deferred GL command buffer) and WebGPU (`wgpu::RenderPipeline`). |
| Push constant UBO                | Verify Slang-rewritten UBO push constants work on GL (binding 0, 128B) and WebGPU (group 0 binding 0, 256B).                      |
| **Acceptance**                   | `triangle.slang` renders correct output on all 5 backends. Golden image diff < 5% across backends.                                |

#### Step 5: `miki-core` + `miki-math` Modules (§15)

| Deliverable                 | Detail                                                                                                                                          |
| --------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `core/push_constants.slang` | Complete `__target_switch` for all 5 targets: `spirv` (native), `hlsl` (root constants), `metal` (argument buffer), `wgsl` (UBO), `glsl` (UBO). |
| `miki-math.slang`           | `module miki_math;` + `__include` for SH, noise, sampling, quaternion, matrix_utils.                                                            |
| `math/*.slang` (5 files)    | Implementing files with core math utilities used by upper modules.                                                                              |
| **Acceptance**              | `import miki_core; import miki_math;` compiles on all 5 targets. `loadPushConstants()` returns correct data per-target.                         |

#### Step 6: Capability Annotations (§15.4.3)

| Deliverable                         | Detail                                                                                                                                                                                  |
| ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[require]` on Tier1-only functions | All mesh shader, 64-bit atomic, RT-related functions annotated with `[require(spirv_1_4, SPV_EXT_mesh_shader)]` / `[require(spirv_1_0, spvInt64Atomics)]` / `[require(hlsl, _sm_6_5)]`. |
| Transitive inference verified       | Compat pass shader that accidentally calls Tier1-only function → compile error.                                                                                                         |
| **Acceptance**                      | Compat pass calling `dispatchMeshlets()` → Slang emits capability error. Same pass on SPIR-V → compiles.                                                                                |

#### Phase 1b Gate Criteria

| Criterion           | Metric                                                                                                                 |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| All 100+ tests pass | Phase 1a (60) + GLSL probes (8) + WGSL probes (7) + ShaderWatcher (8) + Multi-target (5) + Compat (5) + Capability (4) |
| 5-backend triangle  | Golden image diff < 5% across all backends                                                                             |
| Hot-reload < 100ms  | File change → visual update on Vulkan backend                                                                          |
| Capability safety   | Compat shader calling Tier1 function → compile error                                                                   |
| CI green            | All tests pass on full CI matrix (Windows + Linux × all backends)                                                      |

---

### 12.3 Phase 2 — Reflection-Driven Layout, Material System & Async PSO

**Goal**: Shader reflection drives descriptor layout generation. Material system uses link-time specialization. GPU data contracts validated at compile time. Pipeline creation offloaded to background threads.

**Prerequisites**: Phase 1b complete. `03-sync` (AsyncTaskManager, timeline semaphores) available.

#### Step 1: Reflection-Driven Descriptor Layout (§9.3)

| Deliverable             | Detail                                                                                                                                 |
| ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| Auto-layout generation  | `Reflect()` → `ShaderReflection::bindings` → filter by set → auto-generate `DescriptorSetLayoutDesc` for sets 0–2.                     |
| Layout change detection | Debug-mode comparison of current vs previous reflection output. Layout-breaking changes → warning.                                     |
| **Acceptance**          | Forward pass shader compiled → reflected bindings → `CreateDescriptorLayout()` succeeds. Descriptor set created from reflected layout. |

#### Step 2: `miki-brdf` Module with Link-Time Specialization (§15.4, §15.5.2)

| Deliverable                   | Detail                                                                                                                                                                                                         |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `miki-brdf.slang`             | `module miki_brdf;` + 9 implementing files (dspbr, ggx, diffuse, sheen, clearcoat, iridescence, sss, aniso, material_interface).                                                                               |
| `IMaterial` interface         | `evaluate()`, `sample()`, `pdf()` with `associatedtype ShadingData` (§15.4.1).                                                                                                                                 |
| Link-time type specialization | `extern struct MaterialImpl : IMaterial;` in pass shaders. Specialization module synthesized at compile time: `export struct MaterialImpl : IMaterial = DSPBR;` (§15.5.2).                                     |
| `SlangCompiler` extension     | `ShaderCompileDesc` gains `linkTimeConstants` and `linkTimeTypes` maps. `Compile()` synthesizes specialization module, loads via `loadModuleFromSourceString()`, includes in `createCompositeComponentType()`. |
| **Acceptance**                | `material_resolve.slang` with `extern struct MaterialImpl : IMaterial` → link with DSPBR → monomorphized codegen (no virtual dispatch in SPIR-V). Dead code from unused `IMaterial` methods eliminated.        |

#### Step 3: GPU Data Contract Validation (§15.6)

| Deliverable              | Detail                                                                                                               |
| ------------------------ | -------------------------------------------------------------------------------------------------------------------- |
| `ValidateStructLayout()` | `Reflect("miki_core")` → `StructLayout` for `GpuInstance`, `GpuLight` → compare field offsets with C++ `offsetof()`. |
| CI integration           | Struct layout validation runs on every CI build. Mismatch → hard error.                                              |
| **Acceptance**           | Modify `GpuInstance` field order in C++ without updating Slang → CI fails with explicit offset mismatch error.       |

#### Step 4: Async Pipeline Compilation (§16.1)

| Deliverable                     | Detail                                                                                                                                                 |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `AsyncTaskManager` integration  | PSO compilation submitted as async task. Background thread: `SlangCompiler::Compile()` → `IDevice::CreateGraphicsPipeline(blob, PipelineCache)`.       |
| Timeline semaphore completion   | Completion point = `{AsyncCompute, signalValue}`. Main thread polls `IsComplete()`.                                                                    |
| Pipeline swap at frame boundary | New pipeline swapped in at `BeginFrame`. Old pipeline → `DeferredDestructor::Drain(timelineValue)`.                                                    |
| **Acceptance**                  | Submit 10 PSO compilations → main thread continues rendering → all 10 complete within 2 seconds → pipelines usable. No frame stall during compilation. |

#### Step 5: Link-Time Constants (§15.5.2)

| Deliverable                   | Detail                                                                                                                                                  |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `extern static const` pattern | `miki-lighting` declares `extern static const int kMaxClusterLights = 128;`. Specialization module overrides to 256 at link.                            |
| Constant folding verified     | SPIR-V output shows `kMaxClusterLights` inlined as immediate. No `OpLoad`.                                                                              |
| **Acceptance**                | Precompiled `miki-lighting.slang-module` linked with specialization module → `kMaxClusterLights = 256` constant-folded in output SPIR-V. Loop unrolled. |

#### Phase 2 Gate Criteria

| Criterion                | Metric                                                                                                                                                        |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| All 125+ tests pass      | Phase 1b (100) + Reflection E2E (3) + Link-time E2E (4) + Struct layout E2E (3) + Async PSO E2E (5) + Material interface E2E (5) + Link-time constant E2E (3) |
| Reflection → layout      | Forward pass auto-generates correct `DescriptorSetLayout` from reflection                                                                                     |
| Link-time specialization | `IMaterial` → `DSPBR` produces monomorphized codegen, verified via SPIR-V inspection                                                                          |
| Async PSO                | 88 PSO compilations complete in < 4s on thread pool, no frame stall                                                                                           |
| Struct layout CI         | C++↔Slang struct layout validated per CI build                                                                                                                |

---

### 12.4 Phase 3a — Precompiled Modules & Compilation Optimization

**Goal**: Achieve ~4.7× compilation speedup via precompiled `.slang-module` blobs. Session reuse eliminates redundant parsing across frames. Transitive dependency graph enables correct cache invalidation. SPIR-V specialization constants for per-PSO tuning.

**Prerequisites**: Phase 2 complete.

#### Step 1: Precompiled Module CMake Integration (§15.8.1)

| Deliverable                  | Detail                                                                                                                                                                                      |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `shaders/CMakeLists.txt`     | `MIKI_SHADER_MODULES` list (13 modules). `foreach` loop: `slangc` → `.slang-module`. `DEPENDS` includes primary + all `implementing` files via `GLOB_RECURSE` with `${MOD_SHORT}/` pattern. |
| `miki_shader_modules` target | `add_custom_target(miki_shader_modules ALL DEPENDS ${MIKI_PRECOMPILED_MODULES})`.                                                                                                           |
| **Acceptance**               | `cmake --build . --target miki_shader_modules` → 13 `.slang-module` files in `precompiled/`. Edit `core/types.slang` → only `miki_core.slang-module` rebuilds.                              |

#### Step 2: Runtime Staleness Check (§15.8.1.1)

| Deliverable               | Detail                                                                                                                                                                |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `UseUpToDateBinaryModule` | `SessionDesc::compilerOptionEntries` enables staleness check. `loadModule()` → `isBinaryModuleUpToDate()` → recompile from source if stale.                           |
| **Acceptance**            | Precompile `miki_core` → modify `core/types.slang` → `loadModule("miki_core")` detects stale `.slang-module` → recompiles from source → output matches fresh compile. |

#### Step 3: Session Reuse & Incremental Module Compilation

| Deliverable         | Detail                                                                                                                                                       |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Session persistence | Sessions kept alive across frames (not destroyed after each compile). `loadModule()` caches parsed modules within a session. Only re-parses changed modules. |
| Module cache stats  | `SlangCompiler::GetCacheStats()` → hit/miss counts for session module cache.                                                                                 |
| **Acceptance**      | Frame 1: compile pass shader (120ms). Frame 2: same pass, session hit (25ms). Modified module: only that module re-parsed, others from cache.                |

#### Step 4: Transitive Dependency Graph (§6.2 upgrade)

| Deliverable                          | Detail                                                                                                                                                                                                   |
| ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `IncludeDepGraph` transitive closure | Upgrade from 1-level to full BFS/DFS transitive closure. `GetAffected("core/types.slang")` → returns all files that transitively import `miki_core`.                                                     |
| `#include`-aware cache hash          | `PermutationCache` key hash includes hashes of all transitively `#include`'d / `import`'d files, not just root source.                                                                                   |
| **Acceptance**                       | Modify `core/types.slang` → `GetAffected()` returns `miki-core.slang`, all passes importing `miki_core`, and all modules importing `miki_core` (e.g., `miki-brdf`). Cache invalidation cascade verified. |

#### Step 5: `miki-geometry` + `miki-lighting` Modules

| Deliverable                   | Detail                                                                                                                         |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `miki-geometry.slang`         | 8 implementing files: meshlet, culling, hiz, lod, visibility_buffer, macro_binning, sw_rasterizer, vertex_pipeline.            |
| `miki-lighting.slang`         | 6 implementing files: clustered, ibl, area_light, ltc_lut, restir, ddgi.                                                       |
| `IGeometryPipeline` interface | `fetchVertex()`, `transformToClip()`, `encodePrimitiveId()` (§15.4.2).                                                         |
| `[require]` annotations       | Mesh shader functions → `[require(spirv_1_4, SPV_EXT_mesh_shader)]`. 64-bit atomics → `[require(spirv_1_0, spvInt64Atomics)]`. |
| **Acceptance**                | `import miki_geometry; import miki_lighting;` compiles on all 5 targets. Precompiled `.slang-module` loads correctly.          |

#### Step 6: SPIR-V Specialization Constants (§15.5.4)

| Deliverable                      | Detail                                                                                                                                                                                   |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[SpecializationConstant]` usage | `kCullWorkgroupSize` and `kEnableOcclusionCulling` in `culling.slang` use `[vk::constant_id(N)]`.                                                                                        |
| PSO creation integration         | `VkSpecializationInfo` populated from `ShaderCompileDesc::specializationConstants` map.                                                                                                  |
| **Acceptance**                   | Compile `culling.slang` → SPIR-V contains `OpSpecConstant`. Create PSO with workgroup size 128 → patched at PSO creation. Driver dead-code-eliminates disabled occlusion culling branch. |

#### Phase 3a Gate Criteria

| Criterion           | Metric                                                                                                                    |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| All 140+ tests pass | Phase 2 (125) + Precompiled E2E (3) + Staleness E2E (2) + Transitive dep (3) + Module compile (4) + Spec constant E2E (3) |
| Compilation speedup | Single pass: < 40ms warm (vs ~120ms cold) = ~3× minimum. Full rebuild: < 15s (vs ~42s) = ~2.8× minimum                    |
| Session reuse       | Frame-to-frame session hit rate > 95%                                                                                     |
| Cache invalidation  | Transitive dependency change propagates correctly to all dependent caches                                                 |
| CI precompile       | All 13 modules precompile in CI. Edit any implementing file → correct module rebuilds                                     |

---

### 12.5 Phase 3b — Pipeline State Machine, Batching & Bindless

**Goal**: Production-grade pipeline lifecycle management. Startup pipeline batching for < 100ms warm start. Bindless resource access pattern fully operational with `VK_EXT_descriptor_buffer`.

**Prerequisites**: Phase 3a complete. Resource system (`BindlessTable`) from L3 available.

#### Step 1: Pipeline Ready State Machine (§16.5)

| Deliverable          | Detail                                                                                                                                                                         |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `PipelineState` enum | `Pending`, `Compiling`, `Ready`, `Stale`, `Failed`.                                                                                                                            |
| State transitions    | `CreateAsync()` → Pending. Background thread → Compiling → Ready/Failed. Source change → Stale → Compiling. Error → Failed → hot-reload retry → Compiling.                     |
| Deferred destruction | Old pipeline kept alive until `DeferredDestructor::Drain(swapPoint)` confirms GPU finished using it.                                                                           |
| **Acceptance**       | Pipeline transitions through all states. Stale → Compiling → Ready with zero frame stall. Failed pipeline → render pass skips gracefully (uses fallback or previous pipeline). |

#### Step 2: Pipeline Creation Batching (§16.4)

| Deliverable                       | Detail                                                                                                                                                             |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Startup batching                  | All 88+ passes submit PSO compilations to thread pool in parallel.                                                                                                 |
| Priority classes                  | Critical-path PSOs (depth, geometry, resolve, present) → high priority, block first frame. Non-critical (CAD, CAE, debug) → low priority, complete asynchronously. |
| `PipelineCache` (L3) acceleration | Second launch: warm driver cache → < 100ms total startup.                                                                                                          |
| **Acceptance**                    | Cold start: all critical PSOs ready within 2s. Warm start: < 100ms. Non-critical PSOs complete within 5s. No frame stall after first frame.                        |

#### Step 3: Bindless Resource Access (§15.7, §9.2)

| Deliverable                       | Detail                                                                                                                                                                                                               |
| --------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `core/bindless.slang` full impl   | `sampleBindlessTexture()`, `loadBindlessBuffer()` with `NonUniformResourceIndex`. Set 3 fixed layout: `globalTextures[]`, `globalBuffers[]`, `globalSamplers[]`.                                                     |
| Backend-specific descriptor model | Vulkan Tier1: `VK_EXT_descriptor_buffer`. D3D12: descriptor heap. Vulkan Tier2: `VkDescriptorSet` + `UPDATE_AFTER_BIND`. WebGPU: chunked bind groups (256 limit). OpenGL: `GL_ARB_bindless_texture` / SSBO fallback. |
| **Acceptance**                    | Shader samples `globalTextures[N]` via bindless → correct texture fetched on all Tier1/2 backends. `NonUniformResourceIndex` emits correct SPIR-V `OpGroupNonUniformElect` guard.                                    |

#### Step 4: `miki-shadow` + `miki-postfx` Modules

| Deliverable         | Detail                                                                                                                         |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `miki-shadow.slang` | 3 implementing files: vsm, csm, shadow_atlas.                                                                                  |
| `miki-postfx.slang` | 12 implementing files: bloom, dof, motion_blur, tonemap, taa, fxaa, cas, color_grade, ssr, outline, ao/gtao, ao/ssao, ao/rtao. |
| **Acceptance**      | All pass shaders importing these modules compile on all 5 targets. Precompiled `.slang-module` loads correctly.                |

#### Step 5: Multi-Queue Pipeline Dependency (§16.3)

| Deliverable      | Detail                                                                                                                                  |
| ---------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| Cross-queue sync | Pipeline hot-reload respects cross-queue dependencies via timeline semaphore. Graphics → Compute → Graphics dependency chain validated. |
| **Acceptance**   | Hot-reload GTAO shader → async compute pipeline recreated → graphics pipeline waits for compute completion → correct AO output.         |

#### Phase 3b Gate Criteria

| Criterion                | Metric                                                                                             |
| ------------------------ | -------------------------------------------------------------------------------------------------- |
| All 155+ tests pass      | Phase 3a (140) + Pipeline state machine (5) + Batching (3) + Bindless E2E (4) + Module compile (3) |
| Warm startup             | < 100ms for all critical-path PSOs with warm `PipelineCache`                                       |
| Bindless                 | Correct texture fetch via bindless on Vulkan Tier1 (descriptor buffer) + D3D12 (heap)              |
| Pipeline lifecycle       | Full state machine cycle: Pending→Compiling→Ready→Stale→Compiling→Ready with zero frame stall      |
| Hot-reload + cross-queue | Shader hot-reload on compute pipeline with graphics dependency → correct output                    |

---

### 12.6 Phase 5+ — Domain Shader Modules

**Goal**: Implement domain-specific shader modules as rendering features are built. Each module follows the established `module`/`implementing` pattern and links against precompiled library modules.

**Prerequisites**: Phase 3b complete. Corresponding rendering features (CAD, CAE, RT, XR) in development.

#### Module Delivery Order

| Module       |      Trigger Phase      | Dependencies                                  | Key Features                                                                                                            |
| ------------ | :---------------------: | --------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `miki-cad`   |   Phase 7b (CAD Core)   | `miki-core`, `miki-geometry`                  | GPU HLR, section plane/volume, ray pick, measure, boolean preview, draft angle, explode                                 |
| `miki-cae`   |   Phase 8b (CAE Vis)    | `miki-core`, `miki-geometry`, `miki-lighting` | FEM mesh + contour, scalar/vector field, streamline, isosurface (marching cubes), tensor glyph, point cloud splat + EDL |
| `miki-rt`    | Phase 6b (GPU Geometry) | `miki-core`, `miki-lighting`, `miki-brdf`     | RT reflections, RT shadows, RT GI, progressive path tracer, temporal + spatial denoiser                                 |
| `miki-xr`    |      Phase 14 (XR)      | `miki-core`, `miki-geometry`                  | Single-pass stereo (VK_KHR_multiview), foveated rendering (VRS), late-latch reprojection                                |
| `miki-debug` |        Phase 3b+        | `miki-core`                                   | GPU debug viz, wireframe overlay, normal/UV/ID visualization, nanite LOD overlay                                        |

#### Per-Module Delivery Checklist

Each module follows this standard checklist:

1. **Module declaration**: `shaders/miki/miki-{name}.slang` with `module miki_{name};` + `__include` for all implementing files.
2. **Implementing files**: `shaders/miki/{name}/*.slang` with `implementing miki_{name};`.
3. **`[require]` annotations**: All Tier1-only functions annotated (mesh shader, RT, 64-bit atomics).
4. **Precompiled module**: CMake target produces `.slang-module`. CI validates.
5. **Pass shaders**: `shaders/passes/{pass_name}.slang` importing the module.
6. **5-target compilation**: All pass shaders compile on all 5 targets (or correctly reject on incompatible targets via `[require]`).
7. **Link-time specialization**: Where applicable, use `extern struct`/`extern static const` for tier-variant behavior.
8. **Tests**: Module import E2E, precompiled module E2E, capability rejection E2E (if Tier1-only features present).

#### `VK_EXT_shader_object` Evaluation (§8.4)

| Deliverable                         | Detail                                                                                                                                                                               |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Performance benchmark               | Compare PSO model vs shader object model on Tier1 hardware: startup time, hot-reload latency, memory usage, draw call overhead.                                                      |
| `IPipelineFactory` migration design | If shader objects are superior: `MainPipelineFactory::CreateGeometryPass()` returns `PipelineHandle` wrapping `VkShaderEXT` tuple instead of `VkPipeline`. Pass-level API unchanged. |
| **Decision point**                  | Adopt only if ≥20% improvement in at least one key metric and no regression in others. Record trade-off in code comments.                                                            |

---

### 12.7 Phase 15a+ — Metal Backend Shader Validation

**Goal**: Full MSL compilation and validation. Metal-specific `__target_switch` paths operational. Metal descriptor argument buffer layout correct.

**Prerequisites**: Metal `IDevice` backend available. Phase 5+ domain modules complete.

#### Step 1: MSL Compilation Probes

| Deliverable                            | Detail                                                                                                                                                                                                                                   |
| -------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MSL validation in `ValidateOutputBlob` | Verify `#include <metal_stdlib>` or `using namespace metal` in MSL output.                                                                                                                                                               |
| MSL-specific feature probes            | 15 probes: mesh shader (Metal 3.x), argument buffer, simdgroup, threadgroup, texture sampling, buffer addressing, half precision, compute dispatch, render pipeline, depth compare, stencil, vertex/fragment, blend, MSAA, tile shading. |
| **Acceptance**                         | `RunAll(compiler, {SPIRV, DXIL, GLSL, WGSL, MSL})` → all applicable probes pass on MSL. Tier1-only probes correctly handled.                                                                                                             |

#### Step 2: Metal-Specific `__target_switch` Paths

| Deliverable                      | Detail                                                                                                                                     |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `loadPushConstants()` Metal path | `__target_switch case metal:` → argument buffer or `[[buffer(0)]]`.                                                                        |
| Bindless Metal path              | Metal argument buffer for Set 3 bindless layout.                                                                                           |
| **Acceptance**                   | `miki-core` module compiles on MSL. `loadPushConstants()` emits correct Metal code. Bindless pattern generates argument buffer references. |

#### Step 3: Metal Descriptor Argument Buffer Layout

| Deliverable                     | Detail                                                                                                                              |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Set 3 → argument buffer mapping | Metal argument buffers map to Slang `[[vk::binding(N, 3)]]` annotations.                                                            |
| Per-pass descriptor integration | Reflection-driven layout generates Metal argument buffer descriptors for sets 0–2.                                                  |
| **Acceptance**                  | Full rendering pipeline shaders compile to MSL with correct argument buffer layout. Pass shaders render correctly on Metal backend. |

---

### 12.8 Phase 17+ — Neural Shader Inference

**Goal**: GPU-side neural network inference for neural texture compression, ML denoising, and neural radiance cache. Uses Slang's standard `ILayer` / `Linear` / `ReLU` types.

**Prerequisites**: Phase 5+ complete. `slang_neural` module available in Slang ≥ 2026.3.1.

#### Step 1: `miki-neural` Module Foundation

| Deliverable            | Detail                                                                                             |
| ---------------------- | -------------------------------------------------------------------------------------------------- |
| `miki-neural.slang`    | `module miki_neural;` + 3 implementing files (neural_texture, neural_denoiser, nrc).               |
| `import slang_neural;` | Verify Slang neural module (`ILayer`, `Linear<In,Out>`, `ReLU`) compiles on SPIR-V + DXIL targets. |
| **Acceptance**         | `import miki_neural;` compiles. `NeuralTextureDecoder` (4→16→16→4 MLP) compiles to compute shader. |

#### Step 2: Neural Texture Compression

| Deliverable            | Detail                                                                                                                                           |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `NeuralTextureDecoder` | Small MLP: `Linear<4,16> → ReLU → Linear<16,16> → ReLU → Linear<16,4>`. Input: (uv, mip, feature). Output: RGBA.                                 |
| Weight loading         | SSBO/StructuredBuffer containing trained weights. Loaded from disk at init.                                                                      |
| **Acceptance**         | `NeuralTextureDecoder::decode(uv, mip, feature)` → correct RGBA output matching reference implementation. < 0.5ms per 1024×1024 decode dispatch. |

#### Step 3: ML Denoiser Inference

| Deliverable                    | Detail                                                                                       |
| ------------------------------ | -------------------------------------------------------------------------------------------- |
| Neural denoiser compute shader | Trained U-Net or similar architecture weights loaded from disk. Temporal accumulation input. |
| Integration with RT pipeline   | Noisy RT output → neural denoiser → denoised image in < 2ms at 1080p.                        |
| **Acceptance**                 | PSNR improvement ≥ 6 dB over raw 1spp RT. Temporal stability (no flickering).                |

#### Step 4: Neural Radiance Cache (NRC)

| Deliverable       | Detail                                                                                  |
| ----------------- | --------------------------------------------------------------------------------------- |
| Hash-grid encoder | Multi-resolution hash grid (Müller et al. 2022). Encodes 3D position → feature vector.  |
| Small MLP         | Feature vector → radiance (RGB). Used for indirect illumination cache.                  |
| **Acceptance**    | NRC query < 0.1ms for 1M samples. Visual quality matches reference path tracer at 4spp. |

---

### 12.9 Implementation Dependency Summary

```mermaid
graph TD
    subgraph "Phase 1a: Foundation"
        T1["ShaderTypes"]
        T2["Slang CMake"]
        T3["SlangCompiler Core"]
        T4["Reflection"]
        T5["Push Constant Emu"]
        T6["PermutationCache"]
        T7["PipelineCache"]
        T8["SlangFeatureProbe"]
        T9["IPipelineFactory"]
        T10["Blob Validation"]
        T11["miki-core skeleton"]

        T1 --> T3
        T2 --> T3
        T3 --> T4
        T3 --> T5
        T3 --> T6
        T3 --> T8
        T1 --> T6
        T1 --> T7
        T1 --> T9
        T3 --> T10
        T3 --> T11
    end

    subgraph "Phase 1b: Multi-Target"
        M1["Multi-Target Compile"]
        M2["GLSL/WGSL Probes"]
        M3["ShaderWatcher"]
        M4["Compat Validation"]
        M5["miki-core + miki-math"]
        M6["Capability Annotations"]

        T3 --> M1
        M1 --> M2
        T3 --> M3
        M1 --> M4
        T11 --> M5
        M5 --> M6
    end

    subgraph "Phase 2: Integration"
        I1["Reflection Layout"]
        I2["miki-brdf + Link-Time"]
        I3["Struct Validation"]
        I4["Async PSO"]
        I5["Link-Time Constants"]

        T4 --> I1
        M5 --> I2
        T4 --> I3
        T7 --> I4
        I2 --> I5
    end

    subgraph "Phase 3a: Optimization"
        O1["Precompiled CMake"]
        O2["Staleness Check"]
        O3["Session Reuse"]
        O4["Transitive DepGraph"]
        O5["miki-geometry + lighting"]
        O6["Spec Constants"]

        I2 --> O1
        O1 --> O2
        T3 --> O3
        M3 --> O4
        M5 --> O5
        O1 --> O6
    end

    subgraph "Phase 3b: Production"
        P1["Pipeline State Machine"]
        P2["Pipeline Batching"]
        P3["Bindless"]
        P4["miki-shadow + postfx"]
        P5["Multi-Queue Sync"]

        I4 --> P1
        P1 --> P2
        O5 --> P3
        O5 --> P4
        I4 --> P5
    end

    subgraph "Phase 5+: Domain"
        D1["miki-cad"]
        D2["miki-cae"]
        D3["miki-rt"]
        D4["miki-xr"]
        D5["miki-debug"]
        D6["VK_EXT_shader_object eval"]

        P4 --> D1
        P4 --> D2
        P3 --> D3
        P4 --> D4
        P3 --> D5
        P2 --> D6
    end

    subgraph "Phase 15a+: Metal"
        MT1["MSL Probes"]
        MT2["Metal target_switch"]
        MT3["Argument Buffer"]

        M1 --> MT1
        MT1 --> MT2
        MT2 --> MT3
    end

    subgraph "Phase 17+: Neural"
        N1["miki-neural foundation"]
        N2["Neural Texture"]
        N3["ML Denoiser"]
        N4["NRC"]

        D3 --> N1
        N1 --> N2
        N1 --> N3
        N1 --> N4
    end
```

---

## 13. Test Strategy

### 13.1 Unit Tests

| Test Group                 | Count | Key Tests                                                                                                                                                                                                                                                                                                                  |
| -------------------------- | :---: | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ShaderTypes                |   5   | PermutationKey bit ops, ShaderTargetForBackend mapping, ShaderCompileDesc construction                                                                                                                                                                                                                                     |
| SlangCompiler              |  15   | Create, Compile SPIR-V, Compile DXIL, CompileDualTarget, CompileAllTargets, CompileActiveTargets, Reflect bindings, Reflect vertex inputs, Reflect push constants, Reflect struct layouts, Reflect module constants, AddSearchPath, invalid source error, session pool thread isolation, link-time constant specialization |
| PermutationCache           |  10   | GetOrCompile cache miss, cache hit, LRU eviction, disk cache write, disk cache read, stale hash rejection, Insert, Clear, Size, thread-safety (concurrent GetOrCompile)                                                                                                                                                    |
| ShaderWatcher              |   8   | Create, Start valid dir, Start invalid dir, Poll empty, file change detection, generation increment, GetLastErrors on compile failure, Stop idempotent                                                                                                                                                                     |
| SlangFeatureProbe (SPIR-V) |  14   | All universal probes compiled to SPIR-V                                                                                                                                                                                                                                                                                    |
| SlangFeatureProbe (DXIL)   |  14   | All universal probes compiled to DXIL                                                                                                                                                                                                                                                                                      |
| SlangFeatureProbe (GLSL)   |   8   | GLSL-specific probes (Phase 1b)                                                                                                                                                                                                                                                                                            |
| SlangFeatureProbe (WGSL)   |   7   | WGSL-specific probes (Phase 1b)                                                                                                                                                                                                                                                                                            |
| IPipelineFactory           |   5   | Create dispatch (Tier1→Main, Tier2→Compat), CreateGeometryPass, GetTier, NotImplemented for unimplemented passes                                                                                                                                                                                                           |
| PipelineCache              |   5   | Load empty, Load valid, Load stale header, Save, GetNativeHandle                                                                                                                                                                                                                                                           |

### 13.2 Integration Tests

| Test                   | Description                                                                                                           |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Dual-target E2E        | Compile triangle.slang → SPIR-V + DXIL → create pipelines on Vulkan + D3D12                                           |
| Multi-target E2E       | Compile → all 5 targets (SPIRV/DXIL/GLSL/WGSL/MSL) → create pipelines on all backends (Phase 1b+)                     |
| Hot-reload E2E         | Start watcher → modify .slang → Poll → verify pipeline swap                                                           |
| Permutation E2E        | Same shader with 4 permutation variants → verify distinct blobs                                                       |
| Reflection E2E         | Compile PBR shader → verify bindings match expected layout → auto-generate DescriptorSetLayout                        |
| Module import E2E      | `miki-core` → `miki-brdf` → `passes/material_resolve.slang` chain compiles on all 5 targets (§15.3)                   |
| Precompiled module E2E | Precompile `miki-core` → link pass shader against `.slang-module` → verify identical output to source compile (§15.8) |
| Struct layout E2E      | Compile `GpuInstance` / `GpuLight` → Reflect() → compare field offsets with C++ `offsetof()` (§15.6)                  |
| Async PSO E2E          | Submit PSO creation via `AsyncTaskManager` → poll completion → verify pipeline usable (§16.1)                         |
| Pipeline swap E2E      | Hot-reload shader → verify old pipeline deferred-destroyed after timeline drain → new pipeline active (§16.2)         |
| Bindless access E2E    | Shader samples `globalTextures[N]` via bindless → verify correct texture fetched (§15.7)                              |
| Interface dispatch E2E | `IMaterial` generic with `DSPBR` specialization → verify dead code elimination in SPIR-V output (§15.4)               |
| Link-time const E2E    | Precompile module with `extern static const` → link with specialization module → verify constant folding (§15.5.2)    |
| Link-time type E2E     | Precompile module with `extern struct : IMaterial` → link with DSPBR alias → verify monomorphized codegen (§15.5.2)   |
| Capability reject E2E  | Compile Tier2 pass shader that calls `[require(SPV_EXT_mesh_shader)]` function → verify compile error (§15.4.3)       |
| Staleness check E2E    | Precompile module → modify source → `loadModule` with `UseUpToDateBinaryModule` → verify recompile triggered (§15.8)  |
| Spec constant E2E      | Compile shader with `[SpecializationConstant]` → verify `OpSpecConstant` in SPIR-V → patch at PSO creation (§15.5.4)  |

---

## 14. Logging & Output Validation

### 14.1 Logging Strategy

The shader subsystem uses `LogCategory::Shader` via the structured logger (`StructuredLogger`).
All log calls go through `MIKI_LOG_*` macros — zero-contention hot path (thread-local SPSC ring).

| Component           | Event                     | Level            | Content                                                               |
| ------------------- | ------------------------- | ---------------- | --------------------------------------------------------------------- |
| `SlangCompiler`     | Global session created    | Info             | Initialization confirmation                                           |
| `SlangCompiler`     | Compile entry             | Debug            | Source path, target type, shader stage                                |
| `SlangCompiler`     | Compile success           | Info             | Source path, target type, blob size (bytes), wall-clock time (ms)     |
| `SlangCompiler`     | Compile failure           | Error            | Source path, target type, wall-clock time (ms)                        |
| `SlangCompiler`     | Slang diagnostic          | Error/Warn/Debug | Full diagnostic message from Slang (also stored in `lastDiagnostics`) |
| `SlangCompiler`     | Session pool hit          | Trace            | Target type                                                           |
| `SlangCompiler`     | Session pool miss         | Debug            | Target type + version                                                 |
| `SlangCompiler`     | Session creation failure  | Error            | Target type                                                           |
| `SlangCompiler`     | Session cache invalidated | Debug            | Number of evicted sessions                                            |
| `SlangCompiler`     | GLSL post-process         | Trace            | Vulkan→OpenGL builtin replacement applied                             |
| `SlangCompiler`     | Blob validation failure   | Error            | Target type, source path, validation error detail                     |
| `SlangCompiler`     | Search path added         | Debug            | Path string                                                           |
| `PermutationCache`  | Memory cache hit          | Debug            | Source path, entry point                                              |
| `PermutationCache`  | Disk cache hit            | Debug            | Source path, blob size                                                |
| `PermutationCache`  | Cache miss → compile      | Debug            | Source path, entry point                                              |
| `PermutationCache`  | Disk cache write          | Trace            | Disk file path                                                        |
| `PermutationCache`  | Cache cleared             | Debug            | Number of evicted entries                                             |
| `ShaderWatcher`     | Watch started             | Info             | Canonical watch directory path                                        |
| `ShaderWatcher`     | Watch stopped             | Info             | —                                                                     |
| `ShaderWatcher`     | Files changed             | Debug            | Changed file count, affected file count (transitive)                  |
| `ShaderWatcher`     | Recompile entry           | Debug            | File path                                                             |
| `ShaderWatcher`     | Hot-reload success        | Info             | File path, generation counter                                         |
| `ShaderWatcher`     | Hot-reload failure        | Warn             | File path, error message                                              |
| `ShaderWatcher`     | Invalid watch directory   | Error            | Directory path                                                        |
| `SlangFeatureProbe` | RunAll start              | Info             | Target count, probe count                                             |
| `SlangFeatureProbe` | RunAll summary            | Info             | Pass/fail/skip counts                                                 |
| `SlangFeatureProbe` | Probe pass                | Trace            | Probe name, blob size                                                 |
| `SlangFeatureProbe` | Probe fail                | Trace            | Probe name                                                            |
| `SlangFeatureProbe` | Probe empty blob          | Warn             | Probe name, target                                                    |

### 14.2 Performance Guarantees

| Concern                | Guarantee                                                                                                     |
| ---------------------- | ------------------------------------------------------------------------------------------------------------- |
| **Release build**      | `MIKI_MIN_LOG_LEVEL=2` (Info) — all Trace/Debug calls eliminated at compile time by `if constexpr`            |
| **Hot path cost**      | `MIKI_LOG` writes to thread-local SPSC ring (~50ns). Shader compilation is 1–100ms — logging overhead < 0.05% |
| **Blob validation**    | Guarded by `#ifndef NDEBUG` — zero cost in Release builds                                                     |
| **No heap allocation** | Log messages formatted into 512-byte stack buffer via `std::format_to_n`                                      |

### 14.3 Output Blob Validation (Debug Only)

After successful compilation, `ValidateOutputBlob()` performs lightweight structural checks.
**Enabled only in debug builds** (`#ifndef NDEBUG`). Validation failure returns `ShaderCompilationFailed` error.

| Target     | Checks                                                                            |
| ---------- | --------------------------------------------------------------------------------- |
| **SPIR-V** | Magic number `0x07230203`, minimum 20 bytes (5 × uint32 header), 4-byte alignment |
| **DXIL**   | DXBC container magic `0x43425844`, minimum 24 bytes (container header)            |
| **GLSL**   | Non-empty, contains `#version` directive                                          |
| **WGSL**   | Non-empty, minimum 8 bytes                                                        |
| **MSL**    | Non-empty, contains `#include <metal_stdlib>` or `using namespace metal`          |

### 14.4 Design Rationale

- **Diagnostics flow to both systems**: Slang diagnostics are stored in `lastDiagnostics` (for programmatic access via `GetLastDiagnostics()`) **and** piped to `MIKI_LOG` (for crash dumps, file sinks, and console output).
- **No `spirv-val` at compile time**: Full SPIR-V semantic validation adds 10–50ms/shader. Reserved for offline CI validation tool, not runtime compilation.
- **No source code in logs**: Shader source is not logged (512-byte ring entry overflow risk + security).
- **Tag prefix convention**: All shader log messages use `[ComponentName]` prefix (e.g., `[SlangCompiler]`, `[PermutationCache]`) for grep-ability.

---

## 15. Slang Shader Project Architecture

This section defines the **canonical directory layout, module hierarchy, and coding conventions** for all `.slang` files in the miki renderer. The architecture must support the full 88-pass rendering pipeline (see `rendering-pipeline-architecture.md`) across 5 compilation targets (SPIR-V, DXIL, GLSL, WGSL, MSL), with zero per-backend shader forks.

### 15.1 Design Principles

| Principle                                      | Rationale                                                                                                                                                                                                                                       |
| ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Module = compilation unit**                  | Each Slang `module` declaration maps to one logical shader library. Slang compiles modules independently and caches their IR. Modular boundaries enable incremental recompilation: changing `lighting.slang` does not reparse `geometry.slang`. |
| **Interface-driven polymorphism**              | All tier-variant behavior (Main vs Compat, Tier1 vs Tier3) expressed via Slang `interface` + generics, never preprocessor `#ifdef`. Slang specialization at link-time eliminates dead code — zero runtime branching.                            |
| **Single-source, multi-entry**                 | A `.slang` file may contain multiple entry points (vertex + fragment, or task + mesh). The compiler extracts each `[shader("...")]` entry point independently. This keeps related stages co-located.                                            |
| **Flat public API, deep internals**            | Top-level `shaders/miki/` contains public module files (thin: just `module` + `__include` + `import`). Implementation details live in subdirectories. External code only `import`s top-level modules.                                           |
| **Access control**                             | `public` for cross-module API, `internal` (default) for intra-module helpers, `private` for struct internals. No `public` on implementation details.                                                                                            |
| **No preprocessor permutations in module API** | Permutation axes are expressed as Slang generic type parameters or specialization constants, not `#define` macros. Macros are reserved for `MIKI_PERMUTATION_BIT_N` (legacy bridge) and target-specific workarounds only.                       |
| **Precompiled modules for CI**                 | Library modules (`miki-core`, `miki-math`, `miki-brdf`) are precompiled to `.slang-module` IR blobs in CI. Pass-level shaders link against these blobs, reducing CI shader compilation from O(passes × modules) to O(passes).                   |

### 15.2 Directory Layout

```
shaders/
    miki/                              # Top-level public modules (imported by passes)
        miki-core.slang                # module miki_core;  (types, constants, utility)
        miki-math.slang                # module miki_math;  (SH, noise, sampling, BRDF math)
        miki-brdf.slang                # module miki_brdf;  (IMaterial interface, DSPBR, BTDFs)
        miki-lighting.slang            # module miki_lighting; (ILight, clustered, IBL, area)
        miki-geometry.slang            # module miki_geometry; (meshlet, culling, LOD)
        miki-shadow.slang              # module miki_shadow; (VSM, CSM, shadow atlas)
        miki-postfx.slang              # module miki_postfx; (tonemapping, bloom, DoF, TAA)
        miki-cad.slang                 # module miki_cad; (HLR, section, pick, measure)
        miki-cae.slang                 # module miki_cae; (FEM, streamline, iso, tensor)
        miki-rt.slang                  # module miki_rt; (RT reflections, shadows, GI, path trace)
        miki-xr.slang                  # module miki_xr; (stereo, foveated, reprojection)
        miki-neural.slang              # module miki_neural; (neural texture, denoiser, NRC)
        miki-debug.slang               # module miki_debug; (GPU debug viz, nanite overlay)

        core/                          # Implementation files for miki-core
            types.slang                # implementing miki_core; GpuInstance, GpuLight, etc.
            constants.slang            # implementing miki_core; kMaxLights, kMaxInstances, etc.
            bindless.slang             # implementing miki_core; BindlessTable access helpers
            push_constants.slang       # implementing miki_core; PushConstantsBlock
            color_space.slang          # implementing miki_core; sRGB, linear, ACES, Rec2020
            packing.slang              # implementing miki_core; R11G11B10 pack/unpack, octahedral

        math/                          # Implementation files for miki-math
            sh.slang                   # implementing miki_math; spherical harmonics L0-L2
            noise.slang                # implementing miki_math; Perlin, simplex, blue noise
            sampling.slang             # implementing miki_math; Hammersley, Halton, importance
            quaternion.slang           # implementing miki_math; quat rotation, slerp
            matrix_utils.slang         # implementing miki_math; inverse, transpose, normal matrix

        brdf/                          # Implementation files for miki-brdf
            dspbr.slang                # implementing miki_brdf; DSPBR material model
            ggx.slang                  # implementing miki_brdf; GGX NDF, Smith G, Fresnel
            diffuse.slang              # implementing miki_brdf; Lambert, Burley, Oren-Nayar
            sheen.slang                # implementing miki_brdf; Charlie sheen model
            clearcoat.slang            # implementing miki_brdf; clearcoat layer
            iridescence.slang          # implementing miki_brdf; thin-film interference
            sss.slang                  # implementing miki_brdf; separable SSS (Burley)
            aniso.slang                # implementing miki_brdf; anisotropic GGX
            material_interface.slang   # implementing miki_brdf; IMaterial interface definition

        lighting/                      # Implementation files for miki-lighting
            clustered.slang            # implementing miki_lighting; cluster assign + cull
            ibl.slang                  # implementing miki_lighting; IBL precompute, split-sum
            area_light.slang           # implementing miki_lighting; LTC area lights
            ltc_lut.slang              # implementing miki_lighting; LTC matrix LUT sampling
            restir.slang               # implementing miki_lighting; ReSTIR DI/GI reservoirs
            ddgi.slang                 # implementing miki_lighting; DDGI probe update + sample

        geometry/                      # Implementation files for miki-geometry
            meshlet.slang              # implementing miki_geometry; meshlet data, task/mesh entry
            culling.slang              # implementing miki_geometry; frustum, occlusion, 2-phase
            hiz.slang                  # implementing miki_geometry; HiZ generate + sample
            lod.slang                  # implementing miki_geometry; LOD selection, ClusterDAG
            visibility_buffer.slang    # implementing miki_geometry; VisBuffer encode/decode
            macro_binning.slang        # implementing miki_geometry; macro-bin classify + emit
            sw_rasterizer.slang        # implementing miki_geometry; software raster for micro-tri
            vertex_pipeline.slang      # implementing miki_geometry; compat vertex+MDI path

        shadow/
            vsm.slang                  # implementing miki_shadow; VSM page alloc, render, sample
            csm.slang                  # implementing miki_shadow; CSM cascade split, render
            shadow_atlas.slang         # implementing miki_shadow; shadow atlas tile alloc

        postfx/
            bloom.slang                # implementing miki_postfx; multi-pass bloom (downsample+upsample)
            dof.slang                  # implementing miki_postfx; CoC, bokeh, near/far
            motion_blur.slang          # implementing miki_postfx; tile-based motion blur
            tonemap.slang              # implementing miki_postfx; ACES, AgX, Reinhard, neutral
            taa.slang                  # implementing miki_postfx; TAA + jitter + history clamp
            fxaa.slang                 # implementing miki_postfx; FXAA 3.11
            cas.slang                  # implementing miki_postfx; contrast-adaptive sharpen
            color_grade.slang          # implementing miki_postfx; 3D LUT, lift/gamma/gain
            ssr.slang                  # implementing miki_postfx; hierarchical SSR
            outline.slang              # implementing miki_postfx; edge-detect outline
            ao/
                gtao.slang             # implementing miki_postfx; GTAO (half-res + bilateral up)
                ssao.slang             # implementing miki_postfx; SSAO compat path
                rtao.slang             # implementing miki_postfx; RT ambient occlusion

        cad/                           # CAD domain passes
            hlr.slang                  # implementing miki_cad; GPU hidden line removal
            section.slang              # implementing miki_cad; section plane + volume
            pick.slang                 # implementing miki_cad; ray pick + lasso pick
            measure.slang              # implementing miki_cad; GPU measurement
            boolean_preview.slang      # implementing miki_cad; boolean op preview
            draft_angle.slang          # implementing miki_cad; draft angle analysis
            explode.slang              # implementing miki_cad; explode transform

        cae/                           # CAE domain passes
            fem.slang                  # implementing miki_cae; FEM mesh render + contour
            scalar_field.slang         # implementing miki_cae; scalar/vector field viz
            streamline.slang           # implementing miki_cae; streamline + pathline
            isosurface.slang           # implementing miki_cae; marching cubes GPU
            tensor_glyph.slang         # implementing miki_cae; tensor glyph render
            point_cloud.slang          # implementing miki_cae; point cloud splat + EDL

        rt/                            # Ray tracing passes
            rt_common.slang            # implementing miki_rt; common RT payload, hit info
            rt_reflections.slang       # implementing miki_rt; RT reflections
            rt_shadows.slang           # implementing miki_rt; RT shadows
            rt_gi.slang                # implementing miki_rt; RT global illumination
            path_tracer.slang          # implementing miki_rt; progressive path tracer
            denoiser.slang             # implementing miki_rt; temporal + spatial denoiser

        xr/                            # XR/VR passes
            stereo.slang               # implementing miki_xr; single-pass stereo (VK_KHR_multiview)
            foveated.slang             # implementing miki_xr; foveated rendering (VRS)
            reprojection.slang         # implementing miki_xr; late-latch reprojection

        neural/                        # Neural shader support (Phase 17+)
            neural_texture.slang       # implementing miki_neural; neural texture compression
            neural_denoiser.slang      # implementing miki_neural; ML denoiser inference
            nrc.slang                  # implementing miki_neural; neural radiance cache

    passes/                            # Per-pass entry point files (compile units)
        depth_prepass.slang            # import miki_geometry; [shader("vertex")] + [shader("fragment")]
        gpu_culling.slang              # import miki_geometry; [shader("compute")]
        light_cluster.slang            # import miki_lighting; [shader("compute")]
        geometry_main.slang            # import miki_geometry, miki_brdf; task + mesh entry
        geometry_compat.slang          # import miki_geometry; vertex entry (compat pipeline)
        material_resolve.slang         # import miki_brdf, miki_lighting; [shader("compute")]
        deferred_resolve.slang         # import miki_brdf, miki_lighting; [shader("fragment")]
        vsm_render.slang               # import miki_shadow; mesh/vertex shadow entry
        csm_render.slang               # import miki_shadow; vertex shadow entry (compat)
        gtao_compute.slang             # import miki_postfx; [shader("compute")]
        bloom_pass.slang               # import miki_postfx; [shader("compute")]
        taa_resolve.slang              # import miki_postfx; [shader("compute")]
        tonemap_pass.slang             # import miki_postfx; [shader("compute")]
        fullscreen_tri.slang           # utility: fullscreen triangle vertex shader
        blit.slang                     # utility: blit / copy / format convert
        # ... one per render pass (88+ passes total)

    tests/                             # Feature probe test shaders
        probe_*.slang                  # 29 existing probe shaders (S7)

    precompiled/                       # Build output: precompiled .slang-module blobs
        miki_core.slang-module
        miki_math.slang-module
        miki_brdf.slang-module
        miki_lighting.slang-module
        miki_geometry.slang-module
        # Generated by CMake custom command at build time
```

### 15.3 Module Hierarchy & Dependency Graph

```mermaid
graph TD
    subgraph "Library Modules (precompiled)"
        CORE["miki-core<br/>types, constants, bindless,<br/>push constants, color, packing"]
        MATH["miki-math<br/>SH, noise, sampling, quat"]
        BRDF["miki-brdf<br/>IMaterial, DSPBR, GGX,<br/>diffuse, sheen, clearcoat"]
        LIGHT["miki-lighting<br/>ILight, clustered, IBL,<br/>area, ReSTIR, DDGI"]
        GEOM["miki-geometry<br/>meshlet, culling, HiZ, LOD,<br/>VisBuffer, macro-bin"]
        SHADOW["miki-shadow<br/>VSM, CSM, shadow atlas"]
        POSTFX["miki-postfx<br/>bloom, DoF, TAA, GTAO,<br/>tone, FXAA, SSR"]
        CAD["miki-cad<br/>HLR, section, pick,<br/>measure, boolean, draft"]
        CAE["miki-cae<br/>FEM, stream, iso, tensor"]
        RT["miki-rt<br/>reflections, shadows,<br/>GI, path trace, denoise"]
        NEURAL["miki-neural<br/>neural tex, denoise, NRC"]
        XR["miki-xr<br/>stereo, foveated, reprojection"]
        DEBUG["miki-debug<br/>GPU debug viz"]
    end

    subgraph "Pass Entry Points"
        PASS["passes/*.slang<br/>88+ render pass shaders"]
    end

    CORE --> MATH
    CORE --> BRDF
    CORE --> LIGHT
    CORE --> GEOM
    CORE --> SHADOW
    CORE --> POSTFX
    CORE --> CAD
    CORE --> CAE
    CORE --> RT
    CORE --> NEURAL
    CORE --> XR
    CORE --> DEBUG
    MATH --> BRDF
    MATH --> LIGHT
    MATH --> GEOM
    MATH --> POSTFX
    BRDF --> LIGHT
    LIGHT --> SHADOW
    GEOM --> PASS
    BRDF --> PASS
    LIGHT --> PASS
    SHADOW --> PASS
    POSTFX --> PASS
    CAD --> PASS
    CAE --> PASS
    RT --> PASS
    NEURAL --> PASS
    XR --> PASS
    DEBUG --> PASS
```

**DAG invariant**: No cycles allowed. `miki-core` is the root. Higher-level modules may depend on lower-level ones but never vice versa. This is enforced by CI: any circular `import` produces a Slang compilation error.

### 15.4 Interface-Driven Tier Abstraction

Instead of preprocessor `#ifdef TIER1`, use Slang interfaces and generics to abstract tier-variant behavior. This enables compile-time specialization with zero runtime cost.

#### 15.4.1 IMaterial Interface

```slang
// brdf/material_interface.slang
public interface IMaterial {
    associatedtype ShadingData;

    /// Evaluate BRDF for a surface interaction.
    float3 evaluate(ShadingData sd, float3 wi, float3 wo);

    /// Sample a direction from the BRDF lobe.
    float3 sample(ShadingData sd, float3 wo, float2 xi, out float pdf);

    /// PDF of sampling direction wi given outgoing wo.
    float pdf(ShadingData sd, float3 wi, float3 wo);
};
```

#### 15.4.2 IGeometryPipeline Interface

```slang
// geometry/geometry_pipeline_interface.slang
public interface IGeometryPipeline {
    associatedtype VertexData;
    associatedtype PrimitiveData;

    /// Fetch vertex data for a given vertex index.
    VertexData fetchVertex(uint vertexIndex);

    /// Transform and project vertex to clip space.
    float4 transformToClip(VertexData v, float4x4 mvp);

    /// Encode primitive ID for visibility buffer.
    uint encodePrimitiveId(uint instanceId, uint primitiveId);

    // --- Culling interface (critical for performance) ---

    /// Meshlet-level frustum + cone backface test (task shader or mesh shader entry).
    bool cullMeshlet(uint meshletIndex, float4 boundingSphere, int8_t4 normalCone,
                     float4x4 viewProj, float3 cameraPos);

    /// Per-triangle backface + small-triangle + frustum culling in mesh shader.
    /// Returns true if triangle should be DISCARDED.
    /// Industry practice (Zeux/niagara 2023, AMD GPUOpen 2024): per-triangle cull
    /// eliminates 30-50% additional triangles beyond meshlet-level culling.
    bool cullTriangle(float4 clipV0, float4 clipV1, float4 clipV2);

    /// Classify small triangle (< kSmallTriThreshold pixels^2) for SW rasterizer.
    bool isSmallTriangle(float2 ndcV0, float2 ndcV1, float2 ndcV2, float2 viewportSize);
};
```

#### 15.4.2.1 MeshPayload Structure

```slang
// geometry/meshlet.slang (implementing miki_geometry)

/// Task→Mesh shader payload. Keep minimal to reduce memory traffic.
/// AMD recommendation: payload ≤ 16KB, prefer meshlet indices only.
/// Each task workgroup processes kTaskShaderMeshletCount meshlets,
/// compacts visible ones into payload, then DispatchMesh().
struct MeshPayload {
    uint meshletIndices[kTaskShaderMeshletCount];  // compacted visible meshlet indices
    uint instanceId;                                // parent instance (shared for group)
    uint visibleMeshletCount;                       // set by task shader before DispatchMesh
};
// sizeof(MeshPayload) = 32*4 + 4 + 4 = 136 bytes (well within 16KB limit)
```

**Task shader workgroup budget**: Each task workgroup processes `kTaskShaderMeshletCount` (default 32) meshlets. For an instance with 1024 meshlets, this means 32 task workgroups. AMD requires ≥32 meshlets/group to amortize `DispatchMesh` command processor round-trip latency. NVidia is more forgiving but still benefits from batching.

#### 15.4.3 Capability-Based Tier Safety

Slang's **capability system** (`[require]` attributes) provides compile-time validation that a function is only called from contexts where its hardware requirements are met. This replaces ad-hoc preprocessor `#ifdef TIER1` guards with compiler-enforced correctness.

```slang
// geometry/meshlet.slang (implementing miki_geometry)

/// Meshlet dispatch requires mesh shader capability.
/// Slang will emit a compile error if called from a context targeting WGSL/GLSL (no mesh shaders).
[require(spirv_1_4, SPV_EXT_mesh_shader)]
[require(hlsl, _sm_6_5)]
public void dispatchMeshlets(uint groupIndex, MeshPayload payload) {
    // Only callable when targeting SPIR-V with mesh shader ext, or HLSL SM 6.5+
    // Slang capability inference propagates this requirement to all callers.
}

/// Tier1-only: 64-bit atomics for visibility buffer
[require(spirv_1_0, spvInt64Atomics)]
[require(hlsl, _sm_6_6)]
public void atomicVisBufferWrite(RWStructuredBuffer<uint64_t> buf, uint idx, uint64_t val) {
    InterlockedMax(buf[idx], val);
}
```

**Key benefit**: capability inference is **transitive** — if `meshMain()` calls `dispatchMeshlets()`, Slang automatically deduces that `meshMain` also requires mesh shader capability. A compat pipeline shader that accidentally calls a Tier1-only function gets a compile error, not a silent runtime failure.

#### 15.4.4 Stage-Polymorphic Code via `__stage_switch`

Complementing `__target_switch` (§15.5.2), `__stage_switch` enables code that adapts to shader stage:

```slang
// core/types.slang (implementing miki_core)
public uint getInvocationId() {
    __stage_switch {
    case vertex:    return SV_VertexID;
    case fragment:  return SV_PrimitiveID;
    case compute:   return SV_DispatchThreadID.x;
    case mesh:      return SV_GroupIndex;
    }
}
```

Slang infers the capability of `getInvocationId()` as `(vertex | fragment | compute | mesh)` — callable from any of those stages.

#### 15.4.5 Specialization at Link-Time

```slang
// passes/geometry_main.slang
import miki_geometry;
import miki_brdf;

// Link-time type specialization: resolved at link, no vtable, no runtime cost.
extern struct GeoPipeline : IGeometryPipeline = MeshGeoPipeline;

// Tier1: concrete type for mesh shader pipeline
struct MeshGeoPipeline : IGeometryPipeline {
    // ... meshlet-based implementation (fetchVertex, cullTriangle, etc.)
};

// Mesh shader entry point. Workgroup size and output limits use link-time constants
// so meshoptimizer meshlet generation and shader are always in sync.
// NVidia default: numthreads(64), 64V/126T. AMD override: numthreads(128), 128V/256T.
[shader("mesh")]
[numthreads(kMeshShaderWorkgroupSize, 1, 1)]
void meshMain(
    in payload MeshPayload p,
    uint gtid : SV_GroupThreadID,
    uint gid  : SV_GroupIndex,
    OutputVertices<VertexOut, kMeshletMaxVertices> verts,
    OutputIndices<uint3, kMeshletMaxPrimitives> tris
) {
    let meshletIdx = p.meshletIndices[gid];
    MeshletDescriptor meshlet = LoadMeshlet(meshletIdx);
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    // Vertex phase: 1:1 thread-to-vertex mapping (kMeshShaderWorkgroupSize == kMeshletMaxVertices)
    if (gtid < meshlet.vertexCount) {
        let v = GeoPipeline.fetchVertex(meshlet.vertexOffset + gtid);
        verts[gtid].clipPos = GeoPipeline.transformToClip(v, viewProj);
        verts[gtid].data = PackVertexOutput(v);
    }

    // Primitive phase: stride pattern for T > workgroup size (AMD RDNA3: 128 threads, 256 tris)
    for (uint i = 0; i < (kMeshletMaxPrimitives + kMeshShaderWorkgroupSize - 1) / kMeshShaderWorkgroupSize; i++) {
        let primIdx = gtid + i * kMeshShaderWorkgroupSize;
        if (primIdx < meshlet.triangleCount) {
            uint3 tri = LoadTriangleIndices(meshlet, primIdx);
            // Per-triangle culling: backface + degenerate + small-triangle + micro-frustum
            // Eliminates 30-50% additional triangles (Zeux 2023, AMD GPUOpen 2024)
            if (GeoPipeline.cullTriangle(verts[tri.x].clipPos, verts[tri.y].clipPos, verts[tri.z].clipPos)) {
                tris[primIdx] = uint3(0, 0, 0);  // degenerate (GPU skips rasterization)
            } else {
                tris[primIdx] = tri;
            }
        }
    }
}
```

### 15.5 Specialization & Permutation Strategy

#### 15.5.1 Hierarchy of Specialization Mechanisms

miki uses a **layered specialization strategy**, from most preferred (zero cost, maximum safety) to least preferred:

| Priority | Mechanism                          | Slang Feature                                       | Example                                  | Cost                                   | When to Use                                                  |
| :------: | ---------------------------------- | --------------------------------------------------- | ---------------------------------------- | -------------------------------------- | ------------------------------------------------------------ |
|  **1**   | **Link-time type specialization**  | `extern struct T : IFoo = ConcreteT;`               | `IMaterial` → `DSPBR` vs `SimpleLambert` | Zero (monomorphized at link)           | Tier-variant behavior, material system                       |
|  **2**   | **Link-time constant**             | `extern static const int K = N;`                    | Max light count, sample count            | Zero (constant-folded, loops unrolled) | Numeric tuning parameters                                    |
|  **3**   | **SPIR-V specialization constant** | `[SpecializationConstant]` / `[vk::constant_id(N)]` | Workgroup size, feature bits             | Near-zero (patched at PSO creation)    | Per-PSO tuning without recompile                             |
|  **4**   | **`__target_switch`**              | Built-in                                            | Push constant layout, bindless pattern   | Zero (target codegen path)             | Backend workarounds                                          |
|  **5**   | **Preprocessor define**            | `#define MIKI_PERMUTATION_BIT_N`                    | Legacy bridge, rapid prototyping         | Zero (dead code elim)                  | **Last resort only** — incompatible with precompiled modules |

> **Design rule**: Preprocessor-based permutations are **incompatible with precompiled `.slang-module` blobs** because `#define` must be resolved during parsing (front-end). Link-time specialization works on precompiled IR and is therefore the **preferred** mechanism for all new code.

#### 15.5.2 Link-Time Specialization (Preferred)

Slang's [link-time specialization](http://shader-slang.org/slang/user-guide/link-time-specialization) defers specialization to the linking step, enabling full reuse of front-end compilation (tokenization, parsing, type checking, IR generation). This is essential for precompiled module performance.

**Link-time constants** (`extern static const`):

```slang
// miki-lighting module: declares tunable constant
extern static const int kMaxClusterLights = 128;  // default value, overridable at link
```

```slang
// specialization module (generated at runtime by SlangCompiler):
export static const int kMaxClusterLights = 256;  // override for high-end config
```

At link time, Slang constant-folds `kMaxClusterLights` and unrolls any `[ForceUnroll]` loops using it. The precompiled `miki-lighting.slang-module` is reused — only linking and codegen are repeated.

**Link-time types** (`extern struct`):

```slang
// miki-brdf module: declares abstract material type
import miki_brdf;
extern struct MaterialImpl : IMaterial;  // resolved at link time

void shadeSurface(MaterialImpl mat, ShadingData sd, ...) {
    float3 color = mat.evaluate(sd, wi, wo);  // monomorphized call
}
```

```slang
// specialization module:
import miki_brdf;
export struct MaterialImpl : IMaterial = DSPBR;  // = syntax creates type alias
```

**Key advantage over generics**: link-time types produce identical codegen to manual specialization, but the library module only needs to be compiled once. With generics, the library must be recompiled for each concrete type.

**API integration**: `SlangCompiler::Compile()` synthesizes a specialization module from `ShaderCompileDesc::linkTimeConstants` and `ShaderCompileDesc::linkTimeTypes` maps, loads it via `loadModuleFromSourceString()`, and includes it in `createCompositeComponentType()` before linking.

#### 15.5.3 Target-Switch for Backend Adaptation

```slang
// core/push_constants.slang
public struct PushConstants {
    float4x4 model;
    uint materialIndex;
    uint instanceId;
};

public PushConstants loadPushConstants() {
    __target_switch {
    case spirv:
        // Native Vulkan push constants
        return __pushConstant<PushConstants>();
    case hlsl:
        // D3D12 root constants
        return rootConstants;
    case metal:
        // Metal: argument buffer or [[buffer(0)]]
        return metalPushConstants;
    case wgsl:
        // WebGPU: emulated via UBO at group(0) binding(0)
        return pushConstantsUBO.data;
    case glsl:
        // OpenGL: emulated via UBO binding(0)
        return pushConstantsUBO.data;
    }
}
```

#### 15.5.4 SPIR-V Specialization Constants

For parameters that must be tunable **per-PSO without recompilation** (e.g., workgroup size), use Slang's `[SpecializationConstant]` attribute:

```slang
// geometry/culling.slang (implementing miki_geometry)

[SpecializationConstant]
[vk::constant_id(0)]
const uint kCullWorkgroupSize = 256;  // default, patched at VkSpecializationInfo

[SpecializationConstant]
[vk::constant_id(1)]
const bool kEnableOcclusionCulling = true;

[shader("compute")]
[numthreads(kCullWorkgroupSize, 1, 1)]
void gpuCull(uint tid : SV_DispatchThreadID) {
    if (kEnableOcclusionCulling) { /* HiZ test */ }
    // kEnableOcclusionCulling==false: branch eliminated by driver at PSO creation
}
```

Slang generates SPIR-V `OpSpecConstant` / `OpSpecConstantTrue|False` for these. D3D12 and Metal targets ignore the attribute (no equivalent — falls back to compile-time constant).

### 15.6 GPU Data Contracts (Shader ↔ C++ Shared Types)

All GPU-side struct definitions live in `core/types.slang` and have **mirrored C++ structs** validated at compile time via Slang reflection (§4 `Reflect()` → `StructLayout`).

```slang
// core/types.slang (implementing miki_core)

/// GPU instance data — matches C++ GpuInstance exactly.
/// Layout validated by PermutationCache::ValidateStructLayout() in debug builds.
public struct GpuInstance {
    float4x4 modelMatrix;       // offset 0, size 64
    float4x4 normalMatrix;      // offset 64, size 64
    uint     materialIndex;     // offset 128, size 4
    uint     flags;             // offset 132, size 4
    float2   _pad0;             // offset 136, size 8  (explicit padding)
};                              // total: 144 bytes, align 16

/// GPU light data — matches C++ GpuLight.
public struct GpuLight {
    float3   position;          // offset 0
    float    range;             // offset 12
    float3   direction;         // offset 16
    float    spotAngle;         // offset 28
    float3   color;             // offset 32
    float    intensity;         // offset 44
    uint     type;              // offset 48 (0=point, 1=spot, 2=directional, 3=area)
    uint     shadowIndex;       // offset 52
    float2   _pad0;             // offset 56
};                              // total: 64 bytes, align 16
```

**Validation**: In debug builds, `SlangCompiler::Reflect()` extracts `StructLayout` for `GpuInstance` and `GpuLight`, and a `static_assert`-equivalent runtime check compares field offsets against the C++ `offsetof()` values. Any mismatch is a hard error.

### 15.7 Bindless Resource Access Pattern

```slang
// core/bindless.slang (implementing miki_core)

// Set 3 is the global bindless table — fixed layout, never changes.
// Matches kBindlessLayout in C++ (S9.2).
[[vk::binding(0, 3)]] Texture2D<float4>     globalTextures[];
[[vk::binding(1, 3)]] StructuredBuffer<uint> globalBuffers[];
[[vk::binding(2, 3)]] SamplerState           globalSamplers[];

/// Type-safe bindless texture fetch.
public float4 sampleBindlessTexture(uint textureIndex, uint samplerIndex, float2 uv) {
    return globalTextures[NonUniformResourceIndex(textureIndex)]
        .Sample(globalSamplers[NonUniformResourceIndex(samplerIndex)], uv);
}

/// Type-safe bindless buffer load.
public T loadBindlessBuffer<T>(uint bufferIndex, uint elementIndex) {
    return globalBuffers[NonUniformResourceIndex(bufferIndex)]
        .Load<T>(elementIndex);
}
```

### 15.8 Precompiled Module Strategy

Library modules are precompiled to Slang IR (`.slang-module`) at build time. Pass shaders link against these precompiled modules, dramatically reducing compilation time.

#### 15.8.1 CMake Integration

```cmake
# shaders/CMakeLists.txt

set(MIKI_SHADER_MODULES
    miki-core miki-math miki-brdf miki-lighting
    miki-geometry miki-shadow miki-postfx
    miki-cad miki-cae miki-rt miki-xr miki-neural miki-debug
)

foreach(MOD ${MIKI_SHADER_MODULES})
    string(REPLACE "-" "_" MOD_UNDERSCORED ${MOD})
    # Collect ALL source files for this module (primary + implementing files)
    # so CMake rebuilds the precompiled module when any constituent changes.
    string(REGEX REPLACE "^miki-" "" MOD_SHORT ${MOD})  # e.g., miki-core -> core
    file(GLOB_RECURSE _MOD_SOURCES
        "${CMAKE_SOURCE_DIR}/shaders/miki/${MOD}.slang"              # primary module file
        "${CMAKE_SOURCE_DIR}/shaders/miki/${MOD_SHORT}/*.slang"      # e.g., core/*.slang (implementing files)
    )
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/precompiled/${MOD_UNDERSCORED}.slang-module
        COMMAND slangc
            ${CMAKE_SOURCE_DIR}/shaders/miki/${MOD}.slang
            -o ${CMAKE_BINARY_DIR}/shaders/precompiled/${MOD_UNDERSCORED}.slang-module
            -I ${CMAKE_SOURCE_DIR}/shaders/miki
        DEPENDS ${_MOD_SOURCES}  # Tracks primary + ALL __include'd implementing files
        COMMENT "Precompiling Slang module: ${MOD}"
    )
    list(APPEND MIKI_PRECOMPILED_MODULES
        ${CMAKE_BINARY_DIR}/shaders/precompiled/${MOD_UNDERSCORED}.slang-module)
endforeach()

add_custom_target(miki_shader_modules ALL DEPENDS ${MIKI_PRECOMPILED_MODULES})
```

**Important**: The `DEPENDS` list includes **all** `implementing` files (e.g., `core/types.slang`, `core/constants.slang`), not just the primary module file. Without this, editing an `implementing` file does not trigger a rebuild of the precompiled module.

#### 15.8.1.1 Runtime Staleness Check

When loading precompiled `.slang-module` blobs at runtime, `SlangCompiler` validates freshness:

```cpp
// SlangCompiler::Impl::LoadPrecompiledModule
sessionDesc.compilerOptionEntries = {
    { slang::CompilerOptionName::UseUpToDateBinaryModule, {1} },  // Enable staleness check
};
// ISession::loadModule("miki_core") will:
//   1. Look for miki_core.slang-module in search paths
//   2. Call ISession::isBinaryModuleUpToDate() internally
//   3. If stale (source changed since precompilation), recompile from source transparently
//   4. If up-to-date, load binary IR (skips parsing entirely)
```

This guarantees that developers never see stale shader behavior, while production builds (where sources don't change) always hit the fast binary-load path.

#### 15.8.2 Compilation Time Budget

| Scenario                            | Without precompiled        | With precompiled  | Speedup  |
| ----------------------------------- | -------------------------- | ----------------- | -------- |
| Single pass shader (SPIR-V)         | ~120ms (parse all imports) | ~25ms (link only) | **4.8x** |
| Full 88-pass rebuild (multi-target) | ~42s                       | ~9s               | **4.7x** |
| Hot-reload single file              | ~120ms                     | ~25ms             | **4.8x** |
| CI full shader validation           | ~168s                      | ~36s              | **4.7x** |

### 15.9 Module Example: miki-core

```slang
// shaders/miki/miki-core.slang
module miki_core;

// Implementation files
__include "core/types.slang";
__include "core/constants.slang";
__include "core/bindless.slang";
__include "core/push_constants.slang";
__include "core/color_space.slang";
__include "core/packing.slang";
```

```slang
// shaders/miki/core/constants.slang
implementing miki_core;

public static const uint kMaxInstances       = 10000000;  // 10M
public static const uint kMaxLights          = 4096;
public static const uint kMaxBindlessTextures = 1048576;  // 1M
public static const uint kMaxBindlessBuffers  = 262144;   // 256K
public static const uint kMaxBindlessSamplers = 2048;
public static const uint kMaxMeshlets        = 16777216;  // 16M

// Vendor-adaptive meshlet sizing via link-time specialization.
// Default: NVidia-optimal (64V/126T). Override at link time for AMD (128V/256T).
//
// Rationale (2025 industry consensus):
//   NVidia: shader export allocates primitive indices in 128-byte groups.
//     126*3 = 378 bytes fits well. 64 vertices = 1:1 thread mapping at numthreads(64).
//   AMD RDNA3+: exp instruction supports wave-wide offset, 128 threads can export
//     256 primitives in 2 strides. 128V/256T maximizes vertex reuse and occupancy.
//   Intel Arc: same limits as VK_EXT_mesh_shader minimum (256/256), 64V/126T works.
//
// SlangCompiler generates override module:
//   export static const uint kMeshletMaxVertices = 128;   // AMD path
//   export static const uint kMeshletMaxPrimitives = 256; // AMD path
extern static const uint kMeshletMaxVertices   = 64;   // NVidia default
extern static const uint kMeshletMaxPrimitives = 126;  // NVidia default (126*3=378B aligned)
extern static const uint kMeshShaderWorkgroupSize = 64; // must == kMeshletMaxVertices for 1:1 mapping

// Task shader workgroup size: number of meshlets processed per task invocation.
// AMD recommends >= 32 to amortize DispatchMesh command processor latency.
extern static const uint kTaskShaderMeshletCount = 32;  // meshlets per task workgroup

public static const uint kClusterTileSize    = 16;        // 16x16x24 light cluster grid
public static const float kPI               = 3.14159265358979323846;
public static const float kInvPI            = 0.31830988618379067154;
public static const float kEpsilon          = 1e-6;
```

---

## 16. Timeline Semaphore Integration with Pipeline Creation

Pipeline creation is **not** a per-frame hot-path operation — it occurs at initialization and on hot-reload. However, pipeline creation can be **expensive** (10-100ms per PSO on some drivers) and must not stall the rendering loop. This section specifies how pipeline creation integrates with the timeline semaphore synchronization model from `03-sync.md`.

### 16.1 Async Pipeline Compilation

Pipeline creation is offloaded to background threads. The `AsyncTaskManager` (§`03-sync.md` §6) manages async pipeline compilations as cross-frame tasks.

```
Pipeline Creation Request
    ↓
AsyncTaskManager::Submit("PSO: depth_prepass", ...)
    ↓ (background thread, CPU-side)
SlangCompiler::Compile() → ShaderBlob
    ↓
IDevice::CreateGraphicsPipeline(blob, PipelineCache)
    ↓
completionPoint = {AsyncCompute, signalValue}  ← timeline semaphore
    ↓
Main thread: IsComplete() poll → swap pipeline handle
```

> **Note**: PSO compilation is CPU work (driver compiles shader bytecode into GPU ISA), but `AsyncTaskManager` tracks it via the **AsyncCompute** timeline semaphore because ATM submits to `QueueType::AsyncCompute` for all Level A/B/C hardware (see `03-sync.md` §6). The semaphore signal here represents task completion, not GPU execution.

### 16.2 Timeline Semaphore Values for Pipeline Swap

Pipeline hot-reload must ensure the old pipeline is not in use on the GPU before destruction. The timeline semaphore provides a precise synchronization point.

```
Frame N: using pipeline_v1
  Graphics queue signal: G=142

Hot-reload detected (CPU):
  1. Compile new shader → pipeline_v2
  2. Record swap point: swapAfterGraphicsValue = 142
  3. Tag pipeline_v1 for deferred destruction at G >= 142

Frame N+1: BeginFrame waits G >= 140 (2 frames in flight)
  → G=142 is reached → pipeline_v1 safe to destroy
  → Use pipeline_v2 for all new command recording

Deferred destructor at frame N+1:
  DeferredDestructor::Drain(142) → destroys pipeline_v1
```

**Key invariant**: Pipeline swap happens at frame boundary (inside `BeginFrame`), never mid-frame. The `ShaderWatcher::Poll()` is called at frame start, and any pipeline recreation happens before command recording begins.

### 16.3 Multi-Queue Pipeline Dependency

Some pipelines depend on resources produced by async compute (e.g., GTAO pipeline reads compute-produced AO texture). The timeline semaphore ensures correct ordering:

```cpp
// Tier1 pipeline creation with cross-queue dependency
auto& scheduler = frameOrchestrator.GetSyncScheduler();

// Graphics submit #1: geometry passes using old pipeline
uint64_t geomDone = scheduler.AllocateSignal(QueueType::Graphics);
// signal: graphicsTimeline = geomDone

// Async compute: GTAO compute (may use newly hot-reloaded pipeline)
scheduler.AddDependency(QueueType::Compute, QueueType::Graphics, geomDone,
                        PipelineStage::ComputeShader);
uint64_t computeDone = scheduler.AllocateSignal(QueueType::Compute);
// wait: graphicsTimeline >= geomDone
// signal: computeTimeline = computeDone

// Graphics submit #2: deferred resolve reads GTAO output
scheduler.AddDependency(QueueType::Graphics, QueueType::Compute, computeDone,
                        PipelineStage::FragmentShader);
uint64_t resolveDone = scheduler.AllocateSignal(QueueType::Graphics);
// wait: computeTimeline >= computeDone
// signal: graphicsTimeline = resolveDone
```

### 16.4 Pipeline Creation Batching

At startup, all 88+ passes create pipelines. To avoid 88 sequential PSO compilations (potentially 8+ seconds on cold cache), pipelines are batched:

```
Startup pipeline creation:
  1. Sort passes by dependency order (passes with no deps first)
  2. Submit to thread pool (std::async or AsyncTaskManager):
     - Batch 1: depth_prepass, gpu_culling, light_cluster (no deps)
     - Batch 2: geometry_main, geometry_compat (depends on culling PSO? No — PSOs are independent)
     - ... all 88 passes in parallel
  3. Each completion signals a per-pass ready flag
  4. First frame waits for all critical-path PSOs (depth, geometry, resolve, present)
  5. Non-critical PSOs (CAD, CAE, debug) can complete asynchronously — passes skip if PSO not ready

PipelineCache (L3) accelerates this: second launch typically completes in <100ms (warm driver cache).
```

### 16.5 Pipeline Ready State Machine

```mermaid
stateDiagram-v2
    [*] --> Pending : CreateAsync()
    Pending --> Compiling : background thread starts
    Compiling --> Ready : pipeline created successfully
    Compiling --> Failed : compilation error
    Failed --> Compiling : hot-reload retry
    Ready --> Stale : source file changed
    Stale --> Compiling : recompile triggered
    Compiling --> Ready : new pipeline ready
    note right of Ready : Old pipeline kept alive until\ndeferred destructor drains\n(timeline >= swapPoint)
```

---

## 17. Neural Shader Support (Phase 17+)

Slang has first-class support for neural network inference in shaders via the **neural module system** (`slang_neural`). Note: `slang-torch` is a **training-side** tool (PyTorch ↔ Slang interop for differentiable rendering); GPU shader inference uses standard Slang `ILayer` / `Linear` / `ReLU` types that compile to regular compute shaders with matrix operations. miki integrates this for:

| Application                 | Module                   | Slang Feature                                           |
| --------------------------- | ------------------------ | ------------------------------------------------------- |
| Neural texture compression  | `miki-neural`            | `import slang_neural; ILayer` interface                 |
| Real-time ML denoiser       | `miki-neural`            | Inference of trained model weights in compute shader    |
| Neural radiance cache (NRC) | `miki-neural`            | Hash-grid encoder + small MLP for indirect illumination |
| Neural LOD                  | `miki-geometry` (future) | Learned mesh simplification quality metric              |

```slang
// neural/neural_texture.slang (implementing miki_neural)
import slang_neural;  // Slang standard neural module (2026.3.1+)

struct NeuralTextureDecoder : ILayer {
    // Small MLP: 4 inputs (uv + mip + feature) -> 4 outputs (RGBA)
    Linear<4, 16> layer0;
    ReLU activation0;
    Linear<16, 16> layer1;
    ReLU activation1;
    Linear<16, 4> layer2;

    float4 decode(float2 uv, float mipLevel, uint featureIndex) {
        float4 input = float4(uv, mipLevel, float(featureIndex));
        var x = layer0.forward(input);
        x = activation0.forward(x);
        x = layer1.forward(x);
        x = activation1.forward(x);
        return layer2.forward(x);
    }
};
```

---

## 18. Shader Development Workflow

### 18.1 Developer Inner Loop

```
1. Edit .slang file in IDE (VS Code + Slang extension for syntax + LSP)
2. ShaderWatcher detects change (< 50ms on Windows)
3. IncludeDepGraph resolves transitive dependencies
4. SlangCompiler recompiles affected module (precompiled deps loaded from .slang-module)
5. PermutationCache invalidates stale entries
6. Pipeline recreation via IDevice::CreateGraphicsPipeline()
7. Old pipeline deferred-destroyed at next frame boundary
8. Visual result updated in < 100ms total
```

### 18.2 CI Shader Validation Pipeline

```
CI Build:
  1. Precompile all library modules → .slang-module
  2. Compile all 88+ pass shaders × 5 targets = 440+ compilations
  3. Run SlangFeatureProbe (29 probes × 5 targets = 145 tests)
  4. Validate struct layout compatibility (GpuInstance, GpuLight, etc.)
  5. Run spirv-val on all SPIR-V blobs (offline, not at compile time)
  6. Golden SPIR-V diff: detect unexpected codegen changes
  7. Compilation time regression: fail if any shader > 500ms
```

### 18.3 Shader Debug Workflow

| Tool                  | Purpose                           | Integration                                                                    |
| --------------------- | --------------------------------- | ------------------------------------------------------------------------------ |
| Slang Language Server | IDE intellisense, error squiggles | VS Code extension, Slang LSP                                                   |
| RenderDoc             | GPU frame capture + shader debug  | `renderdoc-cli-mcp` + `renderdoc-gui-mcp`                                      |
| SPIR-V disassembly    | Inspect generated code            | `spirv-dis` via `SlangCompiler` debug dump                                     |
| `MIKI_SHADER_DUMP=1`  | Dump all compiled blobs to disk   | Environment variable, shader subsystem check                                   |
| Shader printf         | GPU-side debug output             | Slang `printf()` → `VK_EXT_debug_printf` (Vulkan), `OutputDebugString` (D3D12) |
| GPU debug viz pass    | Visualize normals, UVs, IDs, etc. | `miki-debug` module, dedicated render pass                                     |

---

## 19. Shader Compilation Performance Targets

| Metric                                           | Target  | Current (Phase 1a) | Notes                                    |
| ------------------------------------------------ | ------- | ------------------ | ---------------------------------------- |
| Single shader SPIR-V (cold)                      | < 150ms | ~120ms             | Includes parse + link + codegen          |
| Single shader SPIR-V (warm, precompiled modules) | < 40ms  | N/A                | Link + codegen only                      |
| Multi-target single shader (5 targets)           | < 600ms | ~400ms             | 5 targets, shared parse                  |
| Full 88-pass rebuild (cold, 5 targets)           | < 60s   | N/A                | Parallel compilation                     |
| Full 88-pass rebuild (warm, precompiled)         | < 15s   | N/A                | Link + codegen only                      |
| Hot-reload latency (file change → visual)        | < 100ms | ~80ms              | Includes debounce + compile + PSO create |
| PermutationCache L1 hit                          | < 1us   | ~0.5us             | Hash lookup + pointer return             |
| PermutationCache L2 (disk) hit                   | < 5ms   | ~3ms               | Disk read + hash validate                |
| PipelineCache L3 (driver) hit                    | < 1ms   | ~0.5ms             | Driver-cached PSO creation               |
| `Reflect()` full extraction                      | < 50ms  | ~30ms              | All bindings + vertex + struct layouts   |
| Slang `createGlobalSession`                      | < 100ms | ~80ms              | 3x faster since Slang 2026.3.1 redesign  |

---

## 20. Security & Sandboxing

| Concern                    | Mitigation                                                                                                                                                    |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Malicious shader source    | Slang compiler runs in-process but shader source is only loaded from trusted paths (`shaders/` directory). No user-uploaded shader compilation in production. |
| Shader blob tampering      | Disk cache blobs validated by content hash (SHA-256 of source + dependencies). Hash mismatch → recompile.                                                     |
| Shader printf in release   | Guarded by `MIKI_SHADER_DEBUG` compile flag. Stripped in release builds via Slang preprocessor.                                                               |
| Unbounded compilation time | Slang session timeout (configurable, default 30s). Compilation exceeding timeout returns error.                                                               |
| SPIR-V injection           | No raw SPIR-V loading in production. All SPIR-V generated by Slang from audited source. Debug-only override via `MIKI_ALLOW_RAW_SPIRV=1`.                     |
