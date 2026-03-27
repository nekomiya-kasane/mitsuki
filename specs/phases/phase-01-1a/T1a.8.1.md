# T1a.8.1 — MockDevice (Call Sequence Tracking, Lifecycle Validation)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: Mock Backend
**Roadmap Ref**: `roadmap.md` L331 — `MockDevice` CPU-side mock for no-GPU CI
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.2 | IDevice + ICommandBuffer | Complete | `IDevice`, `ICommandBuffer` interfaces |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/mock/MockDevice.h` | shared | **M** | `MockDevice` — `IDevice` impl for testing. Tracks call sequences, validates resource lifecycle. |
| `src/miki/rhi/mock/MockDevice.cpp` | internal | L | Implementation: in-memory handle tracking, call logging |
| `src/miki/rhi/mock/MockCommandBuffer.h` | shared | L | `MockCommandBuffer` — records commands for verification |
| `tests/unit/test_mock_device.cpp` | internal | L | MockDevice self-tests |

- **Error model**: `Result<T>` for all IDevice methods. Mock can simulate errors via configuration.
- **Thread safety**: Single-owner like real devices.
- **GPU constraints**: None — pure CPU implementation.
- **Invariants**: MockDevice tracks all resource creates/destroys. Resource leak detection on `Destroy()`. Call sequence validation (Begin before End, etc.).

### Downstream Consumers

- `MockDevice.h` (shared, heat **M**):
  - All test files in Phase 1a: Use MockDevice for RHI interface tests without GPU
  - Phase 1b+: Continue using MockDevice for CI testing

### Upstream Contracts

- T1a.3.2: `IDevice` interface (all virtual methods), `ICommandBuffer` interface

### Technical Direction

- **Test mock**: Implements all `IDevice` methods with in-memory tracking. No GPU calls.
- **Call sequence validation**: Records API call order. Verifies: Begin before End, resource created before used, resource destroyed after last use.
- **Resource leak detection**: Tracks all allocated handles. On MockDevice destruction, asserts all handles freed.
- **Error simulation**: `MockDevice::SimulateError(ErrorCode)` — next fallible call returns this error. For testing error paths.
- **Capability**: `CapabilityTier::Tier1_Full` with all features enabled (for testing full API surface).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/mock/MockDevice.h` | shared | **M** | Test mock device |
| Create | `src/miki/rhi/mock/MockDevice.cpp` | internal | L | Implementation |
| Create | `src/miki/rhi/mock/MockCommandBuffer.h` | shared | L | Command recording mock |
| Create | `src/miki/rhi/mock/MockCommandBuffer.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_mock_device.cpp` | internal | L | Self-tests |

## Steps

- [x] **Step 1**: MockDevice + MockCommandBuffer classes
      **Files**: `MockDevice.h` (shared M), `MockDevice.cpp` (internal L), `MockCommandBuffer.h` (shared L), `MockCommandBuffer.cpp` (internal L)
      Implement all `IDevice` methods with handle tracking (flat_map<uint64_t, ResourceInfo>). `MockCommandBuffer` records commands into `vector<MockCommand>`. Call sequence validation. Resource leak detection on destroy.
      **Acceptance**: compiles; all IDevice methods callable
      `[verify: compile]`

- [x] **Step 2**: Error simulation + unit tests
      **Files**: `tests/unit/test_mock_device.cpp` (internal L)
      Add `SimulateError` API. Tests: resource create/destroy lifecycle, leak detection (assert on undestroyed resources), call sequence violation detection, error simulation, capability profile.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(MockDevice, CreateReturnsValid)` | Unit | Step 1 — basic creation | 1 |
| `TEST(MockDevice, ResourceLifecycle)` | Unit | Step 1 — create + destroy | 1 |
| `TEST(MockDevice, LeakDetection)` | Unit | Step 1 — undestroyed resource detected | 1 |
| `TEST(MockDevice, DoubleDestroyIsSafe)` | Unit | Step 1 — double destroy no crash | 1 |
| `TEST(MockDevice, DestroyInvalidHandleIsSafe)` | Unit | Step 1 — invalid handle no crash | 1 |
| `TEST(MockDevice, SimulateError)` | Unit | Step 2 — injected error returned | 2 |
| `TEST(MockDevice, SimulateErrorOnSubmit)` | Unit | Step 2 — error on Submit | 2 |
| `TEST(MockDevice, SimulateErrorOnCreateCommandBuffer)` | Unit | Step 2 — error on CreateCmdBuf | 2 |
| `TEST(MockDevice, ImportSwapchainImage)` | Unit | Step 1 — swapchain import | 1 |
| `TEST(MockDevice, WaitIdleNoOp)` | Unit | Step 1 — WaitIdle no crash | 1 |
| `TEST(MockDevice, CapabilityProfile)` | Unit | Step 1 — full tier + features | 1 |
| `TEST(MockDevice, CallSequenceValidation)` | Unit | Step 1 — Begin before End | 1 |
| `TEST(MockCommandBuffer, ResetClearsState)` | Unit | Step 1 — reset clears commands | 1 |
| `TEST(MockCommandBuffer, AllCommandTypesRecorded)` | Unit | Step 1 — all 16 types | 1 |

## Design Decisions

- **Handle counter + set**: `nextHandle_` monotonically increments; `liveResources_` (std::set) tracks live handles. Simple, deterministic, O(log N).
- **Leak check via assert**: Destructor asserts if live resources remain. Configurable via `SetLeakCheckEnabled()` so tests that intentionally leak can disable it.
- **Single-shot error injection**: `SimulateError()` sets a pending error consumed by the next fallible call. Simple and sufficient for testing error paths.
- **No DestroySampler**: `IDevice` interface lacks `DestroySampler()`, so sampler handles leak in MockDevice. Tracked as known gap.
- **MockCommandBuffer as separate lib**: `miki_rhi_mock` is a standalone STATIC lib, not linked into `miki_rhi`. Tests link both explicitly.

## Implementation Notes

- Contract check: PASS. All IDevice + ICommandBuffer virtual methods implemented.
- 14 tests total, all passing on both `debug-d3d12` and `debug-vulkan` build paths.
- MockDevice is pure CPU — no GPU calls, no platform-specific code, works on all platforms.
- `MockCommandBuffer` records 16 command types via `MockCommandType` enum.
