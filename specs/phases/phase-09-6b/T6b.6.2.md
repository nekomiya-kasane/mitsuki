# T6b.6.2 — ChunkLoader (Async IO + LZ4 Decompress + Staging Upload)

**Phase**: 09-6b
**Component**: Cluster Streaming
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.6.1 | .miki Archive Format | Not Started | `MikiArchive::ReadCluster()` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/ChunkLoader.h` | **public** | **H** | `ChunkLoader::Create()`, `RequestLoad()`, `IsResident()`, `Tick()` |

- **Architecture**: Async IO thread reads compressed clusters from `.miki` archive → LZ4 decompress → staging ring upload → GPU SSBO commit. Non-blocking: `RequestLoad()` enqueues, `Tick()` processes completed uploads.
- **Throughput target**: ≥ 2GB/s decompressed data (LZ4 ~4GB/s decode on modern CPU).
- **Integration**: Uses Phase 4 `StagingRing` for CPU→GPU transfers. Uses `PersistentDispatch` (T6b.5.1) for GPU-side decompression if available (stretch goal).

### Downstream Consumers

- T6b.6.3 (OctreeResidency): calls `RequestLoad()` for needed clusters, `IsResident()` for availability check.
- T6b.7.1 (Transfer Queue): ChunkLoader submits to dedicated transfer queue.
- Phase 8: CadScene streaming loads bodies via ChunkLoader.

## Steps

- [ ] **Step 1**: Define ChunkLoader.h
      `[verify: compile]`
- [ ] **Step 2**: Implement async IO + LZ4 decompress + staging upload pipeline
      `[verify: compile]`
- [ ] **Step 3**: Tests (load throughput, round-trip correctness, concurrent requests)
      `[verify: test]`
