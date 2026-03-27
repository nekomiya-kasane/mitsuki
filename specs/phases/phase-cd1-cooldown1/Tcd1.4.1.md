# Tcd1.4.1 — Performance Profiling Infrastructure

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Perf Regression
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Need automated per-pass GPU timing infrastructure to capture performance baselines and detect regressions.
- **Affected files**: `include/miki/rhi/TimestampQuery.h` (existing from Phase 2), new `tools/perf_baseline.py` script

### Acceptance Criteria

- [ ] Per-pass GPU timestamp queries working in all demos
- [ ] `perf_baseline.py` script captures: FPS, per-pass GPU time (ms), VRAM usage (MB), outputs JSON
- [ ] JSON output format documented and stable for CI consumption

## Steps

- [ ] **Step 1**: Verify timestamp query infrastructure works across all demos
      `[verify: compile]`
- [ ] **Step 2**: Create perf_baseline.py capture script
      `[verify: test]`
