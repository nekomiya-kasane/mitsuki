# T6b.7.1 — Transfer Queue + Timeline Semaphore

**Phase**: 09-6b
**Component**: Zero-Stall Async Streaming
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6b.6.2 | ChunkLoader | Not Started | Submits upload work to transfer queue |

## Context Anchor

### This Task's Contract

**Produces**: Vulkan 1.4 dedicated transfer queue integration for ChunkLoader. Timeline semaphore synchronization between transfer and graphics queues.

- **Architecture**: Transfer queue runs continuously, independent of graphics queue. ChunkLoader submits `vkCmdCopyBuffer` on transfer queue. Timeline semaphore signals when upload complete → graphics queue waits before reading.
- **Zero-stall guarantee**: Graphics queue NEVER waits for transfer. If cluster not yet uploaded, render coarser ancestor.
- Per arch spec §5.8.1: "3-queue streaming architecture to eliminate stalls entirely".

### Downstream Consumers

- T6b.7.2 (Predictive Prefetch): submits prefetch requests to transfer queue.
- Phase 14: extends with DirectStorage bypass (NVMe → GPU direct).

## Steps

- [ ] **Step 1**: Implement transfer queue submission + timeline semaphore sync
      `[verify: compile]`
- [ ] **Step 2**: Tests (async upload completes, graphics queue reads correct data)
      `[verify: test]`
