# T{id}.X.Y — {Task Name}

**Phase**: {nn}-{id} (Tech Spike)
**Status**: Not Started | In Progress | Complete
**Current Step**: —
**Resume Hint**: —
**Effort**: S (< 1h) | M (1-2h) | L (2-4h)
**Decision Gate**: Go | NoGo | Pending

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | — | — | — |

## Spike Objective

> Tech Spike Tasks produce a **decision document**, not production code.
> No Context Anchor Card — no Contract Verification — no API locking.
> Code produced is **throwaway prototype** unless the Decision Gate = Go.

### Question to answer

{The specific technical question this spike must answer.
 e.g., "Can SDF trim textures achieve <0.5px accuracy at <512MB VRAM for 1M faces?"}

### Success criteria (Go)

- [ ] {Quantitative threshold — e.g., "VRAM <= 512MB for 1M faces"}
- [ ] {Quantitative threshold — e.g., "Accuracy < 0.5px deviation at typical CAD zoom"}
- [ ] {Performance threshold — e.g., "CPU generation time < 5ms per face"}

### Failure criteria (NoGo)

- [ ] {e.g., "VRAM > 1GB AND cannot be reduced by virtual paging"}
- [ ] {e.g., "Accuracy > 2px at any tested zoom level"}

### Fallback plan (if NoGo)

{Describe the alternative approach committed to if spike fails.
 e.g., "Abandon GPU trim, commit to CPU multi-threaded pre-tessellation as permanent strategy."}

## Prototype Files

> These are throwaway. Placed under `demos/spikes/{spike_name}/`, NOT under `src/miki/`.

| Action | Path | Notes |
|--------|------|-------|
| Create | `demos/spikes/{name}/main.cpp` | Prototype entry point |
| Create | `demos/spikes/{name}/CMakeLists.txt` | Standalone build |

## Steps

- [ ] **Step 1**: {Build prototype}
      **Produces**: {runnable prototype}
      `[verify: compile]`

- [ ] **Step 2**: {Measure metric A}
      **Produces**: {measurement data}
      **Method**: {how to measure — e.g., RenderDoc capture, VRAM query, timing}
      `[verify: manual]`

- [ ] **Step 3**: {Measure metric B}
      **Produces**: {measurement data}
      `[verify: manual]`

- [ ] **Step 4**: Decision Gate evaluation
      **Produces**: Go/NoGo verdict with evidence
      `[verify: manual]`

## Measurement Results

> Filled during implementation.

| Metric | Target | Measured | Verdict |
|--------|--------|----------|---------|
| {e.g., VRAM at 1M faces} | <= 512MB | — | — |
| {e.g., Accuracy at 10x zoom} | < 0.5px | — | — |
| {e.g., CPU gen time per face} | < 5ms | — | — |

## Decision

> Filled at Step 4.

**Verdict**: Go / NoGo / Conditional
**Evidence summary**: {2-3 sentences with key numbers}
**Implications for downstream phases**: {which phases are affected and how}
**Action items**: {if Go: what to add to target phase. If NoGo: what fallback to activate}

## Notes

*(Observations, surprises, alternative approaches discovered during spike.)*
