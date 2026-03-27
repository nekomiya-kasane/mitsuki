# T7b.4.3 — STEP AP242 PMI Import Integration

**Phase**: 13-7b
**Component**: GPU PMI & Annotation
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.4.1 | PMI Entity Types | Not Started |

## Scope

Import PMI entities from STEP AP242 via `IKernel::Import()`. Map STEP geometric_tolerance, datum_feature, surface_texture entities to miki `PmiAnnotation` types. Store PMI attached to topology in `TopoGraph`. Round-trip test: AP242 file → import → re-export → compare PMI entities.

## Steps

- [ ] **Step 1**: Implement AP242 PMI → PmiAnnotation mapping in import pipeline
      `[verify: compile]`
- [ ] **Step 2**: Tests (AP242 PMI round-trip, GD&T symbol type mapping)
      `[verify: test]`
