# T7b.8.2 — ODA/Datakit Stub Integration (Plugin DLL Loading)

**Phase**: 13-7b
**Component**: CAD Translator SDK
**Status**: Not Started
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status |
|---------|------|--------|
| T7b.8.1 | ITranslator Plugin Interface | Not Started |

## Scope

Stub implementations for ODA Drawings SDK and Datakit CrossCad/Ware translator plugins. Plugin DLL/SO loading via `TranslatorRegistry::LoadPlugin(path)`. Format detection from file extension. Stub returns `NotSupported` when SDK not linked. Tests validate DLL loading, format dispatch, graceful fallback.

## Steps

- [ ] **Step 1**: Implement ODA/Datakit plugin stubs + DLL loading
      `[verify: compile]`
- [ ] **Step 2**: Tests (plugin load, format dispatch, stub fallback)
      `[verify: test]`
