# T{id}.X.Y — {Task Name}

**Phase**: {nn}-{id} ({Phase Name})
**Component**: {Component Name}
**Roadmap Ref**: `roadmap.md` L{line} — {Component summary} (trace back to source deliverable)
**Status**: Not Started | In Progress | Complete
**Current Step**: — (updated during implementation to track cross-session resume point)
**Resume Hint**: — (optional: "Step 3: IFoo.h done, FooImpl.cpp in progress" — written by agent at session end)
**Effort**: S (< 1h) | M (1-2h) | L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T{id}.X.Z | ... | Complete / In Progress | `TypeName`, `FunctionName()` |

## Context Anchor

> Generated during Phase Planning (`/miki-phase` 1.2.3). This is the single
> source of direction for implementation — re-read before each Step.
>
> **Anchor Completeness**: all 4 subsections below must be non-placeholder.
> Verified by micro-check during planning. If marked `<!-- STALE -->`,
> re-generate before executing any Step.

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `IFoo.h` | **public** | **H** | `IFoo` interface — factory, core methods. Cross-phase contract. |
| `FooTypes.h` | **public** | **H** | `FooDesc`, `FooFlags`, `FooResult` — shared value types used by all consumers |
| `FooConfig.h` | **public** | **M** | `FooConfig` — optional config struct, used by advanced consumers only |
| `FooUtils.h` | shared | **M** | `ComputeHash()`, `ValidateDesc()` — helpers reused by same-Phase Tasks |
| `FooImpl.cpp` | internal | L | Implementation of `IFoo` — free to change |
| `FooVulkan.cpp` | internal | L | Vulkan-specific backend — free to change |

- **Error model**: `std::expected<T, ErrorCode>` — monadic chaining, no exceptions on hot path
- **Thread safety**: `IFoo` is single-owner; `FooTypes` are immutable value types (thread-safe by design)
- **GPU constraints**: `FooDesc` — `alignas(16)`, `static_assert(sizeof(FooDesc) == 32)`, explicit `_padding[2]`
- **Invariants**: `IFoo::Create()` returns valid instance or error — never null; `FooDesc::width > 0 && height > 0`

### Downstream Consumers

> Listed per-file. High-heat files have more consumers = more change cost.

- `IFoo.h` (**public**, heat **H**):
  - Phase X: {Component} — calls `IFoo::Create()` + `IFoo::Process()` for {purpose}
  - Phase Y: {Component} — extends `IFoo` via `IFooExtended` for {capability}
  - T{id}.Z.W (same Phase) — calls `IFoo::GetDesc()` for validation
- `FooTypes.h` (**public**, heat **H**):
  - Phase X, Y, Z: all consumers use `FooDesc` / `FooFlags` as parameter types
- `FooConfig.h` (**public**, heat **M**):
  - Phase Y only — reads `FooConfig` for advanced tuning
- `FooUtils.h` (shared, heat **M**):
  - T{id}.Z.W (same Phase) — calls `ComputeHash()` for resource keying

### Upstream Contracts
- T{id}.X.Z: provides `{type/API}` — verify actual signatures match before coding
  - Expected: `ClassName::Method(Args) -> Return`
  - Source: `include/miki/module/Header.h`

### Technical Direction
- {Key architectural principle from roadmap relevant to THIS Task}
  - e.g., injection-first: `IDevice` wraps externally-provided context, never creates its own
- {Implementation technique}
  - e.g., dynamic rendering (`VK_KHR_dynamic_rendering`), not `VkRenderPass`
- {Performance consideration}
  - e.g., bindless descriptor access via `BindlessIndex`, no per-draw descriptor set updates
- {Design pattern}
  - e.g., Pimpl for ABI stability on public headers, raw impl for internal

## Files

> A typical Task produces **3-8 files**. Order by Exposure (public first), then by Ref Heat (H first).

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/module/IFoo.h` | **public** | **H** | Core interface — 5+ downstream consumers across phases |
| Create | `include/miki/module/FooTypes.h` | **public** | **H** | Value types used as parameters by all IFoo consumers |
| Create | `include/miki/module/FooConfig.h` | **public** | **M** | Config struct — 1-2 advanced consumers only |
| Create | `src/miki/module/FooUtils.h` | shared | **M** | Helpers reused by 2 same-Phase Tasks |
| Create | `src/miki/module/FooImpl.cpp` | internal | L | IFoo implementation — no external consumers |
| Create | `src/miki/module/FooVulkan.cpp` | internal | L | Vulkan backend specialization |
| Modify | `tests/unit/test_foo.cpp` | internal | L | Unit tests |
| Modify | `CMakeLists.txt` | internal | L | Build integration |

> **Exposure levels** (what CAN be seen):
> - **public** — cross-Phase interface (`include/miki/`). Modification = breaking change.
>   Contract Verification required (see `/miki-task` §4).
> - **shared** — reused within same Phase by other Tasks. Modification needs consumer notification.
>   Contract Verification required.
> - **internal** — pure implementation detail. Free to change.
>   Contract Verification skipped.
>
> **Ref Heat** (how OFTEN it IS referenced — drives change cost awareness):
> - **H (High)** — referenced by 3+ consumers or across 2+ Phases.
>   Any signature change is expensive (ripple to many Tasks/Phases).
>   Agent must flag proposed changes and get user approval.
> - **M (Medium)** — referenced by 1-2 consumers in same or adjacent Phase.
>   Changes need consumer notification but are manageable.
> - **L (Low)** — no external consumers. Free to refactor.
>
> The combination of Exposure + Ref Heat tells the agent exactly how careful to be:
>
> | Exposure | Ref Heat | Agent behavior |
> |----------|----------|----------------|
> | **public** | **H** | Maximum caution. Full Expected API. Flag any deviation. |
> | **public** | **M** | Full Expected API. Notify known consumers on change. |
> | **public** | L | Rare (public but unused yet). Expected API, but change is cheap. |
> | shared | **H** | Treat like public — many same-Phase consumers. |
> | shared | **M** | Expected API. Notify consumer Tasks. |
> | shared | L | Functional description sufficient. |
> | internal | any | Agent has full freedom. No Expected API required. |

## Steps

> **Ordering principle**: implement high-heat public files first (they define the
> contract other files depend on), then medium-heat shared files, then internal
> impl files (batchable), then tests. This ensures the contract is locked early
> and internal code conforms to it.

- [ ] **Step 1**: Define core public interfaces (heat H)
      **Files**: `IFoo.h` (**public** H), `FooTypes.h` (**public** H)

      **Signatures** (`IFoo.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `IFoo::Create` | `(FooDesc) -> expected<unique_ptr<IFoo>, ErrorCode>` | `[[nodiscard]]` static |
      | `IFoo::Process` | `(span<const Input>) -> expected<Output, ErrorCode>` | `[[nodiscard]]` virtual pure |
      | `IFoo::GetDesc` | `() const noexcept -> FooDesc const&` | `[[nodiscard]]` virtual pure |
      | `IFoo::MaxSize` | `() -> size_t` | `[[nodiscard]]` static `consteval` |
      | `~IFoo` | virtual default | — |

      **Signatures** (`FooTypes.h`):

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `FooDesc` | `{ width:u32, height:u32, fmt:Format, flags:u32, _padding[2]:u32 }` | `alignas(16)` |
      | `FooFlags` | `enum class : u32 { None=0, Async=1<<0, Validate=1<<1 }` | — |
      | `FooResult` | `{ output:ResourceHandle, hash:u64 }` | — |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | `static_assert(sizeof(FooDesc))` | `== 32` |
      | Namespace | `miki::module` |
      | Error type | `ErrorCode` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [ ] **Step 2**: Define secondary public + shared headers (heat M)
      **Files**: `FooConfig.h` (**public** M), `FooUtils.h` (shared M)

      **Signatures** (`FooConfig.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `FooConfig` | `{ maxBatchSize:u32=1024, qualityFactor:f32=1.0, enableProfiling:bool=false }` | — |

      **Signatures** (`FooUtils.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `ComputeHash` | `(span<const byte>) -> u64` | `[[nodiscard]]` |
      | `ValidateDesc` | `(FooDesc const&) -> expected<void, ErrorCode>` | `[[nodiscard]]` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [ ] **Step 3**: Implement internal files (heat L — batchable with Step 2)
      **Files**: `FooImpl.cpp` (internal L), `FooVulkan.cpp` (internal L), `CMakeLists.txt` (internal L)
      *(No signatures required — internal files.)*
      Implement `IFoo` via Pimpl in `FooImpl.cpp`. Vulkan resource creation
      in `FooVulkan.cpp`. Register both in CMake target.
      **Acceptance**: compiles, `IFoo::Create()` returns valid instance
      `[verify: compile]`

- [ ] **Step 4**: Write unit tests
      **Files**: `test_foo.cpp` (internal L)
      *(No signatures required.)*
      Cover: factory success/failure, Process() valid/empty/invalid input,
      GetDesc() round-trip, ComputeHash() determinism, ValidateDesc() edges.
      **Acceptance**: all tests pass on both build paths
      `[verify: test]`

> **Expected API format** (compact signature table, NOT full code blocks):
>
> For public/shared files, list signatures as tables:
> - **Functions**: `| Symbol | Signature | Attrs |` — one row per function
> - **Structs**: `| Symbol | Fields | Attrs |` — one row per struct/enum
> - **Constraints**: `| Constraint | Value |` — `static_assert`, `alignas`, error type, namespace
>
> This keeps a 200-line header's contract in ~15-20 table rows instead of 200 lines of code.
> The agent writes the actual code during implementation, using the signature table as spec.
> Full code prototypes (```cpp blocks) are ONLY used when a signature is genuinely ambiguous
> (e.g., complex template specialization, CRTP pattern) — limit to 30 lines max.
>
> **Step detail rules** (driven by Exposure + Ref Heat):
>
> | Exposure + Heat | Signatures required? | Change protocol |
> |-----------------|---------------------|-----------------|
> | **public** H | Mandatory: full signature table + constraints | Flag any deviation to user |
> | **public** M | Mandatory: signature table | Notify known consumers |
> | **public** L | Mandatory: signature table (cheap to change) | Update Anchor if changed |
> | shared H | Mandatory: signature table | Treat like public |
> | shared M | Mandatory: signature table | Notify consumer Tasks |
> | shared L | Optional: functional desc sufficient | — |
> | internal any | Not required: functional desc only | Agent has full freedom |
>
> **Verification tags**:
> - `[verify: compile]` = build passes on both build paths
> - `[verify: test]` = specific test(s) pass
> - `[verify: visual]` = visual inspection or golden image diff
>   -> Use RenderDoc MCP: `renderdoc_capture_and_analyze` for GPU validation
> - `[verify: gpu-debug]` = GPU pipeline state / buffer inspection
>   -> Use RenderDoc MCP: `renderdoc_pipeline_state_at_eid`, `renderdoc_list_drawcalls`
> - `[verify: manual]` = manual check required (last resort)
> - `[verify: debug]` = runtime debugging needed
>   -> Use DebugMCP: `start_debugging`, `add_breakpoint`, `get_variables_values`

## Tests

> **Mandatory**: tests must cover all 5 categories from `.windsurfrules` §4.4.
> Minimum count: `N >= max(P * 3, 8)` where P = public API count.
> At least 1 `EndToEnd_*` integration test per component is **required**.

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(Module, CreateReturnsValid)` | Positive | factory produces valid instance | 1-3 |
| `TEST(Module, ProcessTransforms)` | Positive | correct output for known input | 2-3 |
| `TEST(Module, GetDescRoundTrip)` | Positive | GetDesc returns the desc passed to Create | 1-3 |
| `TEST(Module, HashDeterministic)` | Positive | ComputeHash same input -> same output | 2 |
| `TEST(Module, Process_EmptyInput)` | Boundary | empty span accepted without error | 2-3 |
| `TEST(Module, Process_MaxSizeInput)` | Boundary | maximum valid input size processed correctly | 2-3 |
| `TEST(Module, Create_ZeroWidth)` | Boundary | zero-valued desc field handled | 1-3 |
| `TEST(Module, Create_NullDevice)` | Error | invalid arg returns expected error code | 1-3 |
| `TEST(Module, Process_InvalidHandle)` | Error | destroyed resource handle returns error | 2-3 |
| `TEST(Module, ValidateDesc_BadFormat)` | Error | ValidateDesc rejects invalid format | 2 |
| `TEST(Module, MoveAssign_TransfersOwnership)` | State | move-assign leaves source empty, dest valid | 1-3 |
| `TEST(Module, DestroyedInstance_NoUseAfterFree)` | State | destructor cleans up, no dangling resources | 1-3 |
| `TEST(Module, EndToEnd_CreateProcessDestroy)` | **Integration** | full workflow: Create -> Configure -> Process -> verify output -> Destroy. Exercises the canonical usage pattern end-to-end. | 1-4 |

## Design Decisions

*(Why this design was chosen, trade-offs considered, alternatives rejected.
 Fill during implementation — not during planning.)*

## Implementation Notes

*(Issues encountered, workarounds, things to revisit.
 Contract check results recorded here after verification.)*
