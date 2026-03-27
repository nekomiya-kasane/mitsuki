# T7b.4.1 — PMI Entity Types

**Phase**: 13-7b
**Component**: GPU PMI & Annotation
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

Define PMI entity types for STEP AP242 import and GPU rendering: `PmiAnnotation` (base), `GdtSymbol` (position/flatness/perpendicularity/etc.), `DatumMarker` (triangle + letter), `SurfaceRoughnessSymbol` (Ra/Rz with value), `WeldSymbol` (ISO 2553), `ToleranceFrame` (feature control frame). `PmiFilter` bitmask type for decluttering.

## Steps

- [ ] **Step 1**: Define PMI types in `include/miki/render/PmiTypes.h`
      `[verify: compile]`
