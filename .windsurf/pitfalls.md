# Agent Pitfall Log (Cold Layer)

> Full history of Agent errors. Hot pitfalls are promoted to `.windsurfrules` §9.
> See `.windsurfrules` §10.1 for promotion/retirement rules.

| Date | Module | Trigger | Root Cause | Prevention | Hot? |
|------|--------|---------|------------|------------|------|
| 2026-02 | rhi/mock | `#include <unexpected>` compile error | coca LLVM 20 toolchain lacks `<unexpected>` header | Use `<expected>` instead | Yes |
| 2026-03 | text/planning | MSDF-bitmap hard switch causes visible pop during scroll-zoom; screenPxRange<1.0 causes noise; no frustum culling for world-space labels | Phase planning only checked steady-state behavior (big text=MSDF, small text=bitmap), never simulated continuous user interaction (zoom). No algorithm parameter extreme analysis (screenPxRange→0). No seam behavior defined between MSDF task and bitmap task at the threshold. | **Mandatory**: Run §4.2.1 Phase Planning Quality Checklist before marking any phase plan complete. Specifically: (1) Interaction Scenario Walkthrough — simulate zoom/pan/resize sequences, (2) Algorithm Parameter Extreme Analysis — identify degenerate cases for core params, (3) Subsystem Seam Behavior — explicitly define crossfade/transition at task boundaries, (4) Dynamic Quality Metrics — add visual-continuity acceptance criteria, (5) Known-Defect Survey — search community bug reports for adopted techniques. | Yes |
| 2026-03 | rhi/vulkan | `VUID-VkDeviceCreateInfo-pNext-02830` validation error: standalone feature structs conflict with `VkPhysicalDeviceVulkanXXFeatures` in pNext chain | Vulkan 1.2/1.3 promoted features (e.g. `timelineSemaphore`, `synchronization2`, `dynamicRendering`, `hostQueryReset`) must NOT appear as both standalone structs AND inside `VkPhysicalDeviceVulkan12/13Features` in the same pNext chain. | When adding a new Vulkan feature, check if it was promoted to a core version struct. If `VkPhysicalDeviceVulkanXXFeatures` is already in the chain, add the feature there and remove any standalone struct for that feature. | Yes |
| 2026-03 | resource | `ResourceManager::CreateTexture(desc, autoBindless=true)` SEH crash (0xc0000005) | Auto-bindless path calls `BindlessTable::RegisterTexture(tex, SamplerHandle{})` with a **null sampler** — Vulkan descriptor write with null sampler crashes the driver. | Never use `autoBindless=true` unless ResourceManager has a valid default sampler. Use manual `BindlessTable::RegisterTexture(tex, sampler)` with an explicit sampler handle. File fix in Phase 5 cleanup: add sampler param to `ResourceManager::CreateTexture` or `SetDefaultSampler()`. | Yes |

---

# Deferred Implementation Registry

> **Purpose**: Track all `NotImplemented` stubs and `GTEST_SKIP` paths that exist because
> a feature is deferred to a later task/phase. When implementing the corresponding task,
> **search this table** to find all stubs and skipped tests that must be resolved.
>
> **Rule**: When a task listed in "Unblocks" column is implemented, the agent MUST:
> 1. Replace the `NotImplemented` stub with a real implementation
> 2. Remove the `GTEST_SKIP` guard so the test exercises the new code
> 3. Delete the row from this table

## NotImplemented Stubs (return ErrorCode::NotImplemented)

| File:Line | Method | Unblocks (Task) | Notes |
|-----------|--------|-----------------|-------|

_(All stubs resolved.)_

## GTEST_SKIP Due to Unimplemented Features

| Test File:Line | Test Name | Skip Reason | Unblocks (Task) |
|----------------|-----------|-------------|-----------------|
| `tests/integration/test_triangle_render.cpp:128` | `TriangleDemo.RenderVulkan` | `CreateGeometryPass not implemented for this tier` | Tests call CreateGeometryPass with no shaders — skip is correct until tests provide compiled SPIR-V/WGSL. |
| `tests/integration/test_triangle_render.cpp:359` | `TriangleDemo.EndToEnd_CompileAndRender` | `CreateGeometryPass not implemented for this tier` | Same as above. Full end-to-end pipeline: compile shader → create pipeline → render → readback. |

## Dummy/Stub Returns (return valid but fake data)

| File:Line | Method | Unblocks (Task) | Notes |
|-----------|--------|-----------------|-------|

_(All dummy returns resolved — factories now forward to IDevice::CreateGraphicsPipeline or return validated sentinels.)_
