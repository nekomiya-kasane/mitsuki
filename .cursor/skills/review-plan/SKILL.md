---
name: review-plan
description: >-
  Review and expand a task/phase plan by researching state-of-the-art techniques,
  benchmarking against industry leaders, and verifying architectural alignment with
  the rendering pipeline spec. Use when the user says "review plan", "expand plan",
  "review-plan", asks to verify a plan against best practices, or wants to ensure
  a phase/task plan achieves optimal performance and architecture.
---

# Review Plan

Review, expand, and harden a task or phase plan before execution.
Ensures the plan targets best-in-class performance, correct architecture,
and comprehensive feature coverage by researching competing engines and
validating against `specs/rendering-pipeline-architecture.md`.

---

## Procedure

### Phase 1: Identify Target

1. Determine which plan to review:
   - A phase spec (`specs/phases/phase-{nn}-{id}.md`)
   - A task file (`specs/phases/phase-{nn}-{id}/T{id}.X.Y.md`)
   - A roadmap section (`specs/roadmap.md`, specific phase heading)
   - Or inline plan text provided by the user
2. Read the target plan fully.
3. Read the parent phase spec and relevant roadmap section for context.

### Phase 2: Research State-of-the-Art

For each major technical area in the plan, research current best practices:

**2.1 Identify key technical domains** in the plan (e.g., GPU culling,
shadow mapping, OIT, tessellation, material system, etc.).

**2.2 Benchmark against industry references** — for each domain, compare
the plan's approach against:

| Reference | Focus |
|-----------|-------|
| UE5 (Nanite, Lumen, VSM) | GPU-driven geometry, GI, virtual shadows |
| Unity 6 (SRP, GPU Resident Drawer) | Render graph, batching, SRP customization |
| Filament (Google) | Mobile PBR, clustered forward, material model |
| BGFX / The Forge | Cross-platform RHI, draw submission |
| AMD GPUOpen (FidelityFX) | FSR, CACAO, Denoiser, SPD, Classifier |
| NVIDIA (RTX, DLSS, Nsight) | RT pipeline, AI upscaling, profiling |
| Vulkan/D3D12 best practices | Descriptor buffer, BDA, mesh shaders, dynamic rendering |
| Academic SOTA (SIGGRAPH/HPG) | Latest culling, LOD, streaming, compression papers |
| CAD-specific (HOOPS, OpenCascade, JT Open) | Edge rendering, section planes, PMI, measurement |

Use `WebSearch` to find the latest techniques (2024-2026 publications,
GDC/SIGGRAPH/HPG talks, Vulkan/D3D12 best practice guides).

**2.3 For each domain, produce a comparison row**:

| Domain | Plan's Approach | SOTA / Best Practice | Gap | Recommendation |
|--------|----------------|---------------------|-----|----------------|

### Phase 3: Architecture Alignment

Read `specs/rendering-pipeline-architecture.md` (the authoritative pipeline spec).
For each item in the plan, verify:

3.1. **Pass graph alignment** — does the plan's pass structure match the
     88-pass reference? Are pass IDs, inputs, outputs, and tier availability
     consistent with §3?

3.2. **Data structure alignment** — do GPU structs (`GpuInstance`,
     `GpuMeshlet`, `MaterialParameterBlock`, etc.) match §5-§8 definitions?
     Check `alignas`, field order, `static_assert` sizes.

3.3. **Tier strategy** — does the plan correctly differentiate T1 (Vulkan/D3D12
     mesh shader path) vs T2/T3/T4 (compat vertex path)? No `if (compat)`
     branches — uses `IPipelineFactory` virtualization per §1.5?

3.4. **Performance budget** — do planned GPU time budgets match §3 column
     "Budget"? Flag any pass exceeding its budget.

3.5. **Dependency correctness** — do pass dependencies match the DAG in §3
     and the RenderGraph architecture in §2?

3.6. **Shader IR** — correct shader language per tier (SPIR-V / DXIL / WGSL /
     GLSL 4.30) per §1.3?

3.7. **Bindless / BDA** — plan uses bindless descriptor model and buffer
     device address where applicable per §1.1 and §6?

Output an alignment table:

| Check | Status | Detail |
|-------|--------|--------|
| Pass graph | PASS/FAIL | ... |
| Data structs | PASS/FAIL | ... |
| Tier strategy | PASS/FAIL | ... |
| Perf budget | PASS/FAIL | ... |
| Dependencies | PASS/FAIL | ... |
| Shader IR | PASS/FAIL | ... |
| Bindless/BDA | PASS/FAIL | ... |

### Phase 4: Gap Analysis & Expansion

4.1. **Missing features** — identify features present in SOTA engines or
     required by the pipeline spec but absent from the plan. For each:
     - What is missing
     - Why it matters (perf, correctness, feature parity)
     - Where to add it (which task/step)
     - Effort estimate

4.2. **Performance opportunities** — identify where the plan could achieve
     better performance:
     - Unnecessary CPU-GPU sync points
     - Missing async compute opportunities
     - Suboptimal memory access patterns
     - Missing hardware features (VRS, wave ops, subgroup ops)

4.3. **Architectural improvements** — identify structural improvements:
     - Better abstraction boundaries
     - Missing extension points for future phases
     - Tighter integration with RenderGraph
     - Better error handling or resource lifetime management

4.4. **Produce expanded plan diff** — output concrete additions as a
     structured diff (not a full rewrite):

```
+ [NEW] Step N.M: <description>
  Files: <file list>
  Rationale: <why this was added>

~ [MODIFIED] Step X.Y: <original> → <expanded>
  Rationale: <why this was changed>

- [REMOVED] Step A.B: <description>
  Rationale: <why this is unnecessary or harmful>
```

### Phase 5: Second Review

After producing the expanded plan, perform a self-review:

5.1. Re-read the expanded plan as a whole.

5.2. Check for internal consistency:
     - No circular dependencies introduced
     - No duplicate work across steps
     - Effort estimates still realistic
     - Test coverage still adequate

5.3. Check for over-engineering:
     - Every addition must have a concrete justification
     - No speculative features without clear downstream need
     - No premature optimization without profiling evidence

5.4. Verify the expanded plan still fits within the phase's scope
     (check complexity limits from `miki-phase.mdc` §2.7).

5.5. Output a final verdict table:

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| Total steps | N | M | +K |
| Estimated effort (hours) | X | Y | +Z |
| SOTA coverage (%) | A% | B% | +C% |
| Architecture alignment | D/7 | E/7 | +F |
| Performance risk items | G | H | -I |

### Phase 6: Report

Output the complete review as a structured report:

```markdown
## Review Plan Report: <plan name>

### 1. SOTA Benchmark
<Phase 2 comparison table>

### 2. Architecture Alignment
<Phase 3 alignment table>

### 3. Gap Analysis
<Phase 4 findings>

### 4. Expanded Plan
<Phase 4 diff>

### 5. Second Review
<Phase 5 verdict>

### 6. Open Questions
<Any items requiring user decision>
```

---

## Key Principles

- **Research before opining** — always use `WebSearch` for SOTA claims.
  Do not rely on training data for technique comparisons.
- **Concrete over vague** — every recommendation must include specific
  files, APIs, or techniques. No "consider improving performance".
- **Respect the pipeline spec** — `rendering-pipeline-architecture.md`
  is authoritative. The plan must align, not the other way around.
- **Diff, not rewrite** — output changes as structured diffs against
  the existing plan. Preserve what already works.
- **Justify additions** — every new step must cite either a SOTA
  reference, a pipeline spec requirement, or a downstream phase need.
- **Flag, don't force** — if a recommendation is debatable, present
  it as an open question for the user, not a unilateral change.
