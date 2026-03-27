# T7b.7.4 — ParallelTessellator (Coca Worker Pool, Per-Body IKernel::Tessellate, Progressive Upload)

**Phase**: 13-7b
**Component**: Import Pipeline
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.7.1 | GltfPipeline | Not Started |

## Scope

Per roadmap: "dispatches `IKernel::Tessellate(shapeId, quality)` per-body on Coca worker pool. Progressive upload (render as bodies complete). Priority scheduling (visible first)."

- **Architecture**: CPU thread pool (Coca coroutine workers) tessellates bodies in parallel. Each completed body → auto-meshlet → staging upload → GPU visible immediately.
- **Priority**: Bodies closer to camera tessellated first (frustum-based priority queue).
- **Progressive rendering**: Scene renders with partially-loaded bodies — coarse tessellation first, refined bodies replace as they complete.
- **Performance**: ≥10× faster than single-thread for 1000-body assembly.

## Steps

- [ ] **Step 1**: Implement ParallelTessellator with Coca worker pool dispatch
      `[verify: compile]`
- [ ] **Step 2**: Implement progressive upload + priority scheduling
      `[verify: compile]`
- [ ] **Step 3**: Tests (parallel speedup, progressive render, thread safety)
      `[verify: test]`
