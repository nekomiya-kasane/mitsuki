# T7b.2.1 — Depth Peeling Compute (N=8 Layers)

**Phase**: 13-7b
**Component**: GPU Boolean Preview
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

None (L0 task).

## Scope

Per arch spec §3 Pass #34: Depth peeling for boolean preview. N=8 depth layers captured via iterative depth-peel passes. Each pass: render scene with depth test `Less` against previous layer's depth → output next layer's depth + color. Output: N×(D32F + RGBA8) layer textures. Budget: <16ms for 100K tri bodies.

- **Algorithm**: For each of N layers: bind previous layer depth as comparison texture → fragment shader discards fragments at or in front of previous layer → captures next deeper layer.
- **Optimization**: Only render bodies involved in boolean operation (2-3 bodies typically), not full scene.
- **Output**: Depth interval array per-pixel: `[z0, z1, z2, ..., z_{2N-1}]` representing alternating inside/outside transitions.

## Steps

- [ ] **Step 1**: Implement iterative depth peel render passes (N=8)
      `[verify: compile]`
- [ ] **Step 2**: Tests (layer count, depth ordering correctness)
      `[verify: test]`
