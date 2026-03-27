# Phase {nn}: {id} — {Name}

**Sequence**: {nn} / {total_phases}
**Status**: Not Started | In Progress | Complete
**Started**: YYYY-MM-DD
**Completed**: YYYY-MM-DD

## Goal

{Copy from roadmap.md — the **Goal** paragraph}

## Roadmap Digest

> **Written during Phase planning** (`/miki-phase` §1.6). Distills the roadmap section
> into a compact reference (~30-50 lines). Task execution reads THIS instead of the
> full roadmap section, saving ~2000-4000 tokens per Task.
>
> **Update rule**: if `roadmap.md` is modified for this phase, regenerate this digest.

### Key Components (from roadmap table)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | {Name} | {1-line summary} | ~N |
| 2 | {Name} | {1-line summary} | ~N |

### Critical Technical Decisions

- {Decision 1 — e.g., "VSM over CSM due to 0.1mm-10km depth range"}
- {Decision 2 — e.g., "Linked-list OIT for >4 layer accuracy"}

### Performance Targets (from roadmap Part VII, if applicable to this phase)

| Metric | Target |
|--------|--------|
| {e.g., 100M tri @1080p} | {>= 60fps} |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase X | {API / type / behavior it will consume} |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase X         | API / infra     |

---

## Components & Tasks

### Component 1: {Name from roadmap table}

> {One-line summary from roadmap}

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ]    | T{id}.1.1 | {Task Name} | — | S |
| [ ]    | T{id}.1.2 | {Task Name} | T{id}.1.1 | M |
| [ ]    | T{id}.1.3 | {Task Name} | T{id}.1.1 | L |

### Component 2: {Name}

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ]    | T{id}.2.1 | ... | — | M |

...

---

## Demo Plan

- **Name**: `demos/{name}/`
- **Shows**: ...
- **Requires Tasks**: T{id}.X.Y, T{id}.X.Z, ...
- **CLI**: `--backend vulkan|d3d12|...`
- **Acceptance**:
  - [ ] renders correctly on backend A
  - [ ] renders correctly on backend B
  - [ ] golden image diff < threshold

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~N     |        |       |
| Integration | ~N     |        |       |
| Visual      | ~N     |        |       |
| Benchmark   | ~N     |        |       |
| **Total**   | ~N     |        | Roadmap target: ~M |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|-----------------|
| L0    | T{id}.1.1, T{id}.2.1         | —               |
| L1    | T{id}.1.2, T{id}.1.3         | L0              |
| L2    | T{id}.3.1                     | L1              |
| ...   | ...                           | ...             |

**Critical path**: L0 -> L1 -> L2 -> ... -> Demo

---

## Forward Design Notes

*(Filled during planning — record what this phase pre-designs for future phases)*

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase X     | ...                    | `IFoo::Bar()` placeholder  |

---

## Cross-Component Contracts

*(Filled during planning 1.3.5 — records agreed interfaces between Components)*

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T{id}.X.Y | `IFoo.h` (**public**) | T{id}.Z.W | `Method(Args) -> Return` |

---

## Completion Summary

*(Filled on phase completion)*

- **Date**: YYYY-MM-DD
- **Tests**: N pass / N total
- **Known limitations**: ...
- **Design decisions**: key architectural choices made during implementation
- **Next phase**: Phase {next_id}

### Locked API Surface

| File | Exposure | Key Exports | Consumed by Phase(s) |
|------|----------|-------------|---------------------|
| `include/miki/module/IFoo.h` | **public** | `IFoo`, `FooDesc` | Phase X, Y, Z |
