# T6b.2.2 — GPU DAG Cut Optimizer Compute

**Phase**: 09-6b
**Component**: LOD Selector + DAG Cut Optimizer
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.2.1 | LOD Selector Types | Not Started | `ProjectedSphereError()`, `IsCorrectLOD()` |
| T6b.1.5 | Recursive DAG Construction | Not Started | Complete `ClusterDAG` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/DagCutOptimizer.h` | **public** | **H** | `DagCutOptimizer::Create()`, `Execute()` |
| `shaders/vgeo/dag_cut.slang` | internal | L | Per-meshlet IsCorrectLOD compute shader |

- **Algorithm**: Single compute dispatch, 1 thread per meshlet across ALL levels. Each thread evaluates `IsCorrectLOD(parentError, myError, threshold)` → writes surviving meshlet index to output buffer via atomic append.
- **Output**: Flat buffer of visible meshlet indices → fed to task shader as meshlet candidates.
- **Budget**: < 0.3ms for 100K total meshlets across all LOD levels.

### Downstream Consumers

- T6b.11.1 (Demo): calls `Execute()` per frame before geometry pass.
- Phase 14: extends with budget-constrained optimization (triangle budget enforcement).

## Steps

- [ ] **Step 1**: Define DagCutOptimizer.h
      `[verify: compile]`
- [ ] **Step 2**: Implement dag_cut.slang + C++ dispatch
      `[verify: compile]`
- [ ] **Step 3**: Tests (known DAG → expected visible meshlet count)
      `[verify: test]`
