# T7b.4.4 — PmiFilter (Type Bitmask + View-Plane Alignment Filter)

**Phase**: 13-7b
**Component**: GPU PMI & Annotation
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.4.2 | PMI Render Pipeline | Not Started |

## Scope

Filter visible PMI entities by type bitmask (Dimension, GDT, SurfaceRoughness, WeldSymbol, DatumMarker, ToleranceFrame) and/or by view plane alignment (show only PMI whose annotation plane is within ±15° of current view normal). Used for decluttering complex assemblies with hundreds of annotations.

## Steps

- [ ] **Step 1**: Implement PmiFilter with type bitmask + view-plane alignment check
      `[verify: compile]`
- [ ] **Step 2**: Tests (filter by type, filter by view alignment, combined filter)
      `[verify: test]`
