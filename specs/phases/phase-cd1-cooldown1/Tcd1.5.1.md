# Tcd1.5.1 — Architecture Overview Document

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: Docs
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: H (2-4h)

## Scope

- **Problem**: No written architecture overview exists. Team onboarding requires understanding module map, data flow, and threading model.
- **Affected files**: `docs/architecture-overview.md` (NEW)

### Acceptance Criteria

- [ ] Module map: all 11 namespaces with dependency arrows
- [ ] Data flow: CPU→GPU upload path, render graph execution, streaming pipeline
- [ ] Threading model: which operations run on which threads (render, IO, compute)
- [ ] GPU pipeline diagram: full Tier1 frame (DepthPrePass → HiZ → Cull → Geometry → Resolve → Lighting → PostProcess → Present)
- [ ] Mermaid diagrams for all major flows

## Steps

- [ ] **Step 1**: Write architecture-overview.md with module map + data flow + threading
      `[verify: manual]`
