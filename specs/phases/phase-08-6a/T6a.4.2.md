# T6a.4.2 — Macro-Binning Compute — 3-Bucket Classify + Indirect Args

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: SceneBuffer + GPU Scene Submission
**Roadmap Ref**: `roadmap.md` L1744 — Macro-Binning (3 render buckets)
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.4.1 | GpuInstance + SceneBuffer | Complete | `SceneBuffer::GetGpuBuffer()`, `GpuInstance.flags/selectionMask` |
| T6a.2.1 | GpuPrefixSum + GpuCompact | Complete | `GpuCompact::Compact()` for visible list compaction |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/MacroBinning.h` | **public** | **M** | `MacroBinning::Create()`, `Classify()`, `GetBucketArgs()`, `GetBucketBuffer()` |
| `shaders/vgeo/macro_bin.slang` | internal | L | Per-instance classify compute (Opaque/Transparent/Wireframe) |
| `src/miki/vgeo/MacroBinning.cpp` | internal | L | Dispatch + indirect arg buffer management |

- **Error model**: `Create()` → `Result<MacroBinning>`. `Classify()` is void (GPU-side).
- **Thread safety**: stateful. Single-owner.
- **GPU constraints**: 3 buckets: Opaque=0, Transparent=1, Wireframe=2. Each bucket has `DrawMeshTasksIndirectCommand[]` + `countBuffer`. Classify uses `atomicAdd` on per-bucket counter SSBO.
- **Invariants**: sum(bucket counts) = visible instance count. Each instance appears in exactly 1 bucket.

### Downstream Consumers

- `MacroBinning.h` (**public**, heat **M**):
  - T6a.8.1 (Demo): calls `Classify()` then 3× `DrawMeshTasksIndirectCount()` using `GetBucketArgs()`
  - Phase 7a-1: Wireframe bucket feeds HLR edge pass
  - Phase 7a-2: Transparent bucket feeds OIT pass

### Upstream Contracts
- T6a.4.1: `SceneBuffer::GetGpuBuffer()` → `BufferHandle` containing `GpuInstance[]`
- T6a.2.1: `GpuCompact::Compact()` for compacting per-bucket instance lists
- T6a.0.1: `DrawMeshTasksIndirectCommand` struct layout (12B)

### Technical Direction
- **Atomic append per bucket**: compute shader reads `GpuInstance.flags` and `selectionMask` to determine bucket. Uses `atomicAdd` on per-bucket counter to get write offset. Writes instance index to per-bucket output buffer.
- **Indirect arg generation**: after classify, a small compute (1 workgroup) writes `DrawMeshTasksIndirectCommand` for each bucket: `{groupCountX = ceil(bucketCount / 32), 1, 1}` (32 meshlets per task workgroup).
- **Count buffer**: `uint32_t[3]` — read by `DrawMeshTasksIndirectCount.countBuffer` to avoid CPU readback.
- **Static frames optimization**: if no instance dirty flags set, skip classify and reuse previous frame's buckets.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/vgeo/MacroBinning.h` | **public** | **M** | Bucket classify + indirect args |
| Create | `shaders/vgeo/macro_bin.slang` | internal | L | Classify compute |
| Create | `src/miki/vgeo/MacroBinning.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_macro_binning.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define MacroBinning.h (heat M)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `MacroBinning::Create` | `(IDevice&, SlangCompiler&, uint32_t maxInstances) -> Result<MacroBinning>` | `[[nodiscard]]` static |
      | `MacroBinning::Classify` | `(ICommandBuffer&, BufferHandle sceneBuffer, uint32_t instanceCount) -> void` | — |
      | `MacroBinning::GetBucketArgs` | `(uint32_t bucketIndex) const noexcept -> BufferHandle` | `[[nodiscard]]` indirect arg buffer |
      | `MacroBinning::GetBucketCount` | `(uint32_t bucketIndex) const noexcept -> BufferHandle` | `[[nodiscard]]` count buffer for IndirectCount |
      | `MacroBinning::GetBucketInstanceBuffer` | `(uint32_t bucketIndex) const noexcept -> BufferHandle` | `[[nodiscard]]` instance index list |

      `[verify: compile]`

- [x] **Step 2**: Implement Slang shader + C++ dispatch
      `[verify: compile]`

- [x] **Step 3**: Unit tests
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(MacroBinning, AllOpaque)` | Positive | 100% opaque → bucket 0 = all, others empty | 2-3 |
| `TEST(MacroBinning, MixedBuckets)` | Positive | Mixed flags → correct bucket assignment | 2-3 |
| `TEST(MacroBinning, BucketCountsSum)` | Positive | sum(3 buckets) = total visible | 2-3 |
| `TEST(MacroBinning, IndirectArgsValid)` | Positive | DrawMeshTasksIndirectCommand groupCountX > 0 for non-empty bucket | 2-3 |
| `TEST(MacroBinning, EmptyScene)` | Boundary | 0 instances → all buckets empty | 2-3 |
| `TEST(MacroBinning, SingleInstance)` | Boundary | 1 instance → exactly 1 bucket non-empty | 2-3 |
| `TEST(MacroBinning, StaticFrameReuse)` | Positive | No dirty → classify skipped (verify no dispatch) | 2-3 |
| `TEST(MacroBinning, EndToEnd_ClassifyAndReadback)` | **Integration** | Upload instances → classify → readback bucket counts → verify | 2-3 |

## Design Decisions

- **Two-dispatch architecture**: (1) macroBinClassify per-instance atomic append, (2) macroBinArgGen writes DrawMeshTasksIndirectCommand. Simpler than single-dispatch with shared memory reduction.
- **Persistent zero buffer**: 12B CpuToGpu buffer with zeros, copied to counterBuffer each frame via CopyBuffer. Avoids per-frame allocation and is correct across all backends.
- **Counter buffer = count buffer**: The same uint32_t[3] serves both as atomic counters AND as the countBuffer for DrawMeshTasksIndirectCount (offset = bucket * 4).
- **DisplayStyle classification**: Wireframe/HLR/HLR_VisibleOnly → Wireframe bucket. XRay/Ghosted → Transparent. All others → Opaque. Matches rendering-pipeline-architecture.md §5.5.
- **Separate arg buffers per bucket** (not packed array): Simpler DrawMeshTasksIndirectCount calls — each bucket has its own 12B buffer at offset 0.
- **GpuCompact not used**: Task spec mentioned GpuCompact but atomic append achieves the same result with fewer dispatches. GpuCompact remains available for Phase 6b.

## Implementation Notes

Contract check: PASS — all 11 items verified.
10/10 tests pass (1 CreateValid + 4 positive + 3 boundary + 1 state + 1 integration).
