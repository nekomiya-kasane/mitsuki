# T{id}.X.Y — {Task Name}

**Phase**: {nn}-{id} (Cooldown #{n})
**Category**: Bug Fix | Tech Debt | Test Gap | API Audit | Perf Regression | Docs
**Status**: Not Started | In Progress | Complete
**Current Step**: —
**Resume Hint**: —
**Effort**: S (< 1h) | M (1-2h) | L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | — | — | — |

## Scope

> Cooldown Tasks do NOT produce new public APIs or new features.
> They stabilize, audit, fix, and fill gaps in existing code.
> No Context Anchor Card required — no Contract Verification required.

### What this Task does

- **Problem**: {describe the bug, tech debt, test gap, or audit target}
- **Root cause**: {if known — for bugs and regressions}
- **Affected files**: {list files to modify}
- **Affected phases**: {which completed phase(s) this touches}

### Acceptance Criteria

- [ ] {Concrete, verifiable criterion — e.g., "ASAN clean on test_foo"}
- [ ] {e.g., "TODO count in src/miki/rhi/ drops from 5 to 0"}
- [ ] {e.g., "Golden image diff < 1% on compat_viewer"}

## Files

| Action | Path | Notes |
|--------|------|-------|
| Modify | `src/miki/module/Foo.cpp` | Fix: {description} |
| Modify | `tests/unit/test_foo.cpp` | Add regression test |

## Steps

- [ ] **Step 1**: {Description}
      **Acceptance**: {what passes}
      `[verify: compile]`

- [ ] **Step 2**: {Description}
      **Acceptance**: {what passes}
      `[verify: test]`

## Tests

| Test Name | Type | Validates |
|-----------|------|-----------|
| `TEST(Module, RegressionFixName)` | Unit | Step 1 — regression doesn't recur |

## Audit Checklist (for API Audit tasks only)

> Fill this section ONLY for tasks with Category = API Audit.

| Header | `[[nodiscard]]` | `explicit` | Pimpl | Doxygen | `static_assert` | Verdict |
|--------|----------------|------------|-------|---------|-----------------|---------|
| `IFoo.h` | OK | OK | OK | Missing 2 | OK | FIX |

## Notes

*(Issues found, workarounds applied, things deferred to next phase.)*
