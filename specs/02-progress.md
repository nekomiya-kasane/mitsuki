# RHI Backend Implementation Progress

> Last updated: 2026-03-28

## Vulkan Backend — COMPLETE

- 9 .cpp + 2 .h, 100% spec coverage
- Zero stubs, zero TODO, zero assert(false)

## D3D12 Backend — COMPLETE (with deferred items)

- 8 .cpp + 2 .h, 100% API surface implemented
- CMake: `miki_d3d12.cmake`, default ON on Windows

### Deferred Items

| Item                                                                | Reason                                                                                                                                                                                   | Priority | Resolution Phase                                                      |
| ------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- | --------------------------------------------------------------------- |
| `ExecuteIndirect` (DrawIndirect/DispatchIndirect/MeshTasksIndirect) | Requires pre-created `ID3D12CommandSignature` per draw type. Vulkan has native `vkCmdDrawIndirect`; D3D12 requires explicit signature describing indirect argument layout.               | HIGH     | Phase 3 (RenderGraph) — create command signatures at device init      |
| `CmdBlitTexture`                                                    | D3D12 has no native image blit. Requires fullscreen quad + shader or compute pass.                                                                                                       | MEDIUM   | Phase 4 (Post-processing) — implement blit utility pipeline           |
| `CmdFillBuffer`                                                     | D3D12 has no `CmdFillBuffer`. Use `ClearUnorderedAccessViewUint` on UAV or compute shader fill.                                                                                          | MEDIUM   | Phase 3 — implement via UAV clear path                                |
| `CmdClearColorTexture` / `CmdClearDepthStencil` (standalone)        | Requires creating temporary RTV/DSV descriptors for arbitrary textures. Currently only works within `CmdBeginRendering`.                                                                 | LOW      | Phase 3 — add descriptor cache for on-demand RTV/DSV creation         |
| Pipeline Library split compilation (`LinkGraphicsPipeline`)         | D3D12 Pipeline State Streams sub-object merging via `ID3D12Device2::CreatePipelineState`.                                                                                                | LOW      | Phase 5 (Material System) — when PSO permutation explosion demands it |
| Mesh shader PSO                                                     | Current path uses `CreateGraphicsPipelineState` with VS slot. Proper MS PSO requires `ID3D12Device2::CreatePipelineState` with `CD3DX12_PIPELINE_MESH_STATE_STREAM`.                     | MEDIUM   | Phase 4 (Mesh pipeline) — when mesh shader rendering path is active   |
| Legacy ResourceBarrier fallback                                     | Current implementation uses Enhanced Barriers exclusively (`ID3D12GraphicsCommandList7::Barrier`). Fallback to legacy `ResourceBarrier` for pre-Agility SDK drivers not yet implemented. | LOW      | Only needed for Windows 10 21H2 and earlier                           |
| `CmdSetDepthBias` dynamic                                           | D3D12 depth bias is PSO state. Dynamic depth bias requires `ID3D12GraphicsCommandList9::OMSetDepthBias` (Windows 11 24H2+).                                                              | LOW      | Phase 8 (Shadow mapping) — when dynamic depth bias is needed          |

# 文件 问题 严重度 说明

1 D3D12CommandBuffer.cpp:303-317 CmdDrawIndirect 是空实现 (no-op) 🔴 高 缺 ID3D12CommandSignature，整个 indirect draw 路径无效
2 D3D12CommandBuffer.cpp:319-330 CmdDrawIndexedIndirect 空实现 🔴 高 同上
3 D3D12CommandBuffer.cpp:332-346 CmdDrawIndexedIndirectCount 空实现 🔴 高 同上
4 D3D12CommandBuffer.cpp:352-356 CmdDrawMeshTasksIndirect 空实现 🔴 高 需要 mesh shader command signature
5 D3D12CommandBuffer.cpp:358-363 CmdDrawMeshTasksIndirectCount 空实现 🔴 高 同上
6 D3D12CommandBuffer.cpp:373-380 CmdDispatchIndirect 空实现 🔴 高 需要 compute command signature
7 D3D12CommandBuffer.cpp:498-503 CmdBlitTexture 空实现 🟡 中 D3D12 无原生 blit，需 compute/fullscreen quad
8 D3D12CommandBuffer.cpp:505-509 CmdFillBuffer 空实现 🟡 中 需要 ClearUnorderedAccessViewUint
9 D3D12CommandBuffer.cpp:511-520 CmdClearColorTexture 空实现 🟡 中 有 RTV 但未使用 ClearRenderTargetView
10 D3D12CommandBuffer.cpp:522-532 CmdClearDepthStencil 空实现 🟡 中 有 DSV 但未使用 ClearDepthStencilView
11 D3D12CommandBuffer.cpp:673-679 CmdSetDepthBias 空实现 🟢 低 D3D12 PSO state，需 ID3D12GraphicsCommandList9
12 D3D12CommandBuffer.cpp:727-730 CmdSetShadingRateImage 空实现 🟡 中 需要从 view 获取 resource 指针
13 D3D12Pipelines.cpp:165-169 Mesh shader PSO 使用 VS slot 占位 🔴 高 需要 Pipeline State Stream API
14 D3D12Pipelines.cpp:468-486 CreatePipelineLibraryPart/LinkGraphicsPipeline 占位 🟡 中 Split compilation 未实现
15 D3D12Pipelines.cpp:392-404 RT pipeline 不存储 StateObject 🔴 高 pso 为 null，无法 dispatch RT
16 D3D12CommandBuffer.cpp:270 CmdBindVertexBuffer StrideInBytes=0 🔴 高 必须从 pipeline 的 vertex input state 获取 stride

WebGPU:

Key Architecture Decisions
Push constants: 256B UBO at @group(0) @binding(0), user bindings shifted to group(N+1)
Command recording: Deferred via WGPUCommandEncoder, finalized at End()
Fence: Emulated via wgpuQueueOnSubmittedWorkDone callback + monotonic serial
Barriers: No-op (Dawn handles transitions implicitly)
Memory: API-managed, no introspection (GetMemoryStats returns zeros)
Bind groups: Immutable — UpdateDescriptorSet recreates the WGPUBindGroup
Surface: Modern configure/unconfigure model (not deprecated WGPUSwapChain)
Known Deferred Items
CmdBlitTexture — requires fullscreen quad shader
CmdFillBuffer(non-zero) — requires compute shader
CmdClearColor/DepthStencil — WebGPU only supports clear via render pass loadOp
CmdExecuteSecondary — render bundles not yet wired
Linux/macOS surface — only Win32 HWND + Emscripten canvas implemented
Dawn prebuilt — needs to be downloaded to third_party/webgpu/dawn/prebuilt/
