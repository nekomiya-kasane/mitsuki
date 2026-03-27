# T7b.15.1 — cad_viewer Demo (Full CAD Viewer with All Phase 7b Features)

**Phase**: 13-7b
**Component**: Demo + Integration
**Status**: Not Started
**Effort**: H (2-4h)

## Dependencies

All above tasks.

## Scope

Per roadmap: "STEP/JT file loaded, assembly tree, GPU HLR, section v2, linked-list OIT, picking v2, boolean preview, explode v2, measurement (incl. body-to-body distance, mass properties), draft angle overlay, PMI annotation, full ImGui control panel."

- **File load**: STEP via IKernel + ParallelTessellator, JT via JtImporter, glTF via GltfPipeline.
- **Assembly tree**: ImGui tree view from CadScene (Phase 8 integration point).
- **Full pipeline**: GPU HLR + section + OIT + picking + measurement + PMI + sketch + boolean preview + explode.
- **ImGui panels**: measurement results, PMI filter, draft angle controls, sketch edit panel, import progress, per-pass GPU timing.
- **CLI**: `--backend vulkan --file model.step`

## Steps

- [ ] **Step 1**: Create CMakeLists + demo skeleton with file load dispatch
      `[verify: compile]`
- [ ] **Step 2**: Wire all Phase 7b subsystems into render graph
      `[verify: compile]`
- [ ] **Step 3**: ImGui control panels for all features
      `[verify: compile]`
- [ ] **Step 4**: Visual + performance verification
      `[verify: visual]` `[verify: manual]`
