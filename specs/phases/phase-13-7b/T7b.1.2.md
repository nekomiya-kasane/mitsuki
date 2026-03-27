# T7b.1.2 — Point/Face/Distance/Angle/Radius GPU Queries

**Phase**: 13-7b
**Component**: GPU Measurement
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.1.1 | PrecisionArithmetic.slang | Not Started |

## Scope

GPU measurement compute shaders for basic CAD queries, all using `precision_float`:
- **Point query**: Ray query → float64 BDA position, readback 24B (double3).
- **Face query**: Ray query → face normal + area from triangle fan reduction.
- **Distance**: Two-point distance, point-to-face distance, edge-to-edge min distance. Single compute dispatch per query.
- **Angle**: Angle between two faces/edges. `acos(dot(n1, n2))`, precision_float accumulation. <0.001° accuracy.
- **Radius**: Least-squares circle fit on sampled surface points. Used for fillet/hole radius measurement.
- Per arch spec: "Sub-mm GPU-parallel precision measurement."

## Steps

- [ ] **Step 1**: Implement point + face query compute shaders
      `[verify: compile]`
- [ ] **Step 2**: Implement distance + angle + radius compute shaders
      `[verify: compile]`
- [ ] **Step 3**: Tests (vs analytical reference values, sub-mm accuracy at 10km distance)
      `[verify: test]`
