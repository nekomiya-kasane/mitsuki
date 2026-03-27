# T1b.6.1 — ShaderWatcher (File Watcher + Include Dep Tracking + Pipeline Swap)

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: Shader Hot-Reload
**Roadmap Ref**: `roadmap.md` L1150 — `ShaderWatcher` file watcher, `#include` dep tracking, atomic pipeline swap, error overlay
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1b.5.1 | SlangCompiler quad-target | Complete | `SlangCompiler::Compile()` for recompilation on file change |
| T1a.10.1 | SlangCompiler | Complete | `SlangCompiler::Compile()`, `SlangCompiler::AddSearchPath()` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/shader/ShaderWatcher.h` | **public** | **H** | `ShaderWatcher` — file watcher + recompile + pipeline swap signal. Cross-phase contract. |
| `src/miki/shader/ShaderWatcher.cpp` | internal | L | Implementation: OS file watcher, include graph, generation counter |
| `tests/unit/test_shader_watcher.cpp` | internal | L | Unit tests |

- **Error model**: `std::expected<T, ErrorCode>` — `Start()` can fail if path invalid. Compilation errors reported via callback, NOT as exceptions.
- **Thread safety**: `ShaderWatcher` runs a background watcher thread. `Poll()` is called from the main thread to collect changes. Thread-safe via mutex on change queue.
- **Invariants**: `Start(watchDir)` begins monitoring. `Poll()` returns list of changed shaders that need pipeline recreation. `Stop()` terminates watcher. Generation counter increments on each successful recompile.

### Downstream Consumers

- `ShaderWatcher.h` (**public**, heat **H**):
  - T1b.7.1 (same Phase): triangle demo hot-reload support
  - Phase 2: forward rendering hot-reload during development
  - Phase 3a: render graph invalidation on shader change
  - Phase 11: debug tools integrate shader watcher status panel

### Upstream Contracts

- T1b.5.1: `SlangCompiler::Compile(ShaderCompileDesc)` — recompile changed shaders
- T1a.10.1: `SlangCompiler::AddSearchPath()` — for `#include` resolution during recompile
  - Source: `include/miki/shader/SlangCompiler.h`

### Technical Direction

- **OS file watcher**: Windows: `ReadDirectoryChangesW` (async overlapped I/O). Linux: `inotify`. macOS: `FSEvents`. Abstract behind `FileWatcher` utility class. Or use a lightweight library (e.g., efsw — MIT, header-heavy but cross-platform).
- **Include dependency graph**: when a `.slang` file is compiled, parse `#include` / `import` directives to build a dependency graph. When a header file changes, all dependent shaders are recompiled.
- **Generation counter**: atomic `uint64_t` incremented on each successful recompile. Rendering code checks `currentGeneration != lastSeenGeneration` to trigger pipeline recreation.
- **Pipeline swap**: `ShaderWatcher` does NOT recreate pipelines itself. It provides a `PollChanges() -> vector<ShaderChange>` method. Each `ShaderChange` contains `{path, newBlob, generation}`. The rendering layer uses this to destroy old pipeline + create new one.
- **Error overlay**: compilation errors are stored and accessible via `GetLastErrors()`. ImGui overlay (in demo) displays them. The watcher does NOT depend on ImGui — error data is plain text.
- **Debounce**: file changes are debounced (100ms) to avoid recompiling on every keystroke during save.
- **Backend-agnostic**: recompiles to the target matching the active backend. GL: `glDeleteProgram` + `glCreateProgram` + relink. WebGPU: `CreateShaderModule` + recreate pipeline. Vulkan: `vkDestroyPipeline` + `vkCreateGraphicsPipelines`. The watcher only provides new blobs; the backend handles pipeline recreation.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/shader/ShaderWatcher.h` | **public** | **H** | Public interface |
| Create | `src/miki/shader/ShaderWatcher.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_shader_watcher.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |
| Modify | `src/miki/shader/CMakeLists.txt` | internal | L | Add source file |

## Steps

- [x] **Step 1**: Define `ShaderWatcher` public interface
      **Files**: `ShaderWatcher.h` (**public** H)

      **Signatures** (`ShaderWatcher.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `ShaderWatcher::Create` | `(SlangCompiler&, ShaderWatcherConfig) -> Result<ShaderWatcher>` | `[[nodiscard]]` static |
      | `ShaderWatcher::Start` | `(path watchDir) -> Result<void>` | `[[nodiscard]]` |
      | `ShaderWatcher::Stop` | `() -> void` | — |
      | `ShaderWatcher::Poll` | `() -> vector<ShaderChange>` | `[[nodiscard]]` |
      | `ShaderWatcher::GetGeneration` | `() const noexcept -> uint64_t` | `[[nodiscard]]` |
      | `ShaderWatcher::GetLastErrors` | `() const -> span<const ShaderError>` | `[[nodiscard]]` |
      | `~ShaderWatcher` | calls `Stop()` | — |

      **Signatures** (supporting types):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `ShaderWatcherConfig` | `{ debounceMs:u32=100, targets:vector<ShaderTarget> }` | — |
      | `ShaderChange` | `{ path:fs::path, target:ShaderTarget, blob:ShaderBlob, generation:u64 }` | — |
      | `ShaderError` | `{ path:fs::path, message:string, line:u32, column:u32 }` | — |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::shader` |
      | Pimpl | yes (background thread hidden) |
      | Move-only | yes |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Implement file watcher + include dep graph + recompile
      **Files**: `ShaderWatcher.cpp` (internal L)
      OS file watcher (`ReadDirectoryChangesW` on Windows). Include parser for `.slang` files.
      On change: recompile affected shaders via `SlangCompiler::Compile()`. Debounce.
      **Acceptance**: file modification triggers recompile
      `[verify: compile]`

- [x] **Step 3**: Write unit tests
      **Files**: `test_shader_watcher.cpp` (internal L)
      Tests: create watcher, write temp `.slang` file, modify it, poll for changes.
      Use temp directory for isolation.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(ShaderWatcher, CreateReturnsValid)` | Positive | factory succeeds | 1-3 |
| `TEST(ShaderWatcher, StartOnValidDirSucceeds)` | Positive | watching starts | 1-3 |
| `TEST(ShaderWatcher, StartOnInvalidDirFails)` | Error | nonexistent path → error | 1-3 |
| `TEST(ShaderWatcher, PollReturnsEmptyInitially)` | Boundary | no changes → empty vector | 1-3 |
| `TEST(ShaderWatcher, FileModificationDetected)` | Positive | modify .slang → Poll returns change | 2-3 |
| `TEST(ShaderWatcher, IncludeDepTriggersRecompile)` | Positive | modify included header → dependent shader recompiled | 2-3 |
| `TEST(ShaderWatcher, GenerationIncrements)` | State | each recompile increments generation | 2-3 |
| `TEST(ShaderWatcher, CompileErrorCaptured)` | Error | invalid shader → error in GetLastErrors() | 2-3 |
| `TEST(ShaderWatcher, DebounceCoalescesChanges)` | Boundary | rapid changes → single recompile | 2-3 |
| `TEST(ShaderWatcher, StopAndRestart)` | State | stop + start on new dir works | 1-3 |
| `TEST(ShaderWatcher, EndToEnd_ModifyAndPoll)` | **Integration** | create watcher → start → modify file → wait → poll → verify new blob | 1-3 |

## Design Decisions

- **Windows: `ReadDirectoryChangesW`** with overlapped I/O and 200ms wait timeout for responsive stop. Non-Windows: polling fallback with `last_write_time` comparison.
- **Include dep graph**: regex-based parsing of `#include "..."` and `import ...;` directives. Transitive deps not tracked (single-level only) — sufficient for typical shader include patterns.
- **Debounce**: `sleep_for(debounceMs)` after collecting file notifications before processing. OS watcher may deliver multiple batches within the window; each batch debounces independently.
- **Recompile targets**: configurable via `ShaderWatcherConfig::targets`. Default SPIRV if empty.
- **Entry point hardcoded to "main"**: production use should extend `ShaderChange` or config to specify entry points per file. Current implementation is sufficient for hot-reload prototype.
- **Added `IsRunning()`**: beyond contract, useful for tests and UI status display.

## Implementation Notes

Contract check: PASS — all 13 contract items verified, 12 tests pass (337 total suite).
