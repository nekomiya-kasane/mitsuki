# T6b.7.2 — Camera-Predictive Prefetch

**Phase**: 09-6b
**Component**: Zero-Stall Async Streaming
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.7.1 | Transfer Queue + Timeline Semaphore | Not Started | Async upload path |

## Context Anchor

### This Task's Contract

**Produces**: CPU prefetch thread that predicts camera trajectory 2-5 frames ahead and pre-requests cluster loads.

- **Prediction model**: Linear extrapolation from camera velocity + acceleration + zoom-rate. For turntable: circular arc prediction. For flythrough: spline-based path prediction.
- **Cache hit target**: >95% during smooth navigation, 100% for turntable/flythrough (fully predictable).
- **Budget**: Prefetch consumes ≤20% of streaming bandwidth (priority lower than on-demand loads).
- Per arch spec §5.8.1: "Camera velocity + acceleration + zoom-rate → predict target LOD 2-5 frames ahead."

## Steps

- [ ] **Step 1**: Implement camera trajectory prediction + prefetch request submission
      `[verify: compile]`
- [ ] **Step 2**: Tests (prediction accuracy for linear/turntable paths, cache hit rate)
      `[verify: test]`
