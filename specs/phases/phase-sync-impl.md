# Phase: Frame Sync Infrastructure Implementation

> **Status**: In Progress
> **Spec**: `specs/03-sync.md` (all 5 spec issues fixed)
> **Depends on**: Phase MW (multi-window), Timeline Semaphore Migration (complete)
> **Scope**: Implement all missing components from 03-sync.md spec

---

## Pre-conditions (already complete)

| Item | Status | Commit |
|------|--------|--------|
| Timeline semaphore migration | Done | timeline-semaphore-migration |
| FrameManager basic lifecycle | Done | existing |
| DeferredDestructor bin-based | Done | existing |
| FrameContext struct | Done | existing |
| I1: FrameManager reuses device global timeline | Done | 5ac9d03 |
| I2: EndFrame takes CommandBufferHandle | Done | 5ac9d03 |
| I5: SetSubmitSyncInfo injection | Done | 5ac9d03 |
| I3: StagingRing/ReadbackRing reclaim sites | Done (commented) | 5ac9d03 |
| 5 spec issues fixed | Done | 4f651e2 |

---

## Implementation Batches

### Batch 0: I4 — DeferredHandleType Completion
**Files**: `src/miki/frame/DeferredDestructor.cpp`, `include/miki/frame/DeferredDestructor.h`
**Effort**: ~30 min

Add missing handle types to DeferredDestructor:
- `Fence`, `Semaphore`, `AccelStruct`, `PipelineLayout`, `DescriptorLayout`, `PipelineCache`, `QueryPool`, `CommandBuffer`, `DeviceMemory`

Each needs: enum value + `Destroy()` overload + switch case in `ExecuteDestroy`.

---

### Batch 1: G2 — SyncScheduler
**Files**: `include/miki/frame/SyncScheduler.h`, `src/miki/frame/SyncScheduler.cpp`
**Effort**: ~2 hours
**Spec**: §13

Core cross-queue timeline dependency resolution. This is the foundation for G1, G3, G4, G8, G9, G12.

API (from spec §13.2):
```
AllocateSignal(QueueType) -> uint64_t
AddDependency(waitQueue, signalQueue, signalValue, waitStage)
GetPendingWaits(QueueType) -> span<TimelineSyncPoint>
GetSignalValue(QueueType) -> uint64_t
CommitSubmit(QueueType)
```

Internal state: `array<QueueState, 3>` where QueueState = `{SemaphoreHandle, nextValue, vector<SemaphoreSubmitInfo> pendingWaits}`.

Constructor takes `QueueTimelines` from `DeviceBase::GetQueueTimelines()`.

Key design points:
- Values are globally monotonic per queue (no per-window encoding)
- `AllocateSignal` atomicity: single-threaded (called only from main thread)
- `pendingWaits` cleared on `CommitSubmit`

---

### Batch 2: G3 — AsyncTaskManager
**Files**: `include/miki/frame/AsyncTaskManager.h`, `src/miki/frame/AsyncTaskManager.cpp`
**Effort**: ~2 hours
**Spec**: §5.6

Long-running compute tasks decoupled from frame lifecycle.

API:
```
Submit(CommandBufferHandle cmd, span<TimelineSyncPoint> waits) -> Result<AsyncTaskHandle>
IsComplete(AsyncTaskHandle) -> bool
GetCompletionPoint(AsyncTaskHandle) -> TimelineSyncPoint
WaitForCompletion(AsyncTaskHandle, timeout) -> Result<void>
Shutdown()
```

Uses `SyncScheduler::AllocateSignal(Compute)` for timeline values.
Stores `vector<TaskEntry>` with `{handle, completionPoint}`.
`IsComplete` polls via `DeviceHandle::GetSemaphoreValue()`.

---

### Batch 3: G1 — FrameOrchestrator
**Files**: `include/miki/frame/FrameOrchestrator.h`, `src/miki/frame/FrameOrchestrator.cpp`
**Effort**: ~2 hours
**Spec**: §2.1

Multi-window orchestration layer that owns:
- `SyncScheduler` (single instance, device-global)
- `AsyncTaskManager` (single instance)
- `DeferredDestructor` (single instance, global)
- Per-window `FrameManager` registry

API:
```
Create(DeviceHandle) -> Result<FrameOrchestrator>
GetSyncScheduler() -> SyncScheduler&
GetAsyncTaskManager() -> AsyncTaskManager&
GetDeferredDestructor() -> DeferredDestructor&
```

FrameOrchestrator does NOT own FrameManagers — SurfaceManager does. It provides shared services that FrameManagers reference.

---

### Batch 4: G4 — Split Submit
**Files**: Modify `src/miki/frame/FrameManager.cpp`
**Effort**: ~1.5 hours
**Spec**: §5.3

Change EndFrame to support split submit:
- Add `EndFrameSplit(span<SubmitBatch> batches)` where each batch has cmd buffers + signal intent
- Default `EndFrame` wraps everything in single batch (backward compatible)
- Each batch signals a SyncScheduler-allocated timeline value
- Last batch additionally signals renderDone binary sem + final timeline value

---

### Batch 5: G5 — StagingRing
**Files**: `include/miki/resource/StagingRing.h`, `src/miki/resource/StagingRing.cpp`
**Effort**: ~2 hours
**Spec**: §7.1

Ring buffer for CPU→GPU streaming uploads.

API:
```
Create(DeviceHandle, capacity=64MB) -> Result<StagingRing>
Allocate(size, align) -> StagingAllocation {cpuPtr, gpuOffset, bufferHandle}
AllocateBlocking(size, align) -> StagingAllocation  // may stall waiting for reclaim
FlushFrame(frameNumber)
ReclaimCompleted(completedFrame)
Capacity() -> uint64_t
```

Implementation: single persistent-mapped CpuToGpu buffer, monotonic write pointer, per-frame chunk tracking.

---

### Batch 6: G6 — ReadbackRing
**Files**: `include/miki/resource/ReadbackRing.h`, `src/miki/resource/ReadbackRing.cpp`
**Effort**: ~1.5 hours
**Spec**: §7.2

Same architecture as StagingRing but GpuToCpu memory.

API:
```
Create(DeviceHandle, capacity=16MB) -> Result<ReadbackRing>
RequestReadback(srcBuffer, offset, size) -> ReadbackFuture
ReclaimCompleted(completedFrame)
```

`ReadbackFuture::Resolve()` returns span of bytes after GPU completion.

---

### Batch 7: G7 — UploadManager
**Files**: `include/miki/resource/UploadManager.h`, `src/miki/resource/UploadManager.cpp`
**Effort**: ~1.5 hours
**Spec**: §7.1.1

4-tier upload routing:

| Size | Path | Mechanism |
|------|------|-----------|
| < 256KB | StagingRing | Zero-alloc ring |
| 256KB - 64MB | StagingRing large | May stall |
| > 64MB | Dedicated buffer | One-shot + deferred destroy |
| > 256MB + ReBAR | Direct VRAM | Zero copy |

---

### Batch 8: G8 + G9 — Timeout & Wait-Graph
**Files**: Modify `FrameManager.cpp`, add methods to `SyncScheduler`
**Effort**: ~2 hours
**Spec**: §12.3, §12.4

G8: Add 5-second timeout to BeginFrame's WaitSemaphore, with diagnostic dump on timeout.

G9: SyncScheduler methods:
```
DumpWaitGraph(FILE*)
ExportWaitGraphDOT(string&)
ExportWaitGraphJSON(string&)
```

Release mode: ring buffer of last 16 submits (~2KB).
Debug mode: full history with call stack hash.
Timeout trigger: DFS cycle detection on wait-graph adjacency.

---

### Batch 9: G10 + G11 + G12 — Low Priority
**Effort**: ~2 hours total

G10: Add `computeQueueFamilyCount`, `hasGlobalPriority` to `GpuCapabilityProfile`. Implement `DetectComputeQueueLevel()`.

G11: `AsyncTaskManager::SubmitBatched()` splits long command buffers into ≤2ms sub-batches with intermediate timeline signals.

G12: QFOT helper functions for 3-queue chain barrier insertion (Vulkan-only, D3D12 no-op).

---

## Execution Order & Dependencies

```
Batch 0 (I4) ──────────────────────────────────────────────→ independent
Batch 1 (G2 SyncScheduler) ───┬── Batch 2 (G3 AsyncTaskManager)
                               ├── Batch 3 (G1 FrameOrchestrator)
                               ├── Batch 4 (G4 Split Submit)
                               ├── Batch 8 (G8+G9 Timeout/WaitGraph)
                               └── Batch 9.3 (G12 QFOT)
Batch 5 (G5 StagingRing) ────── Batch 7 (G7 UploadManager)
Batch 6 (G6 ReadbackRing) ──── independent
Batch 9.1 (G10 ComputeQueueLevel) ── independent
Batch 9.2 (G11 Batch Splitting) ── depends on G3
```

**Critical path**: Batch 1 → Batch 2 → Batch 3 → Batch 4

**Parallel track A**: Batch 5 → Batch 7 (can be done in parallel with critical path)
**Parallel track B**: Batch 6 (independent)

---

## Verification Strategy

Each batch must:
1. Compile with zero errors/warnings
2. Existing tests pass (test_surface_integration, test_window_manager)
3. rhi_triangle_demo runs without regression
4. New unit tests for each component (minimum coverage below)

| Component | Min tests |
|-----------|-----------|
| SyncScheduler | 8 (allocate, dependency, commit, multi-queue, cycle detect) |
| AsyncTaskManager | 6 (submit, poll, wait, shutdown, multi-task) |
| FrameOrchestrator | 4 (create, get services, multi-window) |
| StagingRing | 6 (alloc, wrap, reclaim, blocking, capacity) |
| ReadbackRing | 5 (request, resolve, reclaim, capacity) |
| UploadManager | 4 (small, medium, large, rebar) |
| Wait-Graph | 3 (dump, dot, cycle detect) |
