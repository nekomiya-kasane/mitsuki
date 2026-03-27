# T6b.11.2 — mesh_simplify_demo (Interactive QEM with Quality Slider)

**Phase**: 09-6b
**Component**: Demo + Integration
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.8.1 | CPU QEM Simplifier | Not Started | `MeshSimplifier::Simplify()` |

## Context Anchor

### This Task's Contract

**Produces**: Interactive mesh simplification demo with ImGui quality slider.

- **UI**: Load OBJ/glTF mesh → display original + simplified side-by-side. Slider controls target triangle count (1% to 100%). Real-time CPU QEM simplification on slider change. Display: triangle count, Hausdorff error, boundary edge count, UV seam count.
- **Boundary visualization**: Color-coded edges — locked boundary (red), UV seam (blue), interior (gray).
- **Export**: Simplified mesh export to OBJ/glTF (Phase 15a integration point).

## Steps

- [ ] **Step 1**: Create CMakeLists + demo skeleton with OBJ loader
      `[verify: compile]`
- [ ] **Step 2**: Implement interactive simplification + ImGui controls
      `[verify: compile]`
- [ ] **Step 3**: Integration tests (simplify + verify quality)
      `[verify: test]`
