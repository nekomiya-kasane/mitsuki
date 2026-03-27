# T7b.6.3 — Index Buffer Generation Compute (Adaptive Grid → Triangle Mesh)

**Phase**: 13-7b
**Component**: Parametric Tessellation
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.6.2 | Adaptive Subdivision | Not Started |

## Scope

Second compute pass: generate index buffer from adaptive grid → triangle mesh. Handle T-junctions at subdivision boundaries (insert degenerate triangles or constrained edges to prevent cracks). Output feeds directly into `StagingUploader` → GPU vertex buffer (zero CPU readback). Per arch spec: "Output feeds directly into StagingUploader → GPU vertex buffer (zero CPU readback)."

## Steps

- [ ] **Step 1**: Implement index buffer generation compute (grid → triangles + T-junction handling)
      `[verify: compile]`
- [ ] **Step 2**: Tests (watertight mesh, no T-junction cracks, correct triangle orientation)
      `[verify: test]`
