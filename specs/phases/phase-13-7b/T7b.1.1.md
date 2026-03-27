# T7b.1.1 — PrecisionArithmetic.slang (precision_float Abstraction: double / DSFloat)

**Phase**: 13-7b (Measurement, PMI, Tessellation & Import)
**Component**: GPU Measurement
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `shaders/common/precision_arithmetic.slang` | **shared** | **H** | `precision_float`, `DSFloat`, `ds_add`, `ds_mul`, `ds_div`, `ds_sqrt`, `ds_fma` |

- **precision_float**: On Tier1/2/4 (Vulkan `shaderFloat64` / D3D12 / GL `GL_ARB_gpu_shader_fp64`) = `double`. On Tier3 WebGPU = `DSFloat` (Double-Single emulation via 2×float32, Knuth TwoSum + Dekker TwoProduct, ~48-bit mantissa).
- **DSFloat struct**: `{float hi, float lo}` — high part carries most significant bits, low part carries error term. `ds_add(a, b)`: TwoSum algorithm (6 FLOPs). `ds_mul(a, b)`: Dekker TwoProduct (10 FLOPs). `ds_div`, `ds_sqrt`, `ds_fma` similarly.
- **Precision**: <1e-10 relative error. Sufficient for sub-mm at 10km scale (RTE coordinates).
- **Selection**: `SlangFeatureProbe` detects `shaderFloat64` at startup → compile-time `#ifdef` selects double vs DS path. Per arch spec §GPU Measurement Details.

### Downstream Consumers

- T7b.1.2-1.5 (all measurement queries): use `precision_float` for accumulation.
- Phase 14: validates DS precision at 2B+ tri scale.

### Technical Direction

- Per Stack Overflow research: "Modern GPUs have single-precision FMA which allows double-float in about 8 instructions. The hard part is addition (~20 instructions)."
- Per metal-float64 project: full DS library achieves ~48-bit mantissa at 3-6× float32 ALU cost.
- Implementation: single Slang module with `#ifdef MIKI_HAS_FLOAT64` → native double path, else DS path. All measurement shaders import this module.

## Steps

- [ ] **Step 1**: Implement precision_arithmetic.slang with double + DSFloat dual path
      `[verify: compile]`
- [ ] **Step 2**: Validation tests (DS sphere volume R=1km vs double, relative error < 1e-9)
      `[verify: test]`
