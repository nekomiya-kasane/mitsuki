# T6b.4.1 — Meshlet Compression Types + CPU Encoder

**Phase**: 09-6b
**Component**: Meshlet Compression
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | — | — | Phase 6a `MeshletTypes.h` (MeshletDescriptor, MeshletBuildResult) |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/MeshletCompressor.h` | **public** | **H** | `CompressedMeshlet`, `MeshletCompressor::Encode()`, `MeshletCompressor::Decode()` (CPU ref) |

- **CompressedMeshlet**: Per-meshlet AABB (float3 min + float3 extent = 24B), quantized vertices (uint16×3 per vertex), octahedral normals (uint8×2 per vertex), 8-bit local triangle indices (uint8×3 per tri). Header: vertexCount, triangleCount, materialId.
- **Encode**: Input = `MeshletBuildResult` (uncompressed). Output = `CompressedMeshletData` (flat byte buffer). ~50% size reduction.
- **Decode (CPU)**: Reference decoder for test validation. GPU decoder in T6b.4.2.
- **16-bit quantization**: `quantized = round((pos - aabbMin) / aabbExtent * 65535)`. Dequant = `pos = quantized / 65535 * aabbExtent + aabbMin`. Max error = `aabbExtent / 65535` per axis.
- **Octahedral normal**: Survey (2017) octahedral mapping. 2 bytes per normal, < 1° angular error.

### Downstream Consumers

- T6b.4.2 (Mesh Shader Decoder): reads `CompressedMeshlet` format in GPU.
- T6b.6.1 (.miki Archive): stores compressed meshlets.
- Phase 7a-1 (HLR): reads compressed meshlets for edge classification.

## Steps

- [ ] **Step 1**: Define CompressedMeshlet format + MeshletCompressor.h
      `[verify: compile]`
- [ ] **Step 2**: Implement CPU Encode + Decode
      `[verify: compile]`
- [ ] **Step 3**: Tests (compression ratio, decode correctness vs uncompressed)
      `[verify: test]`
