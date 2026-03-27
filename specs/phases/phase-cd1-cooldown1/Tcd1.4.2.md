# Tcd1.4.2 — Baseline Capture: RTX 4070

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Perf Regression
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Scope

- **Problem**: No performance baseline exists. Need reference measurements on primary Tier1 hardware.
- **Affected files**: `benchmarks/baseline_rtx4070.json` (NEW)

### Acceptance Criteria

- [ ] All demos profiled: triangle, forward_cubes, deferred_pbr, bindless_scene, gpu_driven_basic, virtual_geometry
- [ ] Per-pass GPU time captured (ms) for each demo
- [ ] Total FPS, VRAM usage (MB) recorded
- [ ] Results stored as JSON CI artifact

## Steps

- [ ] **Step 1**: Run perf_baseline.py on all demos, capture RTX 4070 baseline
      `[verify: manual]`
