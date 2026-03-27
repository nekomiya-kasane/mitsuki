# T6b.4.3 — Compression Ratio + Decode Correctness Tests

**Phase**: 09-6b
**Component**: Meshlet Compression
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (1h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.4.2 | Mesh Shader Decoder | Not Started | GPU decode path |

## Context Anchor

Comprehensive test suite: compression ratio ≥ 50%, CPU decode == GPU decode (readback), position error within quantization tolerance, normal angular error < 1°.

## Steps

- [ ] **Step 1**: Compression ratio + decode correctness tests
      `[verify: test]`
