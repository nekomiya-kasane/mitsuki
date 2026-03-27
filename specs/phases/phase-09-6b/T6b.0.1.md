# T6b.0.1 — Fix HiZ EndToEnd Test

**Phase**: 09-6b (ClusterDAG, Streaming, LOD)
**Component**: Pre-Phase Gate Fix
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: S (<1h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | — | — | Phase 6a HiZ Pyramid implementation |

## Context Anchor

### This Task's Contract

**Produces**: Fix for `HiZPyramidTest.EndToEnd_BuildFullPyramid` test failure.

- **Root cause**: Test reads HiZ mip levels starting from full-res dimensions (W×H) but HiZ mip 0 is half-res (W/2 × H/2). ReadbackMip reads too many pixels, getting uninitialized data.
- **Fix**: Adjust test's initial `mw, mh` to `W/2, H/2` since mip 0 of HiZ = half-res of depth buffer. Also verify the HiZ Build() dispatch loop correctly propagates max values through all mip levels.

### Downstream Consumers

- T6b.9.2 (Two-Phase Occlusion): depends on correct HiZ pyramid for early+late culling.

## Steps

- [ ] **Step 1**: Debug and fix HiZ EndToEnd test
      `[verify: test]`

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `HiZPyramidTest.EndToEnd_BuildFullPyramid` | Integration | Full mip chain propagation correctness |
