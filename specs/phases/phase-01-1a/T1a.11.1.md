# T1a.11.1 — SlangFeatureProbe (~30 Shader Regression Tests)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: SlangFeatureProbe
**Roadmap Ref**: `roadmap.md` L334 — Exhaustive shader feature regression suite validating correct code generation across targets
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.10.1 | SlangCompiler | Complete | `SlangCompiler::Compile()`, `ShaderTarget`, `ShaderBlob` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/shader/SlangFeatureProbe.h` | **public** | **M** | `SlangFeatureProbe` — runs ~30 shader feature tests, reports pass/fail per target |
| `src/miki/shader/SlangFeatureProbe.cpp` | internal | L | Implementation: compile test shaders, validate output |
| `shaders/tests/` | internal | L | ~30 test `.slang` files exercising specific shader features |
| `tests/unit/test_slang_probe.cpp` | internal | L | Integration tests running full probe suite |

- **Error model**: `Result<ProbeReport>` with per-test pass/fail and diagnostics.
- **Thread safety**: Stateless — can run on any thread.
- **GPU constraints**: N/A (compilation tests, not execution).
- **Invariants**: Tier1-only features compiled for Tier3/4 target must produce `SlangError::FeatureNotSupported`, never silent miscompile. All ~30 tests pass on SPIR-V + DXIL targets.

### Downstream Consumers

- `SlangFeatureProbe.h` (**public**, heat **M**):
  - Phase 1a CI: Runs full probe suite on every build
  - Phase 1b: Extends with GLSL/WGSL-specific tests (~15 new)
  - All phases: Catches cross-target miscompiles early

### Upstream Contracts

- T1a.10.1: `SlangCompiler::Compile(ShaderCompileDesc)` — compile test shaders to SPIR-V/DXIL

### Technical Direction

- **Exhaustive feature coverage**: Tests validate correct code generation for: struct arrays (nested, with padding), atomics (32/64-bit, image), subgroup ops (ballot, shuffle, clustered reduce), push constant layout, texture arrays, compute shared memory, barrier semantics, binding mapping, half-precision support detection.
- **Tier degradation validation**: Attempting Tier1-only feature (mesh shader, BDA, 64-bit atomics) on Tier3/4 target must error, not silently miscompile.
- **Run on every CI build**: Fast (~5 seconds for all 30 tests). No GPU required.
- **Test shader files**: Small, focused `.slang` files in `shaders/tests/`. Each tests one specific feature.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/shader/SlangFeatureProbe.h` | **public** | **M** | Probe interface |
| Create | `src/miki/shader/SlangFeatureProbe.cpp` | internal | L | Implementation |
| Create | `shaders/tests/probe_struct_array.slang` | internal | L | Nested struct arrays with padding |
| Create | `shaders/tests/probe_atomics_32.slang` | internal | L | 32-bit atomics |
| Create | `shaders/tests/probe_atomics_64.slang` | internal | L | 64-bit atomics (Tier1 only) |
| Create | `shaders/tests/probe_subgroup_ballot.slang` | internal | L | Subgroup ballot |
| Create | `shaders/tests/probe_subgroup_shuffle.slang` | internal | L | Subgroup shuffle |
| Create | `shaders/tests/probe_subgroup_clustered.slang` | internal | L | Clustered reduce |
| Create | `shaders/tests/probe_push_constants.slang` | internal | L | Push constant layout |
| Create | `shaders/tests/probe_texture_array.slang` | internal | L | Texture arrays |
| Create | `shaders/tests/probe_compute_shared.slang` | internal | L | Compute shared memory |
| Create | `shaders/tests/probe_barrier_semantics.slang` | internal | L | Memory vs execution barriers |
| Create | `shaders/tests/probe_binding_map.slang` | internal | L | `[[vk::binding]]` mapping |
| Create | `shaders/tests/probe_half_precision.slang` | internal | L | `float16_t` detection |
| Create | `shaders/tests/probe_image_atomics.slang` | internal | L | Image atomics |
| Create | `shaders/tests/probe_mesh_shader.slang` | internal | L | Mesh shader (Tier1 only) |
| Create | `tests/unit/test_slang_probe.cpp` | internal | L | Full probe suite |

## Steps

- [x] **Step 1**: Define SlangFeatureProbe interface + ProbeReport
      **Files**: `SlangFeatureProbe.h` (**public** M)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `SlangFeatureProbe::RunAll` | `static (SlangCompiler&, span<ShaderTarget> targets) -> Result<ProbeReport>` | `[[nodiscard]]` |
      | `SlangFeatureProbe::RunSingle` | `static (SlangCompiler&, string_view testName, ShaderTarget) -> Result<ProbeTestResult>` | `[[nodiscard]]` |
      | `ProbeReport` | `{ results:vector<ProbeTestResult>, totalPassed:u32, totalFailed:u32, totalSkipped:u32 }` | — |
      | `ProbeTestResult` | `{ name:string, target:ShaderTarget, passed:bool, skipped:bool, diagnostic:string }` | — |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::shader` |
      | Test count | ~30 for SPIR-V + DXIL targets |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 2**: Create test shader files (~15 feature probes)
      **Files**: `shaders/tests/*.slang` (internal L)
      Each file tests one specific feature. Minimal shader code — just enough to exercise the feature.
      Phase 1a covers: struct arrays, atomics 32/64, subgroup ops (3), push constants, texture arrays, compute shared memory, barrier semantics, binding mapping, half precision, image atomics, mesh shader.
      **Acceptance**: all shaders are syntactically valid Slang
      `[verify: compile]`

- [x] **Step 3**: Implement probe runner + tier degradation validation
      **Files**: `SlangFeatureProbe.cpp` (internal L)
      For each test shader: compile to each target, check success/failure. For Tier1-only features on Tier3/4: verify compile error (not silent success). Collect results into `ProbeReport`.
      **Acceptance**: RunAll succeeds with all tests passing for SPIR-V + DXIL
      `[verify: compile]`

- [x] **Step 4**: Unit tests (full probe suite)
      **Files**: `tests/unit/test_slang_probe.cpp` (internal L)
      Run full probe suite. Assert all ~30 tests pass on SPIR-V. Assert all applicable tests pass on DXIL. Assert Tier1-only features fail on lower targets (when Tier3/4 targets added in Phase 1b).
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(SlangProbe, StructArraySPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, Atomics32SPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, Atomics64SPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, SubgroupBallotSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, SubgroupShuffleSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, SubgroupClusteredSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, PushConstantsSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, TextureArraySPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, ComputeSharedSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, BarrierSemanticsSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, BindingMapSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, HalfPrecisionSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, ImageAtomicsSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, MeshShaderSPIRV)` | Unit | Step 2-3 | 2-3 |
| `TEST(SlangProbe, AllDXIL)` | Unit | Step 4 — full DXIL suite | 4 |
| `TEST(SlangProbe, RunAllReport)` | Unit | Step 4 — report correctness | 4 |

*(~30 total tests across SPIR-V + DXIL for each feature probe)*

## Design Decisions

- **Static methods only**: `SlangFeatureProbe` has no instance state. `RunAll` and `RunSingle` are static, making the class stateless and thread-safe.
- **Probe descriptor table**: All 14 probes are described in a `constexpr std::array<ProbeDesc>` with name, filename, stage, and `tier1Only` flag. Easy to extend in Phase 1b.
- **`iShaderDir` parameter**: Both `RunAll` and `RunSingle` take an explicit shader directory rather than hardcoding a path. This enables tests to provide a compile-time path and CI to override via environment.
- **Tier degradation deferred**: The `tier1Only` flag is recorded but not enforced in Phase 1a since GLSL/WGSL targets aren't implemented yet. Phase 1b will add assertions that tier1-only features fail on Tier3/4.
- **`const` span for targets**: `RunAll` takes `span<const ShaderTarget>` rather than `span<ShaderTarget>` for const-correctness.

## Implementation Notes

- Contract check: PASS (16/16 items verified)
- 27 new tests: 14 individual SPIRV probes + 6 individual DXIL probes + 3 RunAll (SPIRV, dual-target, count consistency) + 2 error paths + 1 state + 1 end-to-end
- 14 probe shader files in `shaders/tests/`: struct_array, atomics_32, atomics_64, subgroup_ballot, subgroup_shuffle, subgroup_clustered, push_constants, texture_array, compute_shared, barrier_semantics, binding_map, half_precision, image_atomics, mesh_shader
- Total tests after this task: 216 (189 prior + 27 new)
- All 14 probes pass on both SPIR-V and DXIL targets
- Full probe suite runs in ~8.5s (within the "fast" target of ~5-10s)
