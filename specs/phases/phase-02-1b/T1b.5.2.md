# T1b.5.2 — SlangFeatureProbe GLSL/WGSL Extensions (~15 New Tests)

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: Slang Compiler (quad-target)
**Roadmap Ref**: `roadmap.md` L1149 — SlangFeatureProbe extended: GLSL-specific + WGSL-specific tests
**Status**: Complete
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1b.5.1 | SlangCompiler quad-target | Not Started | `SlangCompiler::Compile({target=GLSL/WGSL})` |
| T1a.11.1 | SlangFeatureProbe | Complete | `SlangFeatureProbe::RunAll()`, `ProbeReport`, probe shader pattern |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `shaders/tests/probe_glsl_*.slang` | internal | **M** | ~8 GLSL-specific probe shaders |
| `shaders/tests/probe_wgsl_*.slang` | internal | **M** | ~7 WGSL-specific probe shaders |
| `tests/unit/test_slang_probe_glsl_wgsl.cpp` | internal | L | Probe runner tests for new targets |

- **Error model**: `ProbeReport` aggregates pass/fail/skip per test
- **Invariants**: `RunAll(compiler, {GLSL, WGSL}, shaderDir)` includes all new probes. Each probe tests one specific GLSL or WGSL feature/limitation.

### Downstream Consumers

- Probe shaders (internal, heat **M**):
  - CI: all probes run on every commit — ensures Slang→GLSL/WGSL codegen doesn't regress
  - Phase 11c: GL hardening uses probe results to identify GLSL codegen issues

### Upstream Contracts

- T1b.5.1: `SlangCompiler::Compile({target=GLSL})` produces valid GLSL 4.30
- T1a.11.1: `SlangFeatureProbe::RunAll()` — discovers `probe_*.slang` files in shader dir
  - Source: `include/miki/shader/SlangFeatureProbe.h`

### Technical Direction

- **GLSL-specific probes** (from roadmap): BDA→SSBO mapping, `layout(binding)` vs descriptor set, texture unit limits, `gl_WorkGroupSize`, `shared` memory, `imageStore`/`imageLoad`, `atomicAdd` (32-bit only in GLSL 4.30).
- **WGSL-specific probes** (from roadmap): storage buffer alignment (16-byte), workgroup size limits (`maxComputeWorkgroupSizeX/Y/Z`), no 64-bit atomics → error, `@group/@binding` syntax, `textureSample` vs `textureSampleLevel`, `array<>` stride constraints.
- **Probe naming**: `probe_glsl_<feature>.slang` and `probe_wgsl_<feature>.slang`. The probe runner auto-discovers by glob pattern.
- **Target-specific run**: each probe is compiled only against its relevant target (GLSL probes against `ShaderTarget::GLSL`, WGSL probes against `ShaderTarget::WGSL`). Use `RunSingle` with explicit target.
- **Expected failures**: some probes intentionally test features that should fail on a target (e.g., 64-bit atomics on WGSL should fail). Probe result `skipped=true` is acceptable for expected failures.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `shaders/tests/probe_glsl_ssbo_mapping.slang` | internal | **M** | BDA → SSBO |
| Create | `shaders/tests/probe_glsl_binding_layout.slang` | internal | **M** | layout(binding) |
| Create | `shaders/tests/probe_glsl_texture_units.slang` | internal | **M** | Texture unit limit |
| Create | `shaders/tests/probe_glsl_workgroup.slang` | internal | **M** | gl_WorkGroupSize |
| Create | `shaders/tests/probe_glsl_shared_memory.slang` | internal | **M** | shared memory |
| Create | `shaders/tests/probe_glsl_image_load_store.slang` | internal | **M** | imageStore/imageLoad |
| Create | `shaders/tests/probe_glsl_atomic_32.slang` | internal | **M** | 32-bit atomicAdd |
| Create | `shaders/tests/probe_glsl_push_constant_ubo.slang` | internal | **M** | push constant → UBO |
| Create | `shaders/tests/probe_wgsl_storage_alignment.slang` | internal | **M** | 16-byte alignment |
| Create | `shaders/tests/probe_wgsl_workgroup_limits.slang` | internal | **M** | workgroup size limits |
| Create | `shaders/tests/probe_wgsl_no_64bit_atomics.slang` | internal | **M** | 64-bit atomics error |
| Create | `shaders/tests/probe_wgsl_group_binding.slang` | internal | **M** | @group/@binding syntax |
| Create | `shaders/tests/probe_wgsl_texture_sample.slang` | internal | **M** | textureSample variants |
| Create | `shaders/tests/probe_wgsl_array_stride.slang` | internal | **M** | array stride constraints |
| Create | `shaders/tests/probe_wgsl_push_constant_ubo.slang` | internal | **M** | push constant → UBO |
| Create | `tests/unit/test_slang_probe_glsl_wgsl.cpp` | internal | L | Probe runner tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Write GLSL probe shaders (~8)
      **Files**: `shaders/tests/probe_glsl_*.slang` (internal M)
      Each shader exercises one GLSL 4.30 feature. Follow existing probe pattern from Phase 1a.
      **Acceptance**: all probes compile to GLSL target
      `[verify: compile]`

- [x] **Step 2**: Write WGSL probe shaders (~7)
      **Files**: `shaders/tests/probe_wgsl_*.slang` (internal M)
      Each shader exercises one WGSL feature/limitation. `probe_wgsl_no_64bit_atomics.slang` should intentionally fail compilation → expected failure.
      **Acceptance**: all probes compile (or fail as expected) to WGSL target
      `[verify: compile]`

- [x] **Step 3**: Write probe runner tests
      **Files**: `test_slang_probe_glsl_wgsl.cpp` (internal L)
      Run `SlangFeatureProbe::RunAll` with `{GLSL, WGSL}` targets. Verify expected pass/fail counts. Individual `RunSingle` tests for key probes.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(SlangProbeGLSL, AllProbesCompile)` | Positive | RunAll with GLSL target — all pass | 1-3 |
| `TEST(SlangProbeGLSL, SSBOMappingCorrect)` | Positive | BDA→SSBO probe passes | 1-3 |
| `TEST(SlangProbeGLSL, BindingLayoutCorrect)` | Positive | layout(binding) probe passes | 1-3 |
| `TEST(SlangProbeGLSL, PushConstantUBO)` | Positive | push constant → UBO probe passes | 1-3 |
| `TEST(SlangProbeWGSL, AllProbesCompile)` | Positive | RunAll with WGSL target — expected results | 2-3 |
| `TEST(SlangProbeWGSL, StorageAlignmentCorrect)` | Positive | 16-byte alignment probe passes | 2-3 |
| `TEST(SlangProbeWGSL, No64BitAtomicsFails)` | Error | 64-bit atomics probe fails as expected | 2-3 |
| `TEST(SlangProbeWGSL, GroupBindingSyntax)` | Positive | @group/@binding probe passes | 2-3 |
| `TEST(SlangProbeWGSL, PushConstantUBO)` | Positive | push constant → UBO probe passes | 2-3 |
| `TEST(SlangProbeGLSLWGSL, TotalProbeCount)` | Boundary | total probe count >= 15 new | 1-3 |
| `TEST(SlangProbeGLSLWGSL, ReportCountsConsistent)` | State | totalPassed + totalFailed + totalSkipped == results.size() | 1-3 |
| `TEST(SlangProbeGLSLWGSL, EndToEnd_FullQuadProbeRun)` | **Integration** | RunAll with all 4 targets — full report consistent | 1-3 |

## Design Decisions

- **Probes registered in kProbes array**: all 15 new probes added to the central `kProbes` constexpr array in `SlangFeatureProbe.cpp`, consistent with existing pattern. No auto-discovery by glob — explicit registration.
- **tier1Only for wgsl_no_64bit_atomics**: marked `tier1Only=true` so it is skipped on Tier2+ devices in future gated runs. For WGSL compilation target, it fails as expected (64-bit atomics unsupported).
- **Unified fixture**: single `SlangProbeGLSLWGSLFixture` shared across all 12 tests rather than splitting GLSL/WGSL fixtures.
- **Updated existing test counts**: `test_slang_probe.cpp` RunAllSPIRV and RunAllDualTarget updated from 14 to 29 probes to account for new entries.

## Implementation Notes

- 8 GLSL probes: ssbo_mapping, binding_layout, texture_units, workgroup, shared_memory, image_load_store, atomic_32, push_constant_ubo
- 7 WGSL probes: storage_alignment, workgroup_limits, no_64bit_atomics (expected fail), group_binding, texture_sample, array_stride, push_constant_ubo
- Total probe count: 29 (14 original + 15 new)
- Test count: 12 new + 27 existing = 39 probe tests total
- All probes are compute stage, compilation-only (no GPU required)
