# T3b.10.1 — IAOProvider Interface + RTAOConfig + RTAO Stub

**Phase**: 05-3b
**Component**: 10 — RTAO Stub
**Roadmap Ref**: `roadmap.md` Phase 3b — RTAO stub; Phase 7a-2 activation
**Status**: Complete
**Effort**: S (< 1h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.8.1 | GTAO | Not Started | `Gtao`, AOBuffer output |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `IAOProvider.h` | **public** | **H** | `IAOProvider` interface, `AOMode` enum, `RTAOConfig` |
| `RtaoStub.h` | **shared** | **M** | `RtaoStub` -- returns GTAO output; placeholder until Phase 7a-2 |

- `IAOProvider`: abstract interface for AO sources. `GetAOBuffer() -> TextureHandle`, `GetMode() -> AOMode`.
- `AOMode` enum: `{ GTAO, SSAO, RTAO }`
- `RTAOConfig`: `{ uint32_t spp=1; float maxRange=5.0f; bool temporalAccumulation=true; }` -- defines RTAO parameters for future activation.
- `RtaoStub`: implements IAOProvider, delegates to GTAO. Phase 7a-2 replaces with real RT implementation.
- GTAO and SSAO also implement IAOProvider (adapter wrappers).

### Downstream Consumers

- `IAOProvider.h` (**public** H):
  - T3b.16.1: IPipelineFactory uses IAOProvider to get tier-appropriate AO
  - Phase 7a-2: RTAO activation implements IAOProvider with real ray-query AO
  - DeferredResolve: reads AOBuffer from IAOProvider

### Technical Direction

- **Strategy pattern**: IAOProvider abstracts AO source. Rendering code never checks tier for AO -- just calls `provider->GetAOBuffer()`.
- **Stub contract**: RtaoStub::GetMode() returns AOMode::RTAO but GetAOBuffer() returns GTAO output. Phase 7a-2 provides real implementation.
- **Config forward-compatibility**: RTAOConfig defined now so Phase 7a-2 can use it without API change.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/IAOProvider.h` | **public** | **H** | Interface |
| Create | `include/miki/rendergraph/RtaoStub.h` | **shared** | **M** | Stub |
| Create | `src/miki/rendergraph/RtaoStub.cpp` | internal | L | Delegates to GTAO |
| Create | `tests/unit/test_ao_provider.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define IAOProvider interface + AOMode enum + RTAOConfig
- [x] **Step 2**: Implement GtaoProvider and SsaoProvider adapter wrappers
- [x] **Step 3**: Implement RtaoStub (delegates to GTAO)
- [x] **Step 4**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(IAOProvider, GtaoProvider_ReturnsGTAO)` | Positive | mode == GTAO, valid buffer |
| `TEST(IAOProvider, SsaoProvider_ReturnsSSAO)` | Positive | mode == SSAO, valid buffer |
| `TEST(IAOProvider, RtaoStub_ReturnsRTAOMode)` | Positive | mode == RTAO |
| `TEST(IAOProvider, RtaoStub_DelegatesToGTAO)` | State | buffer handle matches GTAO output |
| `TEST(IAOProvider, NullGtao_Error)` | Error | RtaoStub with null GTAO provider returns error |
| `TEST(IAOProvider, SwitchModeRuntime)` | Boundary | switching AOMode at runtime returns correct provider |
| `TEST(IAOProvider, EndToEnd_ProviderChain)` | Integration | create GTAO + wrap in provider + get AOBuffer + verify format |
