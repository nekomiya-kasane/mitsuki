# 05 — Shader Pipeline Implementation Progress

> **Last updated**: 2026-04-13
> **Spec reference**: `specs/05-shader-pipeline.md` > **Status**: Phases 1a through 5+ **complete**. Phase 17+ (Neural) **complete** (inference path). Phase 15a+ (Metal) and VK_EXT_shader_object evaluation **pending**.

---

## 1. Phase Completion Summary

| Phase | Name                 | Status       | Tests                      | Notes                                                                                   |
| ----- | -------------------- | ------------ | -------------------------- | --------------------------------------------------------------------------------------- |
| 1a    | Foundation           | **Complete** | 5 suites                   | SlangCompiler, Reflection, PermutationCache, FeatureProbe, PipelineFactory              |
| 1b    | Multi-Target         | **Complete** | 3 suites (18 tests)        | 5-target compile (SPIRV/DXIL/GLSL/WGSL/MSL), ShaderWatcher, Capability annotations      |
| 2     | Integration          | **Complete** | 4 suites                   | ReflectionLayout, LinkTimeSpecialization, StructLayoutValidation, AsyncPipelineCompiler |
| 3a    | Optimization         | **Complete** | 1 suite                    | Precompiled modules, staleness check, session reuse, transitive dep graph               |
| 3b    | Production           | **Complete** | 2 suites (13 tests)        | Pipeline state machine, batching, bindless, miki-shadow + miki-postfx, multi-queue sync |
| 5+    | Domain Modules       | **Complete** | 40 tests (39 pass, 1 skip) | miki-debug, miki-cad, miki-cae, miki-rt, miki-xr + pass shaders + RTAO                  |
| 15a+  | Metal Validation     | **Pending**  | —                          | Requires Metal `IDevice` backend                                                        |
| 17+   | Neural Inference     | **Complete** | Included in Phase 5+ tests | miki-neural module (portable MLP, no `slang_neural` dependency)                         |
| —     | VK_EXT_shader_object | **Pending**  | —                          | Deferred: requires Tier1 runtime integration                                            |

---

## 2. C++ Infrastructure (Headers + Sources)

### 2.1 Headers (`include/miki/shader/`)

| File                      | Phase | Description                                                             |
| ------------------------- | ----- | ----------------------------------------------------------------------- |
| `ShaderTypes.h`           | 1a    | ShaderTarget, ShaderStage, ShaderBlob, ShaderCompileDesc, BindingInfo   |
| `SlangCompiler.h`         | 1a    | Slang compiler wrapper (Pimpl), `Compile()`, `CompileAllTargets()`      |
| `PermutationCache.h`      | 1a    | 64-bit bitfield key → cached blob LRU + disk                            |
| `SlangFeatureProbe.h`     | 1a    | 29 feature probes across 5 targets                                      |
| `ShaderLayoutGenerator.h` | 2     | Reflection-driven DescriptorSetLayout generation                        |
| `StructLayoutValidator.h` | 2     | C++↔Slang struct layout validation                                      |
| `AsyncPipelineCompiler.h` | 2     | Background PSO compilation thread pool                                  |
| `ShaderWatcher.h`         | 1b    | Filesystem watcher for hot-reload                                       |
| `GLSLPostProcessor.h`     | 1a    | GLSL output post-processing                                             |
| `ManagedPipeline.h`       | 3b    | Pipeline lifecycle state machine (Pending→Compiling→Ready→Stale→Failed) |
| `PipelineBatchCompiler.h` | 3b    | Startup PSO batch compilation with priority classes                     |

### 2.2 Sources (`src/miki/shader/`)

| File                        | Phase | Description                                     |
| --------------------------- | ----- | ----------------------------------------------- |
| `SlangCompiler.cpp`         | 1a/1b | Core compiler, multi-target, session management |
| `PermutationCache.cpp`      | 1a    | In-memory LRU + content-hashed disk cache       |
| `SlangFeatureProbe.cpp`     | 1a/1b | Feature probe execution, Tier1 detection        |
| `ShaderLayoutGenerator.cpp` | 2     | Reflection → descriptor layout                  |
| `StructLayoutValidator.cpp` | 2     | Struct field offset / size validation           |
| `AsyncPipelineCompiler.cpp` | 2     | Thread pool, priority queue                     |
| `ShaderWatcher.cpp`         | 1b    | Filesystem monitoring, generation tracking      |
| `ManagedPipeline.cpp`       | 3b    | State machine transitions, deferred destruction |
| `PipelineBatchCompiler.cpp` | 3b    | Batch submission, critical-path prioritization  |

---

## 3. Slang Shader Modules (13 modules, 92+ implementing files)

### 3.1 Module Inventory

| Module          | Implementing files                                                                                                            | Phase | Precompiled | Tests                |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------- | ----- | ----------- | -------------------- |
| `miki-core`     | 6 (types, constants, bindless, push_constants, color_space, packing)                                                          | 1a    | Yes         | Phase 1a suite       |
| `miki-math`     | 5 (sh, noise, sampling, quaternion, matrix_utils)                                                                             | 1b    | Yes         | Phase 1b suite       |
| `miki-brdf`     | 9 (ggx, diffuse, clearcoat, sheen, sss, aniso, iridescence, dspbr, material_interface)                                        | 2     | Yes         | Phase 2 suite        |
| `miki-geometry` | 9 (meshlet, culling, hiz, lod, macro_binning, sw_rasterizer, vertex_pipeline, visibility_buffer, geometry_pipeline_interface) | 3a    | Yes         | Phase 3a suite       |
| `miki-lighting` | 7 (clustered, ibl, area_light, ddgi, restir, ltc_lut, light_interface)                                                        | 3a    | Yes         | Phase 3a suite       |
| `miki-shadow`   | 3 (vsm, csm, shadow_atlas)                                                                                                    | 3b    | Yes         | Phase 3b suite       |
| `miki-postfx`   | 13 (bloom, dof, motion_blur, tonemap, taa, fxaa, cas, color_grade, ssr, outline, gtao, ssao, **rtao**)                        | 3b/5+ | Yes         | Phase 3b + 5+ suites |
| `miki-debug`    | 6 (wireframe, overdraw, mip_viz, attribute_viz, lod_overlay, gpu_lines)                                                       | 5+    | Yes         | Phase 5+ suite       |
| `miki-cad`      | 7 (hlr, section, pick, measure, boolean_preview, draft_angle, explode)                                                        | 5+    | Yes         | Phase 5+ suite       |
| `miki-cae`      | 6 (fem, scalar_field, streamline, isosurface, tensor_glyph, point_cloud)                                                      | 5+    | Yes         | Phase 5+ suite       |
| `miki-rt`       | 6 (rt_common, rt_reflections, rt_shadows, rt_gi, path_tracer, denoiser)                                                       | 5+    | Yes         | Phase 5+ suite       |
| `miki-xr`       | 3 (stereo, foveated, reprojection)                                                                                            | 5+    | Yes         | Phase 5+ suite       |
| `miki-neural`   | 4 (neural_common, neural_texture, neural_denoiser, nrc)                                                                       | 17+   | Yes         | Phase 5+ suite       |

**Total**: 13 module declarations, 84 implementing files, all registered in `shaders/CMakeLists.txt`.

### 3.2 Pass Shaders (`shaders/passes/`, 15 files)

| Pass File                | Stage(s)                   | Imports                      | Status                       |
| ------------------------ | -------------------------- | ---------------------------- | ---------------------------- |
| `depth_prepass.slang`    | Vertex + Fragment          | `miki_geometry`              | Scaffold (placeholder logic) |
| `gpu_culling.slang`      | Compute                    | `miki_geometry`              | Scaffold                     |
| `light_cluster.slang`    | Compute                    | `miki_lighting`              | Scaffold                     |
| `geometry_main.slang`    | Compute (Tier1: mesh/task) | `miki_geometry`, `miki_brdf` | Scaffold                     |
| `geometry_compat.slang`  | Vertex + Fragment          | `miki_geometry`              | Scaffold                     |
| `material_resolve.slang` | Compute                    | `miki_brdf`, `miki_lighting` | Scaffold                     |
| `deferred_resolve.slang` | Fragment                   | `miki_brdf`, `miki_lighting` | Scaffold                     |
| `vsm_render.slang`       | Vertex + Fragment          | `miki_shadow`                | Scaffold                     |
| `csm_render.slang`       | Vertex + Fragment          | `miki_shadow`                | Scaffold                     |
| `gtao_compute.slang`     | Compute                    | `miki_postfx`                | Scaffold                     |
| `bloom_pass.slang`       | Compute                    | `miki_postfx`                | Scaffold                     |
| `taa_resolve.slang`      | Compute                    | `miki_postfx`                | Scaffold                     |
| `tonemap_pass.slang`     | Compute                    | `miki_postfx`                | Scaffold                     |
| `fullscreen_tri.slang`   | Vertex                     | (standalone)                 | Complete                     |
| `blit.slang`             | Vertex + Fragment          | (standalone)                 | Complete                     |

**Note**: Pass shaders are currently structural scaffolds. Full rendering logic will be filled in as their respective rendering phases (6b, 7b, 8b, etc.) are implemented. All 15 pass files compile successfully on SPIRV (verified by 17 `PassShaderTest` tests).

### 3.3 Feature Probe Shaders (`shaders/tests/`, 29 files)

All 29 `probe_*.slang` files exist and are used by `SlangFeatureProbe` for Tier1/2/3/4 capability detection.

---

## 4. Test Inventory

### 4.1 Test Suites

| Test Executable                 | Phase  | Tests | Status           |
| ------------------------------- | ------ | ----- | ---------------- |
| `test_slang_compiler`           | 1a     | ~5    | Pass             |
| `test_permutation_cache`        | 1a     | ~5    | Pass             |
| `test_pipeline_cache`           | 1a     | ~3    | Pass             |
| `test_pipeline_factory`         | 1a     | ~3    | Pass             |
| `test_shader_types`             | 1a     | ~3    | Pass             |
| `test_multi_target`             | 1b     | 6     | Pass             |
| `test_shader_watcher`           | 1b     | 8     | Pass             |
| `test_capability_annotations`   | 1b     | 4     | Pass             |
| `test_reflection_layout`        | 2      | ~5    | Pass             |
| `test_link_time_specialization` | 2      | ~4    | Pass             |
| `test_struct_layout_validation` | 2      | ~5    | Pass             |
| `test_async_pipeline_compiler`  | 2      | ~4    | Pass             |
| `test_precompiled_modules`      | 3a     | ~8    | Pass             |
| `test_pipeline_state_machine`   | 3b     | ~5    | Pass             |
| `test_phase3b_modules`          | 3b     | 8     | Pass             |
| `test_phase5_domain_modules`    | 5+/17+ | 40    | 39 Pass + 1 Skip |

**Total**: 16 test executables, ~120+ individual tests.

### 4.2 Phase 5+/17+ Test Breakdown (40 tests)

| Category                   | Count | Detail                                                                           |
| -------------------------- | ----- | -------------------------------------------------------------------------------- |
| Module compilation (SPIRV) | 6     | debug, cad, cae, rt, xr, neural                                                  |
| Module compilation (DXIL)  | 6     | debug, cad, cae, rt, xr, neural                                                  |
| Function invocation        | 5     | debug functions, CAD functions, neural texture decode, NRC query, RTAO functions |
| Precompiled blob existence | 6     | debug, cad, cae, rt, xr, neural (neural: skipped until built)                    |
| Pass shader compilation    | 17    | 15 pass files × multiple entry points                                            |

---

## 5. Build System

### 5.1 CMake Version

- `cmake_minimum_required(VERSION 4.0)` — upgraded from 3.21 to match toolchain (4.2.3)
- Generator: Ninja (required for `DEPFILE` + `CODEGEN` features)

### 5.2 Shader CMake (`shaders/CMakeLists.txt`)

- Finds `slangc` executable (prebuilt or source-built)
- Precompiles all 13 modules to `.slang-module` blobs
- Output: `${CMAKE_BINARY_DIR}/shaders/precompiled/`
- **Dependency tracking** (dual-layer):
  - `DEPENDS` (GLOB_RECURSE): bootstrap for first build, catches primary + implementing files
  - `DEPFILE` (`slangc -depfile`): precise cross-module import-graph tracking for incremental builds. Ninja reads `.d` files from `${CMAKE_BINARY_DIR}/shaders/depfiles/`
- **`CODEGEN`** (CMP0171): `cmake --build . --target codegen` drives only shader precompilation, skipping all C++ compilation. Enables fast shader iteration
- `cmake_policy(SET CMP0171 NEW)` set explicitly

### 5.3 Test CMake (`cmake/targets/tests.cmake`)

- All 16 test executables defined
- Compile definitions: `MIKI_SHADER_DIR`, `MIKI_SHADER_TESTS_DIR`, `MIKI_PASS_DIR`, `MIKI_PRECOMPILED_DIR`
- Timeout: 60s per test

---

## 6. Architecture Decisions

| Decision                                        | Rationale                                                                                                                                                                                                               |
| ----------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Portable MLP inference** (not `slang_neural`) | `slang_neural` requires Slang >= 2026.3.1 and doesn't guarantee all 5 targets. Our `linearLayerFloat` + activation functions compile everywhere. Future: add `CoopVec` hardware path behind `__target_switch` for Tier1 |
| **Pass shaders as scaffolds**                   | Full logic depends on rendering phases not yet implemented. Scaffolds verify module import + compilation correctness now                                                                                                |
| **No `[require]` on library modules**           | Library modules contain only pure math functions. Slang auto-infers capabilities from builtins. `[require]` only needed for `spirv_asm`/`__intrinsic_asm` intrinsics                                                    |
| **13 modules (not 12)**                         | Spec originally planned 12. `miki-neural` added as 13th module in Phase 17+                                                                                                                                             |
| **RTAO in miki-postfx** (not miki-rt)           | RTAO is an AO technique used in postfx pipeline. RT ray generation is in `miki-rt`, but the AO-specific logic (power curve, bilateral filter, temporal accumulate) belongs with GTAO/SSAO                               |

---

## 7. Remaining Work

### 7.1 VK_EXT_shader_object Evaluation (Phase 5+ — Deferred)

| Item                                         | Status      | Blocker                                       |
| -------------------------------------------- | ----------- | --------------------------------------------- |
| Performance benchmark (PSO vs shader object) | Not started | Requires Tier1 hardware + runtime integration |
| `IPipelineFactory` migration design          | Not started | Depends on benchmark results                  |
| Decision point (adopt if >= 20% improvement) | Not started | —                                             |

**Prerequisite**: Runtime pipeline integration (`IDevice::CreateGraphicsPipeline`) must be operational to measure real-world PSO creation time vs `VkShaderEXT` creation time.

### 7.2 Phase 15a+ — Metal Backend Shader Validation (Blocked)

| Step | Item                                                     | Status      | Blocker                               |
| ---- | -------------------------------------------------------- | ----------- | ------------------------------------- |
| 1    | MSL compilation probes (15 probes)                       | Not started | Metal `IDevice` backend not available |
| 2    | Metal `__target_switch` paths (push constants, bindless) | Not started | Step 1                                |
| 3    | Metal argument buffer layout (set 3 mapping)             | Not started | Step 2                                |

**Prerequisite**: Metal backend (`IDevice` implementation for Metal/MoltenVK) must be operational. The Slang compiler already generates MSL output — this phase validates correctness.

### 7.3 Pass Shader Logic (Blocked on Rendering Phases)

| Pass                        | Full Logic Phase        | Status   |
| --------------------------- | ----------------------- | -------- |
| `depth_prepass`             | Phase 6b (GPU Geometry) | Scaffold |
| `gpu_culling`               | Phase 6b                | Scaffold |
| `geometry_main` (mesh/task) | Phase 6b                | Scaffold |
| `geometry_compat` (vertex)  | Phase 6b                | Scaffold |
| `light_cluster`             | Phase 7b (Lighting)     | Scaffold |
| `material_resolve`          | Phase 7b                | Scaffold |
| `deferred_resolve`          | Phase 7b                | Scaffold |
| `vsm_render`                | Phase 8a (Shadows)      | Scaffold |
| `csm_render`                | Phase 8a                | Scaffold |
| `gtao_compute`              | Phase 9 (PostFX)        | Scaffold |
| `bloom_pass`                | Phase 9                 | Scaffold |
| `taa_resolve`               | Phase 9                 | Scaffold |
| `tonemap_pass`              | Phase 9                 | Scaffold |

### 7.4 Test Gaps (Future Phases)

| Gap                                  | Priority | Phase                                           |
| ------------------------------------ | -------- | ----------------------------------------------- |
| L1 CPU Golden Reference tests (~300) | Medium   | Next shader phase                               |
| L3 GPU Integration tests (~100)      | Medium   | After runtime integration                       |
| L4 Visual Regression tests (~20)     | Low      | After rendering pipeline                        |
| MSL compilation tests (15 probes)    | Medium   | Phase 15a+                                      |
| `CoopVec` hardware path tests        | Low      | When VK_NV_cooperative_vector drivers available |

---

## 8. File Count Summary

| Category                                          | Count   |
| ------------------------------------------------- | ------- |
| C++ headers (`include/miki/shader/`)              | 11      |
| C++ sources (`src/miki/shader/`)                  | 9       |
| Module declarations (`shaders/miki/miki-*.slang`) | 13      |
| Implementing files (`shaders/miki/**/*.slang`)    | 84      |
| Pass shaders (`shaders/passes/*.slang`)           | 15      |
| Feature probes (`shaders/tests/probe_*.slang`)    | 29      |
| C++ test files (`tests/shader/test_*.cpp`)        | 16      |
| **Total shader-related files**                    | **177** |
