# RHI Backend Implementation Progress

> Last updated: 2026-03-28

## Vulkan Backend — COMPLETE

- 9 .cpp + 2 .h, 100% spec coverage
- Zero stubs, zero TODO, zero assert(false)

## D3D12 Backend — COMPLETE (with deferred items)

- 8 .cpp + 2 .h, 100% API surface implemented
- CMake: `miki_d3d12.cmake`, default ON on Windows

### Deferred Items

| Item | Reason | Priority | Resolution Phase |
|---|---|---|---|
| `ExecuteIndirect` (DrawIndirect/DispatchIndirect/MeshTasksIndirect) | Requires pre-created `ID3D12CommandSignature` per draw type. Vulkan has native `vkCmdDrawIndirect`; D3D12 requires explicit signature describing indirect argument layout. | HIGH | Phase 3 (RenderGraph) — create command signatures at device init |
| `CmdBlitTexture` | D3D12 has no native image blit. Requires fullscreen quad + shader or compute pass. | MEDIUM | Phase 4 (Post-processing) — implement blit utility pipeline |
| `CmdFillBuffer` | D3D12 has no `CmdFillBuffer`. Use `ClearUnorderedAccessViewUint` on UAV or compute shader fill. | MEDIUM | Phase 3 — implement via UAV clear path |
| `CmdClearColorTexture` / `CmdClearDepthStencil` (standalone) | Requires creating temporary RTV/DSV descriptors for arbitrary textures. Currently only works within `CmdBeginRendering`. | LOW | Phase 3 — add descriptor cache for on-demand RTV/DSV creation |
| Pipeline Library split compilation (`LinkGraphicsPipeline`) | D3D12 Pipeline State Streams sub-object merging via `ID3D12Device2::CreatePipelineState`. | LOW | Phase 5 (Material System) — when PSO permutation explosion demands it |
| Mesh shader PSO | Current path uses `CreateGraphicsPipelineState` with VS slot. Proper MS PSO requires `ID3D12Device2::CreatePipelineState` with `CD3DX12_PIPELINE_MESH_STATE_STREAM`. | MEDIUM | Phase 4 (Mesh pipeline) — when mesh shader rendering path is active |
| Legacy ResourceBarrier fallback | Current implementation uses Enhanced Barriers exclusively (`ID3D12GraphicsCommandList7::Barrier`). Fallback to legacy `ResourceBarrier` for pre-Agility SDK drivers not yet implemented. | LOW | Only needed for Windows 10 21H2 and earlier |
| `CmdSetDepthBias` dynamic | D3D12 depth bias is PSO state. Dynamic depth bias requires `ID3D12GraphicsCommandList9::OMSetDepthBias` (Windows 11 24H2+). | LOW | Phase 8 (Shadow mapping) — when dynamic depth bias is needed |
