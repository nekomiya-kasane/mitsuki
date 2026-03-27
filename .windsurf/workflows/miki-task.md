---
description: How to execute a single Task within a miki renderer phase
---

# miki Task Workflow (Execution Layer)

> Invoked by `/miki-roadmap` §3 when starting a new Task.
> Operates on a Task file: `specs/phases/phase-{nn}-{id}/T{id}.X.Y.md`
> (see `/miki-roadmap` §8 for `{nn}`/`{id}` naming rules)
>
> **Position in hierarchy**:
> ```
> /miki-roadmap  → session entry, execute Tasks, gate check, session end
> /miki-phase    → phase planning ONLY
> /miki-task     → (this) Task execution: Steps, code, tests, verification
> ```
>
> **Task file templates**:
> - Standard: `specs/templates/task-template.md`
> - Cooldown: `specs/templates/cooldown-task-template.md`
> - Tech Spike: `specs/templates/spike-task-template.md`

---

## 1. Task Startup

// turbo
1.1. Read the Task file `specs/phases/phase-{nn}-{id}/T{id}.X.Y.md`
     - If `**Resume Hint**` is non-empty, this is a **cross-session resume**.
       Use the hint to skip already-completed files within the current Step.
// turbo
1.2. Read the parent phase spec to confirm:
     - Task dependencies are satisfied (all dep Tasks = Complete)
     - No blocking issues from previous Tasks
     - Read the **Roadmap Digest** section for phase-level context
       (this replaces reading the full roadmap section — saves ~2-4K tokens)
// turbo
1.3. Read relevant source files listed in the Task's "Files" section
1.4. Set `todo_list` items = Task's Steps (one todo per Step)
1.5. **Detect Task type** — check the Task file header:
     - If `**Phase**` contains `Cooldown`: this is a **cooldown task**.
       Skip Context Anchor re-reads (§2.2 first bullet) and Contract
       Verification (§4). Use Acceptance Criteria from the task file instead.
     - If `**Phase**` contains `Tech Spike`: this is a **spike task**.
       Skip Contract Verification (§4). Decision Gate replaces it.
     - Otherwise: standard task — full workflow applies.

1.6. **Self-summary** (attention anchoring): Before writing any code, generate
     a 2-3 sentence summary of:
     - What this Task must accomplish
     - Which APIs / types it produces or consumes
     - Key constraints (GPU alignment, thread safety, error handling)

---

## 2. Implementation Loop (per Step)

2.1. Mark current Step as `in_progress` in `todo_list`

2.2. Implement the Step:
     - **[Standard tasks only]** Re-read this Task's **Context Anchor**
       section (~20-40 lines) to refresh direction before writing code.
       Cooldown/spike tasks: re-read **Scope** or **Spike Objective** instead.
     - Write code (follow `.windsurfrules` §5 coding standards)
     - Verify: API names, signatures, error model, threading match the Anchor
     - Write corresponding test if Step includes a test.
       **Test writing protocol** (per `.windsurfrules` §4.4):
       - Cover all 5 categories: Positive, Boundary, Error, State, Integration
       - Minimum count: `N >= max(P * 3, 8)` where P = public API count
       - At least 1 `EndToEnd_*` integration test per component (exercises
         a complete multi-API workflow as canonical usage pattern)
       - After writing tests, run the **test completeness self-check** (§4.4 table)
     - Each Step must leave the codebase in a compilable state
     - **[CRITICAL] Pre-save compliance check** — before finalizing each file,
       re-verify against `.windsurfrules` §5 C++23 paradigms:
       - `std::expected` not exceptions on hot paths?
       - `[[nodiscard]]` on all fallible returns?
       - `std::ranges` / `std::views` over raw loops?
       - `std::format` / `std::print` over iostream?
       - `alignas(16)` + `static_assert` + explicit padding on GPU structs?
       This counters attention decay on coding standards in long sessions.

2.3. **Step verification** — execute based on the `[verify:]` tag:

     | Tag | Action |
     |-----|--------|
     | `[verify: compile]` | Build both paths. If consecutive compile-only Steps, batch them (see 2.5) |
     | `[verify: test]` | Build + run specific test filter |
     | `[verify: visual]` | Use **RenderDoc MCP**: `renderdoc_capture_and_analyze` on demo executable, inspect draw calls + pipeline state. Report summary. |
     | `[verify: gpu-debug]` | Use **RenderDoc MCP**: `renderdoc_pipeline_state_at_eid`, `renderdoc_list_drawcalls`, `renderdoc_search_resource` to inspect GPU state |
     | `[verify: debug]` | Use **DebugMCP**: `start_debugging` + `add_breakpoint` + `get_variables_values` to inspect runtime state |
     | `[verify: manual]` | Pause and ask user to verify. Describe what to check. |

// turbo
     Build command:
```
cmake --build build --config Debug 2>&1 | Select-Object -Last 20
```
// turbo
     Test command:
```
ctest --test-dir build --output-on-failure -R {TestFilter}
```

2.4. On Step completion:
     - Check off `[x]` in Task file
     - Update `**Current Step**` field in Task file header to next Step number
     - Mark `completed` in `todo_list`
     - Move to next Step

2.5. **Step Batch Processing** — when 2-3 consecutive Steps ALL have
     `[verify: compile]` AND operate on the same file group:
     - Implement all batched Steps before running a single build verification
     - This reduces build cycles by ~30%
     - Do NOT batch across different Components or across `[verify: test]` boundaries

---

## 3. Task Verification

// turbo
3.1. All Steps checked off `[x]` in Task file
3.2. Build passes:
```
cmake --build build --config Debug 2>&1 | Select-Object -Last 20
```
// turbo
3.3. Tests pass:
```
ctest --test-dir build --output-on-failure -R {TestFilter}
```
// turbo
3.4. No TODO/stub/placeholder in Task's files:
```
grep_search {task_source_dir} "TODO|STUB|FIXME|placeholder"
```

---

## 4. Contract Verification (post-implementation)

> **Scope gate**: Skip this entire section if ANY of:
> - Task is a **cooldown task** (no Contract, no Anchor)
> - Task is a **spike task** (no Contract; Decision Gate in task file replaces this)
> - Task has only **internal** Exposure files (no public/shared contract to verify)
>
> If skipped, proceed directly to Section 5.

<!-- CHECKPOINT: must output Contract Verification diff table -->

// turbo
4.1. Re-read this Task's **Context Anchor** section.

4.2. **Contract check** — for each `Expected API` in this Task's Steps,
     output an **expected vs actual diff table**:

     | # | Item | Expected (from Anchor) | Actual (from code) | Match? |
     |---|------|----------------------|-------------------|--------|
     | 1 | `IFoo::Create` return type | `expected<unique_ptr<IFoo>, ErrorCode>` | `expected<unique_ptr<IFoo>, ErrorCode>` | PASS |
     | 2 | `FooDesc` sizeof | 12 | 16 | **FAIL** |
     | 3 | `[[nodiscard]]` on Create | required | present | PASS |

     Per-signature checklist:
     - Function/type name matches exactly
     - Parameter types + order match
     - Return type matches (including `expected<T,E>` error type)
     - `[[nodiscard]]` / `noexcept` / `explicit` present where specified
     - `static_assert(sizeof(...))` value matches

     Any FAIL → fix code or update Anchor (with user approval).

4.3. **Consumer check** — for each Downstream Consumer in Anchor:
     verify the implemented API satisfies that consumer's stated need.

4.4. **Direction check** — for each Technical Direction item:
     verify implementation follows the stated approach.

4.5. **Verdict**:
     - **PASS**: record "Contract check: PASS" in Task file's Implementation Notes.
     - **FAIL**: list mismatches, fix code, re-run §3 + §4 until PASS.
     - **AMBIGUOUS**: record as questions in Implementation Notes for user.

---

## 5. Task Completion

5.1. Set Task status to `Complete` in Task file header
5.2. Update parent phase spec: mark Task row as `[x]`
5.3. Check if this is the last Task in its Component:
     - If yes: mark Component `[DONE]` in `roadmap.md`
5.4. **Pipeline viz T0 sync** (mandatory):
     - For each pass implemented/modified in this Task:
       (a) Add `"T0"` to the pass's `tier` array in `demos/pipeline-viz/src/data/pipelineData.ts`
       (b) Set `implStatus: "implemented"`, add `implPhase`, add `sourceFiles[]`
       (c) Add the pass to `OverviewPanel TIER_FLOWS.T0.steps` if not already present
     - This ensures T0 (current implementation) is always accurate in the viz.
5.5. **Harness feedback** (per `.windsurfrules` §10.2):
     - If repeated errors were encountered during this Task, append to `.windsurf/pitfalls.md`
     - If cross-module or recurrent, also promote to `.windsurfrules` §9
5.6. Return to `/miki-roadmap` §3 (auto-advance rules apply)
