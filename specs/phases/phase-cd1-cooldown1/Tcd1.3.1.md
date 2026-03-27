# Tcd1.3.1 — API Audit: IDevice.h

**Phase**: cd1-cooldown1 (Cooldown #1)
**Category**: API Audit
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Scope

- **Problem**: IDevice.h is the most-referenced public header. Must be fully audited before freeze.
- **Affected files**: `include/miki/rhi/IDevice.h`
- **Affected phases**: ALL (every phase uses IDevice)

### Acceptance Criteria

- [ ] Every virtual method has `[[nodiscard]]` where return value matters
- [ ] Every factory method parameter uses `explicit` constructors where applicable
- [ ] Complete Doxygen for all public methods
- [ ] `static_assert` on all GPU-facing structs in the header
- [ ] No breaking change needed (if needed, document migration path)

## Audit Checklist

| Method | `[[nodiscard]]` | Doxygen | Error model | Verdict |
|--------|----------------|---------|-------------|---------|
| CreateBuffer | | | | |
| CreateTexture | | | | |
| CreateGraphicsPipeline | | | | |
| CreateComputePipeline | | | | |
| ... | | | | |

*(Fill during execution)*

## Steps

- [ ] **Step 1**: Audit IDevice.h against checklist, fix all issues
      `[verify: compile]`
- [ ] **Step 2**: Verify no downstream test regressions
      `[verify: test]`
