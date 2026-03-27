# T7b.8.1 — ITranslator Plugin Interface + TranslatorRegistry

**Phase**: 13-7b
**Component**: CAD Translator SDK
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

None (L0 task).

## Scope

`ITranslator::Import(path) → ImportResult { bodies[], assembly_tree, pmi[], metadata }`. Plugin DLL/SO loaded at runtime via `TranslatorRegistry`. Supports ODA, Datakit, CoreTechnologie backends. Plugin selection at build time (`MIKI_TRANSLATOR_DATAKIT=ON`). Fallback: STEP/JT/IGES for uncovered formats.

## Steps

- [ ] **Step 1**: Define ITranslator interface + TranslatorRegistry (DLL loading)
      `[verify: compile]`
- [ ] **Step 2**: Tests (registry lookup, stub plugin load, fallback path)
      `[verify: test]`
