# T6b.4.2 — Mesh Shader Decoder (Compressed Meshlet GPU Decompression)

**Phase**: 09-6b
**Component**: Meshlet Compression
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.4.1 | Compression Types + CPU Encoder | Not Started | `CompressedMeshlet` format |

## Context Anchor

### This Task's Contract

**Produces**: Modified `mesh_geo.slang` that reads compressed meshlet data and dequantizes in-shader.

- **Dequantize positions**: `pos = float3(quantized) / 65535.0 * aabbExtent + aabbMin` (2 FMA per vertex, 6 ALU total).
- **Decode octahedral normals**: `octDecode(uint8×2) → float3 normal` (~8 ALU per vertex).
- **8-bit local indices**: Already supported by Phase 6a mesh shader (no change needed).
- **Performance**: +8 ALU/vertex vs uncompressed. Offset by 50% bandwidth reduction → net positive on bandwidth-limited scenes.

### Downstream Consumers

- T6b.11.1 (Demo): renders compressed meshlets at full quality.
- Phase 7a-1: HLR edge classify reads decompressed normals.

## Steps

- [ ] **Step 1**: Add compressed meshlet struct to vgeo_common.slang
      `[verify: compile]`
- [ ] **Step 2**: Modify mesh_geo.slang to support compressed vertex fetch path
      `[verify: compile]`
- [ ] **Step 3**: GPU decode correctness tests (readback vs CPU reference)
      `[verify: test]`
