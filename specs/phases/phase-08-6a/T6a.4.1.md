# T6a.4.1 — GpuInstance Struct + SceneBuffer CPU Mirror + Dirty-Flag Compute Upload

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: SceneBuffer + GPU Scene Submission
**Roadmap Ref**: `roadmap.md` L1744 — GPU Scene Submission, SceneBuffer
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.0.1 | RHI Extensions | Complete | `BufferUsage::ShaderDeviceAddress` for GPU instance buffer |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/GpuInstance.h` | **public** | **H** | `GpuInstance` (128B GPU struct), `InstanceFlags`, `SelectionMask` |
| `include/miki/vgeo/SceneBuffer.h` | **public** | **H** | `SceneBuffer::Create()`, `Add/Remove/Update()`, `Upload()`, `GetGpuBuffer()`, `GetCpuMirror()` |
| `shaders/vgeo/scene_upload.slang` | internal | L | Compute shader: copy dirty instances to GPU SSBO |
| `src/miki/vgeo/SceneBuffer.cpp` | internal | L | CPU mirror + dirty tracking + upload dispatch |

- **Error model**: `Create()` → `Result<SceneBuffer>`. `Add()` → `Result<uint32_t>` (instance index). `Upload()` is void.
- **Thread safety**: NOT thread-safe. Single update thread (render thread).
- **GPU constraints**: `GpuInstance` = 128B, `alignas(16)`, matches Slang struct layout. Buffer usage: `Storage | ShaderDeviceAddress | TransferDst`.
- **Invariants**: `GpuInstance.entityId` maps 1:1 to ECS `Entity._value`. CPU mirror is always in sync after `Upload()`. Instance indices are stable (no compaction on remove — use tombstone).

### Downstream Consumers

- `GpuInstance.h` (**public**, heat **H**):
  - T6a.4.2 (MacroBin): reads `GpuInstance.selectionMask` + `flags` for bucket classification
  - T6a.5.1 (Task Shader): reads `GpuInstance.worldMatrix` + `boundingSphere` for frustum cull
  - T6a.6.2 (Material Resolve): reads `GpuInstance.materialId` for per-pixel material fetch
  - Phase 7a-1 (HLR): reads `GpuInstance.entityId` for edge→entity lookup
  - Phase 7a-2 (Picking): reads `GpuInstance.entityId` for VisBuffer→Entity resolution, `selectionMask` + `flags` for selectability
  - Phase 8 (CadScene): writes `GpuInstance.flags` (layerBits:16 + displayStyle:4 + lineStyle:4 + assemblyLevel:4 + analysisOverlay:4)
- `SceneBuffer.h` (**public**, heat **H**):
  - T6a.4.2: reads `SceneBuffer::GetGpuBuffer()` for cull compute input
  - T6a.5.1: reads `SceneBuffer::GetGpuBuffer()` for task shader instance buffer
  - T6a.8.1 (Demo): calls `Add()`, `Update()`, `Upload()`
  - Phase 6b: extends `SceneBuffer` with LOD fields, uses `GetDirtyInstances()` for incremental TLAS
  - Phase 7a-2: calls `GetCpuMirror()` for O(1) entity resolution after pick readback
  - Phase 8: calls `Add/Remove/Update()` from CadScene presentation layer

### Upstream Contracts
- Phase 5 `Entity`: `Entity._value` (uint32_t) stored as `GpuInstance.entityId`
- Phase 5 `ComponentPool<T>::ForEachWithEntity()`: iterate (Entity, TransformComponent) for bulk upload
- Phase 4 `ResourceManager::CreateBuffer()`: GPU SSBO creation
- Phase 4 `BDAManager::Register()`: BDA for mesh shader vertex fetch from instance buffer

### Technical Direction
- **GpuInstance layout** (128B, frozen at this phase for Phase 7a-2/8 forward compatibility):
  ```
  struct GpuInstance {                    // alignas(16), 128 bytes
      float4x4    worldMatrix;            // 64B
      float4      boundingSphere;         // 16B — xyz=center, w=radius (world-space)
      uint32_t    entityId;               // 4B  — ECS Entity._value
      uint32_t    meshletBaseIndex;       // 4B  — offset into global MeshletDescriptor[]
      uint32_t    meshletCount;           // 4B
      uint32_t    materialId;             // 4B  — index into MaterialParameterBlock[]
      uint32_t    selectionMask;          // 4B  — selected/hovered/ghosted/isolated bits
      uint32_t    colorOverride;          // 4B  — RGBA8 packed, 0 = no override
      uint32_t    clipPlaneMask;          // 4B  — per-instance section plane enable
      uint32_t    flags;                  // 4B  — see flags bit layout below
      uint32_t    _padding[4];            // 16B — pad to 128B
  };
  static_assert(sizeof(GpuInstance) == 128);
  ```
- **`flags` bit layout** (per `rendering-pipeline-architecture.md` §5.5, frozen at this phase):
  ```
  bits  0-15:  layerBits       (16 layers, 1 bit each)
  bits 16-19:  displayStyle    (14 modes, 4 bits — Shaded/ShadedEdges/Wireframe/HLR/...)
  bits 20-23:  lineStyle       (8 modes: Solid/Dash/Dot/DashDot/DashDotDot/Phantom/Center/Hidden)
  bits 24-27:  assemblyLevel   (depth in segment tree, 4 bits)
  bits 28-31:  analysisOverlay (Zebra/Iso/Curv/Draft, 4 bits)
  ```
  Phase 7a-1 HLR reads `lineStyle` for edge rendering. Phase 8 CadScene writes `displayStyle` + `layerBits`.
- **`worldMatrix` is CAMERA-RELATIVE (RTE)** (per `rendering-pipeline-architecture.md` §5.5): CPU computes `worldMatrix = double_modelMatrix - double_cameraPos` then casts to `float4x4`. This eliminates FP32 jitter at >10km distance. Phase 5 `RteManager` provides the camera-relative transform computation.
- **Dirty tracking**: per-instance dirty bit (std::vector<bool> or bitset). `Upload()` dispatches compute that copies only dirty instances from staging → GPU SSBO. Clears dirty bits after upload.
- **Tombstone removal**: removed instances get `entityId = 0` (null entity). GPU cull skips null entities. No array compaction — indices are stable for VisBuffer→Entity O(1) lookup.
- **Capacity**: initial 65536 instances, grow 2× when full. GPU buffer reallocation + copy on grow.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/vgeo/GpuInstance.h` | **public** | **H** | GPU struct + flags |
| Create | `include/miki/vgeo/SceneBuffer.h` | **public** | **H** | CPU mirror + upload |
| Create | `shaders/vgeo/scene_upload.slang` | internal | L | Dirty instance copy compute |
| Create | `src/miki/vgeo/SceneBuffer.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_scene_buffer.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define GpuInstance.h + SceneBuffer.h (heat H)

      **Signatures** (`GpuInstance.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `GpuInstance` | See layout above (128B, flags = layerBits:16 \| displayStyle:4 \| lineStyle:4 \| assemblyLevel:4 \| analysisOverlay:4) | `alignas(16)`, 128B, trivially copyable |
      | `SelectionMask` | `enum class : u32 { None=0, Selected=1<<0, Hovered=1<<1, Ghosted=1<<2, Isolated=1<<3 }` | Bitmask |
      | `DisplayStyle` | `enum class : u8 { Shaded=0, ShadedEdges=1, Wireframe=2, HLR=3, HLR_VisibleOnly=4, XRay=5, Ghosted=6, Realistic=7, NoShading=8, Matcap=9, Arctic=10, Pen=11, Artistic=12, Sketchy=13 }` | 14 modes, 4 bits |

      **Signatures** (`SceneBuffer.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `SceneBuffer::Create` | `(IDevice&, uint32_t initialCapacity = 65536) -> Result<SceneBuffer>` | `[[nodiscard]]` static |
      | `SceneBuffer::Add` | `(GpuInstance const&) -> Result<uint32_t>` | Returns instance index |
      | `SceneBuffer::Remove` | `(uint32_t instanceIndex) -> void` | Tombstone |
      | `SceneBuffer::Update` | `(uint32_t instanceIndex, GpuInstance const&) -> void` | Marks dirty |
      | `SceneBuffer::Upload` | `(ICommandBuffer&) -> void` | Dispatches dirty upload |
      | `SceneBuffer::GetGpuBuffer` | `() const noexcept -> BufferHandle` | `[[nodiscard]]` |
      | `SceneBuffer::GetCpuMirror` | `() const noexcept -> span<GpuInstance const>` | `[[nodiscard]]` |
      | `SceneBuffer::GetInstanceCount` | `() const noexcept -> uint32_t` | `[[nodiscard]]` |

      **Constraints**: `static_assert(sizeof(GpuInstance) == 128)`, namespace `miki::vgeo`
      `[verify: compile]`

- [x] **Step 2**: Implement SceneBuffer.cpp + scene_upload.slang
      `[verify: compile]`

- [x] **Step 3**: Unit tests
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(SceneBuffer, CreateValid)` | Positive | Create returns valid instance | 1-2 |
| `TEST(SceneBuffer, AddReturnStableIndex)` | Positive | Add returns sequential indices | 1-3 |
| `TEST(SceneBuffer, RemoveTombstone)` | Positive | Remove sets entityId=0, index reusable | 1-3 |
| `TEST(SceneBuffer, UpdateMarksDirty)` | Positive | After Update, Upload transfers data | 1-3 |
| `TEST(SceneBuffer, CpuMirrorMatchesGpu)` | Positive | GetCpuMirror reflects Add/Update/Remove | 1-3 |
| `TEST(SceneBuffer, GrowOnFull)` | Boundary | Exceeding initial capacity triggers realloc | 1-3 |
| `TEST(SceneBuffer, UploadOnlyDirty)` | Positive | Only dirty instances transferred (verify dispatch count) | 2-3 |
| `TEST(SceneBuffer, UploadNoDirty_Noop)` | Boundary | No dirty → no dispatch | 2-3 |
| `TEST(SceneBuffer, GpuInstance_Layout)` | Positive | sizeof=128, alignof>=16, field offsets correct | 1,3 |
| `TEST(SceneBuffer, EntityIdRoundTrip)` | Positive | Add GpuInstance with entityId → GetCpuMirror → same entityId | 1-3 |
| `TEST(SceneBuffer, MoveSemantics)` | State | Move leaves source empty | 1-3 |
| `TEST(SceneBuffer, EndToEnd_AddUploadReadback)` | **Integration** | Add 1000 instances → Upload → GPU readback → verify | 2-3 |

## Design Decisions

- **Byte-granularity dirty bits** (not std::vector<bool>): simpler indexing, negligible overhead at 65K instances (65KB). Avoids bitset thread-safety complexity.
- **Free-list LIFO reuse**: Remove() pushes index onto freeList_; Add() pops from freeList_ first. O(1) both directions.
- **Staging layout**: `[dirtyIndices: N*4B][instanceData: N*128B]` — single MapBuffer call, two descriptor bindings with offsets.
- **scene_upload.slang uses RWByteAddressBuffer** (not RWStructuredBuffer<GpuInstance>) to avoid Slang struct layout mismatches. 32×uint Load/Store per instance.
- **Grow uses CopyBuffer** via temporary command buffer + WaitIdle. Acceptable for rare reallocation (doubling from 65K→131K). Phase 6b may add async grow.
- **SlangCompiler parameter added to Create()**: matches HiZPyramid/GpuPrefixSum pattern — shader compilation at Create() time.

## Implementation Notes

Contract check: PASS — all 17 items verified.
20/20 tests pass (9 CPU-only layout + 11 GPU compute upload via Vulkan readback).
