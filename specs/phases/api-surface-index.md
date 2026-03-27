# Locked API Surface Index (Hub)

> Cumulative record of ALL locked public APIs across completed phases.
> Split by **architecture layer** to keep per-load token cost bounded.
>
> **Layer-based loading protocol** (`/miki-roadmap` §1.3):
> 1. Read **this hub file** (~60 lines) to get Phase→Layer mapping + dependency table.
> 2. Compute the current phase's **transitive dependency chain** from the table.
> 3. Map dependent phases to their **Layer numbers** using the table below.
> 4. Read only those layer files: `specs/phases/api-surface-L{NN}.md`
> 5. Token cost = O(number of dependent layers), NOT O(total completed phases).
>
> At Phase 10+, the monolithic file would exceed 1000 lines.
> With layer split, each file stays ~100-200 lines (1-4 phases per layer).

---

## Phase → Layer Mapping

> From the 11-layer architecture in `roadmap.md`.
> Each phase's APIs belong to one primary layer.

| Phase | Layer | Layer Name | Direct Dependencies | Status |
|-------|-------|------------|--------------------|---------|
| 1a | L01 | Foundation + RHI | — | Not Started |
| 1b | L01 | Foundation + RHI | 1a | Not Started |
| 2 | L02 | Forward Rendering | 1a, 1b | Complete (TextRenderer deferred to 3a) |
| 3a | L03 | Render Graph | 2 | Not Started |
| 3b | L03 | Post-Processing | 3a | Not Started |
| 4 | L02 | Resource Management | 2 | Not Started |
| 5 | L04 | ECS & Scene | 4 | Not Started |
| 6a | L05 | GPU-Driven Core | 3b, 4, 5 | Not Started |
| 6b | L05 | Streaming & LOD | 6a | Not Started |
| 7a1 | L06 | CAD Rendering | 3b, 6a | Not Started |
| 7a2 | L06 | CAD Interaction | 7a1 | Not Started |
| 7b | L06 | CAD Precision | 7a2 | Not Started |
| 8 | L07 | Scene Management | 5, 7b | Not Started |
| 9 | L08 | Interactive Tools | 8 | Not Started |
| 10 | L08 | CAE & Point Cloud | 8, 6b | Not Started |
| 11 | L09 | Debug & Profiling | 3a | Not Started |
| 11b | L09 | Compat Hardening | 8, 11 | Not Started |
| 11c | L09 | OpenGL Hardening | 11b | Not Started |
| 12 | L08 | Multi-View | 8 | Not Started |
| 13 | L10 | Async & Coroutine | 1..12 | Not Started |
| 14 | L10 | Scale Validation | 6b..8, 13 | Not Started |
| 15a | L11 | SDK & Headless | 1..14 | Not Started |
| 15b | L11 | Cloud & Release | 15a | Not Started |

*(1.1 phases: 16-21 added when entering 1.1 development.)*

---

## Layer Files

> Each file is append-only. Created when the first phase in that layer completes.

| File | Layers | Phases |
|------|--------|--------|
| `specs/phases/api-surface-L01.md` | L01 Foundation + RHI | 1a, 1b |
| `specs/phases/api-surface-L02.md` | L02 Rendering + Resources | 2, 4 |
| `specs/phases/api-surface-L03.md` | L03 Render Graph + Post | 3a, 3b |
| `specs/phases/api-surface-L04.md` | L04 ECS & Scene | 5 |
| `specs/phases/api-surface-L05.md` | L05 GPU-Driven | 6a, 6b |
| `specs/phases/api-surface-L06.md` | L06 CAD Rendering | 7a1, 7a2, 7b |
| `specs/phases/api-surface-L07.md` | L07 Scene Management | 8 |
| `specs/phases/api-surface-L08.md` | L08 Tools & CAE | 9, 10, 12 |
| `specs/phases/api-surface-L09.md` | L09 Debug & Compat | 11, 11b, 11c |
| `specs/phases/api-surface-L10.md` | L10 Async & Perf | 13, 14 |
| `specs/phases/api-surface-L11.md` | L11 SDK & Release | 15a, 15b |

---

*(No phases completed yet. First layer file created when Phase 1a completes.)*

<!-- Template for each phase entry inside a layer file:

## Phase {nn}-{id}: {Name}

**Completed**: YYYY-MM-DD
**Direct Deps**: Phase X, Y

| File | Exposure | Ref Heat | Key Exports | Consumed by Phase(s) |
|------|----------|----------|-------------|---------------------|
| `include/miki/module/IFoo.h` | **public** | **H** | `IFoo`, `FooDesc` | Phase X, Y, Z |

**Total locked headers**: N

-->
