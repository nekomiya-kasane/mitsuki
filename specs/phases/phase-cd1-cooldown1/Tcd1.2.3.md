# Tcd1.2.3 — vgeo Test Gap-Fill

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Test Gap
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: GPU compute correctness gaps in vgeo module — meshlet boundary cases, HiZ boundary, radix sort edge cases, SW rasterizer depth test, material resolve tile edge cases.
- **Affected files**: `tests/unit/test_gpu_prefix_sum.cpp`, `test_gpu_radix_sort.cpp`, `test_hiz_pyramid.cpp`, `test_scene_buffer.cpp`, `test_macro_binning.cpp`, `test_gpu_cull.cpp`, `test_visibility_buffer.cpp`
- **Affected phases**: 6a

### Acceptance Criteria

- [ ] ≥80% line coverage on `src/miki/vgeo/`
- [ ] HiZ EndToEnd test fixed and passing
- [ ] All GPU compute tests include boundary cases (0 elements, 1 element, max elements)
- [ ] All new tests pass

## Steps

- [ ] **Step 1**: Coverage audit + write missing vgeo tests (+10-15 tests)
      `[verify: test]`
