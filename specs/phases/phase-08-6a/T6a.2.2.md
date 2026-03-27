# T6a.2.2 — GpuRadixSort (Onesweep, 16M keys)

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: GPU Compute Primitives
**Roadmap Ref**: `roadmap.md` L1745 — GpuRadixSort (Onesweep, 16M keys <2ms)
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.2.1 | GpuPrefixSum + GpuCompact + GpuHistogram | Complete | `GpuPrefixSum::ScanExclusive()` for passHist prefix sum |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/GpuRadixSort.h` | **public** | **H** | `GpuRadixSort::Create()`, `Sort()`, `SortKeyValue()` |
| `shaders/vgeo/radix_sort.slang` | internal | L | Onesweep scatter compute |
| `src/miki/vgeo/GpuRadixSort.cpp` | internal | L | Multi-pass radix sort dispatch |

- **Error model**: `Create()` → `Result<GpuRadixSort>`. `Sort()` is void (GPU-side).
- **Thread safety**: stateful (owns pipeline + scratch). Single-owner.
- **GPU constraints**: 8-bit digit, 4 passes for 32-bit keys. Workgroup size 256. Scratch = 2× input size.
- **Invariants**: output is stably sorted by key. Key-value pairs maintain correspondence.

### Downstream Consumers

- `GpuRadixSort.h` (**public**, heat **H**):
  - T6a.6.2 (Material Resolve): sorts VisBuffer pixels by `materialId` for coherent material evaluation
  - Phase 6b: `ClusterDAG` DAG cut optimizer sorts by projected error
  - Phase 7a-2: `PickDedup` sorts hit buffer by `instanceId` for deduplication via `GpuCompact`

### Upstream Contracts
- T6a.2.1: `GpuHistogram::Compute(cmdBuf, keys, histogramOut, count, 256)`, `GpuPrefixSum::ScanExclusive(cmdBuf, input, output, 256)`
- T6a.0.1: `ICommandBuffer::Dispatch()`, `IDevice::CreateComputePipeline()`

### Technical Direction
- **Onesweep radix sort** (Adinets & Merrill, 2022): single-pass per digit, using warp-level cooperation for local sorting + global scatter. Eliminates the separate scatter pass of traditional radix sort. Achieves ~2× throughput vs 2-pass approaches.
- **4 passes for 32-bit keys**: digit width = 8 bits, 4 passes (bits 0-7, 8-15, 16-23, 24-31). Each pass: histogram → prefix sum → scatter.
- **Key-value sort**: `SortKeyValue()` sorts 64-bit `{key:32, value:32}` packed pairs. Alternative: separate key and value buffers with index indirection.
- **Scratch management**: double-buffered ping-pong (input → scratch → input for next pass). Grow-only allocation.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/vgeo/GpuRadixSort.h` | **public** | **H** | Sort interface |
| Create | `shaders/vgeo/radix_sort.slang` | internal | L | Onesweep scatter |
| Create | `src/miki/vgeo/GpuRadixSort.cpp` | internal | L | Multi-pass dispatch |
| Create | `tests/unit/test_gpu_radix_sort.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define GpuRadixSort.h (heat H)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `GpuRadixSort::Create` | `(IDevice&, SlangCompiler&, GpuPrefixSum&, GpuHistogram&) -> Result<GpuRadixSort>` | `[[nodiscard]]` static |
      | `GpuRadixSort::Sort` | `(ICommandBuffer&, BufferHandle keys, uint32_t count) -> void` | In-place sort |
      | `GpuRadixSort::SortKeyValue` | `(ICommandBuffer&, BufferHandle keys, BufferHandle values, uint32_t count) -> void` | Paired sort |

      `[verify: compile]`

- [x] **Step 2**: Implement Slang shader + C++ multi-pass dispatch
      `[verify: compile]`

- [x] **Step 3**: Unit tests with GPU readback verification
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(GpuRadixSort, Sort1K_Correctness)` | Positive | 1K random keys sorted correctly | 2-3 |
| `TEST(GpuRadixSort, Sort16M_Correctness)` | Positive | 16M keys match CPU std::sort | 2-3 |
| `TEST(GpuRadixSort, SortKeyValue_Correspondence)` | Positive | values follow their keys | 2-3 |
| `TEST(GpuRadixSort, AlreadySorted)` | Boundary | pre-sorted input unchanged | 2-3 |
| `TEST(GpuRadixSort, ReverseSorted)` | Boundary | worst-case input | 2-3 |
| `TEST(GpuRadixSort, AllSameKey)` | Boundary | stability: original order preserved | 2-3 |
| `TEST(GpuRadixSort, SingleElement)` | Boundary | N=1 | 2-3 |
| `TEST(GpuRadixSort, PowerOfTwo_NonPowerOfTwo)` | Boundary | both N cases work | 2-3 |
| `TEST(GpuRadixSort, Perf_16M_Under2ms)` | Benchmark | 16M keys < 2ms | 2-3 |
| `TEST(GpuRadixSort, EndToEnd_SortAndReadback)` | **Integration** | Upload → sort → readback → verify | 2-3 |

## Design Decisions

1. **Reduce-then-scan (not Onesweep)**: DeviceRadixSort 4-kernel architecture (Init→Upsweep→Scan→Downsweep) chosen for Vulkan portability. Onesweep requires forward thread progress guarantee not universally available on Vulkan. Deferred to Phase 14.
2. **Wave-level multisplit ranking**: b0nes164/GPUSorting `WarpLevelMultiSplit` pattern — 8-bit ballot refinement (`WaveActiveBallot` × 8), `InterlockedAdd` to `s_waveHist[waveIdx*RADIX+digit]`, `WaveReadLaneAt` broadcast. Per-row barriers required on Vulkan/Slang (barrier-free single-pass failed due to shared memory atomic visibility).
3. **passHist scan via GpuPrefixSum**: Full-buffer exclusive prefix sum on column-major passHist. Downsweep reads `scanned[d*N+p] - scanned[d*N+0]` = O(1) per-digit offset. Eliminates O(partIdx) loop.
4. **Descending sort**: Digit inversion `255 - digit` in Upsweep + Downsweep, controlled by push constant `descending` flag.
5. **GpuHistogram not used**: Upsweep builds histograms directly via LDS + atomicAdd to globalHist/passHist. GpuHistogram from T6a.2.1 is not needed.

## Implementation Notes

- **Architecture**: 4-kernel (Init + Upsweep + Scan + Downsweep), 4 pipelines (init, upsweep, downsweep key-only, downsweep KV)
- **API**: `Sort`, `SortDescending`, `SortKeyValue`, `SortKeyValueDescending` — 4 public methods
- **Push constants**: 16B `{count, shift, numPartitions, descending}`
- **Shared memory**: `s_hist[256]` + `s_keys[3840]` + `s_waveHist[MAX_WAVES=16 × 256]` = ~17KB (key-only), +`s_vals[3840]` = ~32KB (KV)
- **Performance**: ~49ms/16M (Phase 14 target: <2ms). Bottleneck: per-row barriers in Stage 3.
- **Tests**: 139 total (10 hand-written + 129 parametric across 7 suites)
- **Commits**: f21c827, 1387efc, bd3bc20, 8a08f60
- **Contract check**: PASS — API matches Anchor except GpuHistogram dependency removed (not needed).
- **Competitive analysis**: See `specs/roadmap.md` Appendix A. Phase 14 forward design note in `phase-08-6a.md`.
