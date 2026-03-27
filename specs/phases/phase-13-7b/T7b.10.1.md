# T7b.10.1 — SpecGlossToMetalRough CPU Converter (Khronos Algorithm)

**Phase**: 13-7b
**Component**: Specular-Glossiness Conversion
**Status**: Not Started
**Effort**: L (< 1h)

## Dependencies

None (L0 task).

## Scope

Per roadmap: "CPU-side converter: `SpecGlossToMetalRough(diffuse, specular, glossiness) -> (baseColor, metallic, roughness)`. Khronos recommended algorithm (glTF spec appendix). Runs once per material during STEP/glTF import — zero runtime cost. MaterialParameterBlock stores only metallic-roughness."

- **Algorithm**: Khronos KHR_materials_pbrSpecularGlossiness → metallic-roughness conversion. `metallic = solveMetallic(perceivedDiffuse, perceivedSpecular)`. `roughness = 1.0 - glossiness`. `baseColor = diffuse * (1 - metallic) + specular * metallic`.
- **Integration**: Called in import pipeline for legacy glTF 1.0 / STEP materials that use specular-glossiness model.

## Steps

- [ ] **Step 1**: Implement SpecGlossToMetalRough converter
      `[verify: compile]`
- [ ] **Step 2**: Tests (known material conversions, round-trip fidelity)
      `[verify: test]`
