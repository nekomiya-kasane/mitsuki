---
description: Bridge gaps between miki spec and implementation by importing reference code from D:\repos\miki, then refactor or rewrite to meet mitsuki standards
---

# miki Gap-Fill Workflow

> Invoked when the user identifies a gap between the mitsuki spec and the current implementation.
> The user describes **what** needs to be implemented (e.g. "WindowManager tree model",
> "SurfaceManager cascade destroy"). This workflow imports reference code from `D:\repos\miki`,
> evaluates it, and produces production-quality code in `c:\mitsuki`.

---

## 1. Reconnaissance — Reference Codebase

// turbo
1.1. Run `tree` on the four source directories of the reference codebase to understand layout:

```powershell
tree D:\repos\miki\include /F | Select-Object -First 120
tree D:\repos\miki\src /F | Select-Object -First 120
tree D:\repos\miki\demos /F | Select-Object -First 80
tree D:\repos\miki\tests /F | Select-Object -First 80
```

// turbo
1.2. Based on the user's gap description, identify relevant files in the reference codebase.
     Use `grep_search` and `code_search` scoped to `D:\repos\miki` to locate:
     - Headers (API surface)
     - Source files (implementation)
     - Tests (behavioral specs)
     - Demos (usage examples)

1.3. Read all identified files. Output a summary table:

| File (D:\repos\miki) | Purpose | Reuse Plan |
|-----------------------|---------|------------|
| `include/miki/platform/WindowManager.h` | Window tree API | Reuse names, refactor internals |
| ... | ... | ... |

---

## 2. Architecture Evaluation

2.1. Compare the reference code against:
     - The mitsuki spec (read relevant `specs/*.md` sections)
     - C++23 best practices (`.windsurfrules` §5)
     - Performance requirements (zero technical debt policy)

2.2. For each reference file, classify into one of three categories:

| Category | Criteria | Action |
|----------|----------|--------|
| **Reuse** | Architecture is correct, naming is good, only minor C++23 upgrades needed | Copy to mitsuki, apply minimal edits |
| **Refactor** | Core logic is sound but structure/patterns need modernization | Copy, then restructure (keep function/variable names where possible) |
| **Rewrite** | Architecture is wrong, has technical debt, or violates spec | Write from scratch using reference only for naming conventions |

2.3. Output the classification table for user review. **Wait for user approval before proceeding.**

---

## 3. Import & Adapt

3.1. Copy relevant files from `D:\repos\miki` to `c:\mitsuki` preserving directory structure:

```powershell
# Example — adjust paths per classification table
Copy-Item "D:\repos\miki\include\miki\platform\WindowManager.h" "c:\mitsuki\include\miki\platform\" -Force
```

3.2. For **Reuse** files:
     - Update `#include` paths if directory structure differs
     - Replace deprecated patterns with C++23 equivalents:
       - `std::optional` error returns → `std::expected<T, ErrorCode>`
       - Raw loops → `std::ranges` / `std::views`
       - `printf` / `iostream` → `std::format` / `std::print`
       - Missing `[[nodiscard]]` → add on all fallible returns
     - Preserve original function names, parameter names, variable names
     - Preserve original comments (Doxygen style)

3.3. For **Refactor** files:
     - Keep public API surface (class names, method names, parameter names)
     - Restructure internals (data layout, algorithms, error handling)
     - Add `static_assert(sizeof(...))` for GPU-facing structs
     - Add `alignas` where needed
     - Ensure `noexcept` correctness

3.4. For **Rewrite** files:
     - Use reference code ONLY as naming reference
     - Implement from spec + best practices
     - Match function/variable names from reference where they are descriptive and correct
     - Document why rewrite was necessary (brief comment at file top)

---

## 4. Build Integration

4.1. Update `CMakeLists.txt` files as needed:
     - Add new source files to existing targets
     - Create new targets if needed
     - Ensure `miki_copy_runtime_dlls` is called for any new executable targets

// turbo
4.2. Build verification:

```powershell
. .\setup_env.ps1; cmake --preset debug 2>&1 | Select-Object -Last 15
```

// turbo
4.3. Compile:

```powershell
cmake --build build/debug 2>&1 | Select-Object -Last 30
```

4.4. Fix all compilation errors. Do NOT use workarounds — fix root causes.

---

## 5. Test Integration

5.1. Copy or adapt tests from `D:\repos\miki\tests` for the imported code.

5.2. Ensure tests follow mitsuki conventions:
     - `TEST(ModuleName, SpecificBehavior)` naming
     - Cover: Positive, Boundary, Error, State, Integration
     - Linked against `gtest` / `gtest_main` via `miki::third_party::gtest`

// turbo
5.3. Run tests:

```powershell
ctest --test-dir build/debug --output-on-failure -R {TestFilter}
```

5.4. All tests must pass. Zero regressions on existing tests.

---

## 6. Quality Gate

6.1. Verify no technical debt introduced:

// turbo
```powershell
grep -r "TODO\|FIXME\|HACK\|STUB\|placeholder" c:\mitsuki\include c:\mitsuki\src --include="*.h" --include="*.cpp" | Select-Object -First 20
```

6.2. Verify naming consistency with reference:
     - Public API names match reference where architecturally sound
     - Internal names may differ if reference had poor naming

6.3. Output a final diff summary to the user:

| Item | Reference (D:\repos\miki) | mitsuki (c:\mitsuki) | Status |
|------|---------------------------|----------------------|--------|
| `WindowManager::CreateWindow` | Present | Present (reused) | OK |
| `WindowNode::nativeToken` | `void*` | `void*` | OK |
| `MultiWindowManager` | Present (deprecated) | Removed | Rewritten as WindowManager+SurfaceManager |

6.4. Report completion to user.
