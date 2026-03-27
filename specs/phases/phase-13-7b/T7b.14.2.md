# T7b.14.2 — Atlas BC7 Compression (4:1) for Large PMI Assemblies

**Phase**: 13-7b
**Component**: PMI RichText Prerequisites
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.14.1 | RichTextSpan Extensions | Not Started |

## Scope

BC7 texture compression for MSDF glyph atlas (4:1 ratio, 16MB→4MB) to handle large PMI-heavy assemblies with 64+ atlas pages. Offline compression at atlas build time. GPU reads BC7 directly (hardware decompression). Quality: < 0.5 dB PSNR loss vs uncompressed MSDF — imperceptible for SDF rendering.

## Steps

- [ ] **Step 1**: Implement BC7 atlas compression pipeline (offline + GPU upload)
      `[verify: compile]`
- [ ] **Step 2**: Tests (BC7 quality vs uncompressed, text rendering fidelity)
      `[verify: test]`
