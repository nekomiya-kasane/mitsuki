# T7b.4.5 — PMI Tests (GD&T Rendering, Datum Placement, AP242 Round-Trip)

**Phase**: 13-7b
**Component**: GPU PMI & Annotation
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.4.4 | PmiFilter | Not Started |

## Scope

Comprehensive PMI test suite: GD&T symbol rendering correctness (position/flatness/perpendicularity), datum marker placement, surface roughness symbol display, weld symbol ISO 2553, tolerance frame rendering, AP242 import round-trip, PmiFilter type bitmask + view alignment, PMI visibility toggle per-view.

## Steps

- [ ] **Step 1**: Write PMI correctness + filter + round-trip tests
      `[verify: test]`
