# T6b.10.1 — Dithered LOD Fade (8-Frame Bayer Pattern)

**Phase**: 09-6b
**Component**: LOD Transition Smoothing
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.2.2 | GPU DAG Cut Optimizer | Not Started | LOD selection determines which meshlets are transitioning |

## Context Anchor

### This Task's Contract

**Produces**: Dithered LOD fade in mesh shader — meshlets transitioning between LOD levels use an 8-frame Bayer dither pattern to smoothly blend in/out.

- **Algorithm**: When a meshlet is selected for transition (parent error near threshold), compute a dither alpha from an 8-frame 4×4 Bayer matrix indexed by `(pixelCoord % 4, frameIndex % 8)`. Discard fragments below dither threshold. Both parent and child meshlets rendered simultaneously during transition, with complementary dither patterns.
- **Zero overdraw cost**: Dithered fragments are discarded (alpha test), not blended. No transparency pass needed.
- **Transition duration**: 8 frames at 60fps = ~133ms. Configurable via push constant.
- Per arch spec §5.6: "dithered fade (8-frame Bayer pattern, no overdraw cost)".

### Downstream Consumers

- T6b.11.1 (Demo): LOD transitions visible in virtual_geometry demo.

## Steps

- [ ] **Step 1**: Add dither pattern + transition state to mesh shader
      `[verify: compile]`
- [ ] **Step 2**: Tests (no visual popping in LOD transition, dither pattern correctness)
      `[verify: test]`
