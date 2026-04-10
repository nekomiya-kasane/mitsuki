---
description: Systematic test design for C++ modules — specification-driven, boundary-complete, with mutation mindset. Use when asked to add/review/harden tests for any component.
---

# TDD Test Design Workflow

> **Scope**: C++23 project, any module. Tests are the *executable specification*
> of system behavior and boundary conditions — not mere validation.
>
> **Philosophy**: Tests should be designed so that *any single behavioral change
> in the implementation causes at least one test to fail*. This is the mutation
> testing completeness criterion (Meta ACH, 2025).
>
> **When to invoke**: `/tdd-test-design <component>` where `<component>` is
> a class, module, or subsystem name (e.g. `FrameManager`, `StagingRing`).

---

## Phase 0 — Reconnaissance (do NOT write code yet)

// turbo
0.1. Read ALL public headers of `<component>` to extract the **API surface**:
     - Every public/protected method signature
     - Every struct/enum consumed or produced
     - Every documented precondition, postcondition, invariant
     - Error codes returned and their semantics

// turbo
0.2. Read the implementation to identify **internal state transitions**:
     - What is the state machine? (e.g. Uninitialized → Ready → InFrame → ...)
     - What operations are valid in each state?
     - What are the side effects of each transition?

0.3. Read existing tests to identify **coverage gaps**:
     - Which API methods have zero tests?
     - Which error paths are untested?
     - Which state transitions are untested?
     - Which boundary values are untested?
     - Are expectations precise (checking exact values) or weak (`has_value()` only)?

0.4. Produce a **Test Design Table** (output to user, do not create file):

     | # | API / Behavior | Category | Technique | Gap? |
     |---|----------------|----------|-----------|------|
     | 1 | `Create(valid)` | Positive | EP | Covered |
     | 2 | `Create(null device)` | Error | EP | Gap |
     | 3 | `BeginFrame → EndFrame` | State | STT | Weak |
     | ... | ... | ... | ... | ... |

     Categories (all 7 must be represented):
     - **Positive**: normal happy-path
     - **Boundary**: min/max/zero/overflow/off-by-one
     - **Error**: invalid input, resource exhaustion, wrong state
     - **State Transition**: valid and invalid transitions in the state machine
     - **Invariant**: properties that must hold across all operations
     - **Concurrency**: multi-thread, re-entrancy, ordering
     - **Integration**: multi-API workflows, cross-component interaction

     Techniques:
     - **EP** = Equivalence Partitioning
     - **BVA** = Boundary Value Analysis
     - **STT** = State Transition Testing
     - **PBT** = Property-Based Testing mindset (invariant assertions)
     - **MUT** = Mutation Testing mindset (would flipping a `<` to `<=` be caught?)
     - **STRESS** = adversarial multi-step scenario

---

## Phase 1 — Test Specification (still no code)

1.1. From the Test Design Table, produce a **Test Spec List**:
     each entry is a one-line test name + one-line behavioral assertion.

     Format: `TEST_ID: <TestName> — GIVEN <precondition> WHEN <action> THEN <precise_expectation>`

     Rules for expectations:
     - NEVER use bare `has_value()` / `EXPECT_TRUE(result)` as the only check
     - ALWAYS assert exact return values, exact state changes, exact side effects
     - For numeric values: assert exact value, not just > 0
     - For state: assert both the new state AND that old state resources are cleaned
     - For errors: assert exact error code, not just "is error"

1.2. Classify each test into priority:

     | Priority | Criterion |
     |----------|-----------|
     | **P0** | Catches a bug that would corrupt data or deadlock |
     | **P1** | Catches a bug in documented behavior contract |
     | **P2** | Catches a regression in edge case or performance contract |
     | **P3** | Catches a cosmetic or diagnostic regression |

1.3. Review the spec list for **mutation completeness**:
     For each branch / comparison in the implementation, verify at least one test
     would fail if that branch condition were inverted. If not, add a test.

1.4. Design **adversarial stress tests** (minimum 2):
     These test complex multi-step scenarios under harsh conditions:

     Patterns to use:
     - **Rapid state cycling**: create → use → destroy → create in tight loop
     - **Interleaved operations**: alternate between different API paths each iteration
     - **Resource exhaustion**: push to max capacity, then recover
     - **Order violation probing**: call APIs in unexpected but not invalid order
     - **Partial failure recovery**: inject failure mid-workflow, verify cleanup
     - **Timeline monotonicity**: verify monotonic counters never regress across N frames
     - **Cross-component cascade**: one component's edge case triggers another's boundary

1.5. Present the full Test Spec List to the user for review before implementation.
     User may add/remove/modify tests. Wait for approval.

---

## Phase 2 — Implementation

2.1. Write tests in order: P0 → P1 → P2 → P3.
     Each test must:
     - Use descriptive name: `<Category><Number>_<BriefDescription>`
     - Follow AAA pattern (Arrange-Act-Assert) with clear section separation
     - Assert **precise values**, never just truthiness
     - Document the behavioral contract in a comment if non-obvious
     - Be deterministic (no timing dependencies, no random without seed)

2.2. **Expectation precision rules** (non-negotiable):

     | Bad (reject) | Good (require) |
     |-------------|----------------|
     | `EXPECT_TRUE(r.has_value())` | `ASSERT_TRUE(r.has_value()); EXPECT_EQ(r->frameIndex, 0u);` |
     | `EXPECT_FALSE(r.has_value())` | `EXPECT_EQ(r.error(), ErrorCode::InvalidArgument)` |
     | `EXPECT_GT(v, 0)` | `EXPECT_EQ(v, 42u)` (if value is deterministic) |
     | `EXPECT_NE(h, Handle{})` | `EXPECT_TRUE(h.IsValid()); EXPECT_EQ(pool.Count(), 1u)` |

2.3. For each batch of 3-5 tests written:
// turbo
     Build:
```
cmake --build build --config Debug 2>&1 | Select-Object -Last 20
```
// turbo
     Run:
```
ctest --test-dir build --output-on-failure -R {TestFilter}
```

2.4. **On test failure** — apply the diagnostic protocol:

     2.4.1. Read the test assertion failure message carefully.
     2.4.2. Determine root cause category:

     | Category | Symptom | Action |
     |----------|---------|--------|
     | **Test Bug** | Test expectation is wrong | Fix test, document why |
     | **Impl Bug** | Implementation violates its own spec | Fix implementation |
     | **Spec Ambiguity** | Spec doesn't define this behavior | Ask user to clarify, then fix both |
     | **Environmental** | D3D12/GPU-specific, timing | Add skip guard or tolerance |

     2.4.3. NEVER silently weaken an assertion to make a test pass.
            If the expected value was wrong, explain WHY and update spec.
     2.4.4. If fixing implementation, verify ALL existing tests still pass.

---

## Phase 3 — Coverage Audit

3.1. Run all tests and confirm 100% pass rate (excluding known environmental skips).

3.2. Produce a **Coverage Summary Table**:

     | Category | Count | Target | Met? |
     |----------|-------|--------|------|
     | Positive | N | >= 1 per public API | |
     | Boundary | N | >= 1 per numeric param | |
     | Error | N | >= 1 per error code | |
     | State Transition | N | >= 1 per valid transition | |
     | Invariant | N | >= 2 per component | |
     | Concurrency | N | >= 1 if multi-thread | |
     | Integration/Stress | N | >= 2 | |
     | **Total** | N | >= max(APIs * 3, 15) | |

3.3. **Mutation spot-check**: pick 3 critical branches in the implementation.
     For each, mentally flip the condition and verify a test would catch it.
     If not, add the missing test.

3.4. Present final summary to user.

---

## Appendix A — Test Naming Convention

```
<Category><Number>_<BriefDescription>
```

| Prefix | Category |
|--------|----------|
| `POS` | Positive / happy path |
| `BND` | Boundary value |
| `ERR` | Error / invalid input |
| `STT` | State transition |
| `INV` | Invariant / property |
| `CON` | Concurrency |
| `INT` | Integration |
| `STR` | Stress / adversarial |

Example: `STR01_RapidCreateDestroyRecreate`, `BND03_MaxFramesInFlightClamped`

---

## Appendix B — Adversarial Test Patterns Reference

1. **N-Frame Pipeline Drain**: Run N frames, WaitAll, verify all resources reclaimed,
   counters at exact expected values, no leaks.

2. **Alternating Code Paths**: Alternate between two different API usage patterns
   every frame (e.g. single-batch vs multi-batch EndFrame). Verify timeline
   monotonicity and state consistency after each transition.

3. **Failure Injection Recovery**: Begin a frame, simulate a failure mid-way
   (e.g. acquire fails), verify the component recovers cleanly on the next frame
   with no state corruption.

4. **Max-Capacity Boundary**: Push all numeric limits to their maximum
   (e.g. kMaxFramesInFlight, kMaxSwapchainImages), run a complete workflow,
   verify correctness at the boundary.

5. **Move Semantics Stress**: Move-construct/assign the component mid-workflow,
   verify the moved-to instance preserves exact state and the moved-from is inert.

6. **Cross-Subsystem Cascade**: Exercise a workflow where component A's output
   is component B's input, under boundary conditions for both.

7. **Idempotency Proof**: Call the same operation N times and verify the result
   is identical to calling it once (where idempotency is expected).

8. **Monotonicity Proof**: Over a long sequence of operations, verify that
   monotonic quantities (timeline values, frame numbers) never decrease
   and increase by the exact expected delta.
