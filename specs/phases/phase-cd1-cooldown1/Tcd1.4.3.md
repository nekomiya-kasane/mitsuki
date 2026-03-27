# Tcd1.4.3 — Baseline Capture: GTX 1060 / Intel UHD 630 (Tier2/4 Compat)

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Perf Regression
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: Compat pipeline (Tier2/4) has no performance baseline. Need reference on low-end hardware.
- **Affected files**: `benchmarks/baseline_gtx1060.json`, `benchmarks/baseline_uhd630.json` (NEW)

### Acceptance Criteria

- [ ] Compat-path demos profiled on GTX 1060 (Tier2) and Intel UHD 630 (Tier4)
- [ ] Per-pass GPU time + FPS + VRAM recorded
- [ ] Results stored as JSON CI artifacts

## Steps

- [ ] **Step 1**: Run perf_baseline.py on compat demos, capture baselines
      `[verify: manual]`
