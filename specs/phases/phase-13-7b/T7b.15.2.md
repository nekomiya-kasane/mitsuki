# T7b.15.2 — Integration Tests (Measurement Accuracy, PMI Render, Import Pipeline)

**Phase**: 13-7b
**Component**: Demo + Integration
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.15.1 | cad_viewer Demo | Not Started |

## Scope

Comprehensive integration test suite for all Phase 7b subsystems:

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `Measurement_DistanceAccuracy` | Integration | Two-point distance < 0.01mm error vs analytical |
| `Measurement_AngleAccuracy` | Integration | Two-plane angle < 0.001° error |
| `Measurement_MassProperties_Sphere` | Integration | Volume/area/centroid vs analytical < 0.01% |
| `Measurement_MassProperties_VsKernel` | Integration | GPU mass props vs IKernel::ExactMassProperties < 0.01% |
| `Measurement_BodyBodyDistance` | Integration | Min distance vs CPU brute-force < 0.01mm |
| `Measurement_DSFloat_VsDouble` | Integration | DS path vs double path relative error < 1e-9 |
| `Boolean_SphereSubtract` | Integration | Sphere-sphere subtract produces cavity |
| `DraftAngle_KnownGeometry` | Integration | Draft angle map correct for known faces |
| `PMI_GdtRender` | Integration | GD&T symbols render at correct positions |
| `PMI_AP242RoundTrip` | Integration | AP242 PMI import → count matches |
| `Sketch_EntityRender` | Integration | SDF line/arc/circle render quality |
| `Sketch_ConstraintColor` | Integration | DOF-based color coding correct |
| `ParamTess_VsCpuRef` | Integration | GPU tess vs IKernel::Tessellate < 0.01mm deviation |
| `Import_STEP_RoundTrip` | Integration | STEP body/face/PMI counts match |
| `Import_JT_Structure` | Integration | JT assembly tree + LOD levels correct |
| `Import_NoKernel_Miki` | Integration | .miki archive loads without kernel linked |
| `DirectCurveText_Quality` | Integration | 96px glyph rendering quality (SDF coverage) |
| `Perf_FullPipeline` | Benchmark | Full cad_viewer pipeline profiling |

## Steps

- [ ] **Step 1**: Write all Phase 7b integration tests
      `[verify: test]`
