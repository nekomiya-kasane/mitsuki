# Tcd1.4.4 — CI Regression Gate Script

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Perf Regression
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: No automated regression detection. Need CI script that compares current run against baseline and fails on >5% regression.
- **Affected files**: `tools/perf_regression_check.py` (NEW), `.github/workflows/ci.yml` (modify)

### Acceptance Criteria

- [ ] Script compares current JSON against baseline JSON, per-pass delta
- [ ] >5% regression on any pass → exit code 1 (CI fail)
- [ ] Human-readable report: pass name, baseline ms, current ms, delta %
- [ ] CI workflow updated to run regression check after test step

## Steps

- [ ] **Step 1**: Implement perf_regression_check.py
      `[verify: compile]`
- [ ] **Step 2**: Integrate into CI workflow
      `[verify: test]`
