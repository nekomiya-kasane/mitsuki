# Phase cd1: Cooldown #1 — GPU Pipeline Core Stabilization

**Sequence**: cd1 (after Phase 09-6b)
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

3-week stabilization buffer after the GPU-driven rendering core (Phase 6a+6b) is complete. **No new features.** Focus: technical debt cleanup, regression test gap-fill, API review & freeze, performance profiling baseline, documentation.

## Scope

This Cooldown targets all code from Phases 1a through 6b:
- `miki::rhi` (IDevice, ICommandBuffer, all 5 backends)
- `miki::render` (ForwardPass, GBuffer, DeferredResolve, ToneMapping, shadows, post-process)
- `miki::rendergraph` (RenderGraphBuilder, Compiler, Executor, Cache)
- `miki::vgeo` (MeshletGenerator, GpuCullPipeline, VisibilityBuffer, MaterialResolve, SwRasterizer, ClusterDAG, ChunkLoader, MeshletCompressor)
- `miki::shader` (SlangCompiler, PermutationCache, ShaderWatcher)
- `miki::resource` (BindlessTable, BDAManager, StagingRing, ResourceManager, MemoryBudget)
- `miki::scene` (Entity, ComponentPool, QueryEngine, SystemScheduler, BVH, Octree)

## Activities & Tasks

### Activity 1: Tech Debt Cleanup

> Resolve ALL `TODO` / `FIXME` / `HACK` markers accumulated in Phases 1–6b. Code review sweep on critical modules.

| Status | Task ID | Name | Effort |
|--------|---------|------|--------|
| [ ] | Tcd1.1.1 | Audit + resolve TODO/FIXME/HACK markers in miki::rhi | M |
| [ ] | Tcd1.1.2 | Audit + resolve TODO/FIXME/HACK markers in miki::vgeo | M |
| [ ] | Tcd1.1.3 | Audit + resolve TODO/FIXME/HACK markers in miki::render + miki::rendergraph | M |
| [ ] | Tcd1.1.4 | Audit + resolve TODO/FIXME/HACK markers in miki::shader + miki::resource + miki::scene | M |

### Activity 2: Test Gap-Fill

> Audit test coverage per module. Add missing edge-case tests. Target: ≥80% line coverage on core modules.

| Status | Task ID | Name | Effort |
|--------|---------|------|--------|
| [ ] | Tcd1.2.1 | RHI test gap-fill — missing backend edge cases, error paths, resource leak tests | H |
| [ ] | Tcd1.2.2 | RenderGraph test gap-fill — barrier correctness, aliasing edge cases, conditional pass | M |
| [ ] | Tcd1.2.3 | vgeo test gap-fill — GPU compute correctness, meshlet edge cases, HiZ boundary | M |
| [ ] | Tcd1.2.4 | Integration test sweep — cross-module smoke tests for all demos | M |

### Activity 3: API Review & Freeze

> Freeze `IDevice`, `ICommandBuffer`, `RenderGraphBuilder` APIs. Breaking changes after this point require deprecation cycle.

| Status | Task ID | Name | Effort |
|--------|---------|------|--------|
| [ ] | Tcd1.3.1 | API audit: IDevice.h — [[nodiscard]], explicit, Doxygen, Pimpl check | M |
| [ ] | Tcd1.3.2 | API audit: ICommandBuffer.h — signature consistency, error model | M |
| [ ] | Tcd1.3.3 | API audit: RenderGraphBuilder.h — builder pattern consistency | M |
| [ ] | Tcd1.3.4 | API audit: vgeo public headers — GpuCullTypes, ClusterDAG, MeshletCompressor | M |
| [ ] | Tcd1.3.5 | Write api-surface-L05.md (Phase 6a+6b locked API surface) | M |
| [ ] | Tcd1.3.6 | Forward Compatibility Audit — read ALL remaining phases, verify frozen APIs sufficient | H |

### Activity 4: Performance Baseline

> Profile all demos on reference hardware. Record baseline FPS, GPU time per pass, VRAM usage. CI benchmark artifacts.

| Status | Task ID | Name | Effort |
|--------|---------|------|--------|
| [ ] | Tcd1.4.1 | Performance profiling infrastructure — timestamp query per-pass GPU timing | M |
| [ ] | Tcd1.4.2 | Baseline capture: RTX 4070 — all demos (triangle, forward_cubes, deferred_pbr, bindless_scene, gpu_driven_basic, virtual_geometry) | H |
| [ ] | Tcd1.4.3 | Baseline capture: GTX 1060 / Intel UHD 630 (Tier2/4 compat path) | M |
| [ ] | Tcd1.4.4 | CI regression gate — benchmark comparison script, >5% regression = fail | M |

### Activity 5: Documentation

> Architecture overview document, API reference stubs (Doxygen), module dependency diagram.

| Status | Task ID | Name | Effort |
|--------|---------|------|--------|
| [ ] | Tcd1.5.1 | Architecture overview document — module map, data flow, threading model | H |
| [ ] | Tcd1.5.2 | Doxygen stubs for all frozen public headers | M |
| [ ] | Tcd1.5.3 | Module dependency diagram (mermaid) — verify no circular deps | L |

---

## Implementation Order

| Layer | Tasks | Notes |
|-------|-------|-------|
| L0 | Tcd1.1.1–1.4 | Tech debt — can run in parallel |
| L1 | Tcd1.2.1–2.4 | Test gap-fill — after debt cleanup |
| L2 | Tcd1.3.1–3.6 | API audit — after tests stabilize |
| L3 | Tcd1.4.1–4.4 | Perf baseline — after API freeze |
| L4 | Tcd1.5.1–5.3 | Docs — after everything else stable |

**Sequential dependency**: Debt cleanup → Test gap-fill → API freeze → Perf baseline → Docs.
This ordering ensures each activity benefits from the previous: clean code → better tests → stable APIs → reliable profiling → accurate docs.

---

## Acceptance Criteria (Phase Gate)

| # | Check | Target |
|---|-------|--------|
| 1 | TODO/FIXME/HACK count | 0 in core modules (rhi, render, rendergraph, vgeo) |
| 2 | Test pass rate | 100% (excluding known hardware-specific skips) |
| 3 | Line coverage (core) | ≥ 80% on rhi, rendergraph, vgeo |
| 4 | API headers frozen | IDevice.h, ICommandBuffer.h, RenderGraphBuilder.h, all vgeo public headers |
| 5 | api-surface-L05.md | Written + reviewed |
| 6 | Forward compat audit | All downstream phases (7a-1 through 15b) verified against frozen APIs |
| 7 | Perf baseline | Captured on RTX 4070 + GTX 1060, stored as CI artifacts |
| 8 | Regression gate | >5% regression = CI fail (script in place) |
| 9 | Architecture doc | Module map + data flow + threading model documented |
| 10 | Doxygen | All frozen public headers have complete Doxygen |

---

## Test Summary

| Category | Target | Notes |
|----------|--------|-------|
| New unit tests (gap-fill) | ~30-50 | Edge cases, error paths, boundary conditions |
| Integration tests | ~10 | Cross-module smoke tests |
| Benchmark tests | ~5 | Per-pass GPU timing validation |
| **Total new** | ~45-65 | Adds to existing ~2200+ |
