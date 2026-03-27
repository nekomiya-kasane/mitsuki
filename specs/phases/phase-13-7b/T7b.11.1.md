# T7b.11.1 — Vertex Displacement Compute Shader

**Phase**: 13-7b
**Component**: Displacement Mapping
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

Per roadmap: "Compute shader vertex displacement pass: read displacement texture, offset vertices along normal by `displacementScale * textureSample`. Runs before meshlet compression. Optional — only for materials with `displacementTex != 0xFFFFFFFF`. Budget: <0.5ms @1M vertices. Tier1 only."

## Steps

- [ ] **Step 1**: Implement displacement compute shader (sample texture, offset vertices)
      `[verify: compile]`
- [ ] **Step 2**: Tests (displacement correctness, zero displacement when tex=0xFFFFFFFF)
      `[verify: test]`
