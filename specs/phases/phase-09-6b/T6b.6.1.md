# T6b.6.1 — .miki Archive Format

**Phase**: 09-6b
**Component**: Cluster Streaming
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/MikiArchive.h` | **public** | **H** | `MikiArchive::Create()`, `Open()`, `ReadCluster()`, `WriteCluster()` |

- **Format**: Chunked archive with per-cluster LZ4 compression, octree page table for spatial indexing, random-access seek. Header: magic, version, cluster count, page table offset. Each cluster: compressed size + LZ4 payload.
- **API**: `Create(path)` for writing, `Open(path)` for reading. `ReadCluster(clusterId) -> span<byte>` decompresses on demand. `WriteCluster(clusterId, data)` compresses and appends.
- Per arch spec §5.6: `.miki` archive is the disk format for out-of-core streaming.

### Downstream Consumers

- T6b.6.2 (ChunkLoader): reads clusters from archive via async IO.
- Phase 8: CadScene serialization writes `.miki` archives.
- Phase 10: Point cloud streaming reuses archive format.

## Steps

- [ ] **Step 1**: Define MikiArchive.h + archive format spec
      `[verify: compile]`
- [ ] **Step 2**: Implement write + read + LZ4 compress/decompress
      `[verify: compile]`
- [ ] **Step 3**: Round-trip tests (write → read → compare)
      `[verify: test]`
