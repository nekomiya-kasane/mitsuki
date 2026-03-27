# T1a.2.1 — ErrorCode + Result + Types

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: Foundation
**Roadmap Ref**: `roadmap.md` L325 — `ErrorCode.h`, `Result<T>`, `Types.h` (math types, `alignas(16)`)
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.1.1 | LLVM/libc++ Toolchain + CMake Skeleton | Not Started | Root CMake, C++23 module support |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/core/ErrorCode.h` | **public** | **H** | `ErrorCode` enum class with module error ranges. Cross-phase error type. |
| `include/miki/core/Result.h` | **public** | **H** | `Result<T>` = `std::expected<T, ErrorCode>`. Monadic chaining. |
| `include/miki/core/Types.h` | **public** | **H** | `float3`, `float4`, `float4x4`, `AABB`, `BoundingSphere`, `FrustumPlanes`, `Ray`, `Plane`. All `alignas(16)`. |
| `src/miki/core/CMakeLists.txt` | internal | L | `miki::core` library target |
| `tests/unit/test_foundation.cpp` | internal | L | Unit tests for Foundation types |

- **Error model**: `ErrorCode` is the root error type; `Result<T>` wraps it. No exceptions.
- **Thread safety**: All types are value types — thread-safe by design (immutable after construction).
- **GPU constraints**: `Types.h` types are `alignas(16)` with `static_assert(sizeof)` for GPU struct compatibility.
- **Invariants**: `ErrorCode::Ok` == 0; all error codes are non-zero; module ranges do not overlap.

### Downstream Consumers

- `ErrorCode.h` (**public**, heat **H**):
  - T1a.3.1 (same Phase): `RhiTypes.h` uses `ErrorCode` for RHI error codes
  - T1a.3.2 (same Phase): `IDevice` returns `Result<T>` using `ErrorCode`
  - Phase 1b..15b: ALL phases use `ErrorCode` as the universal error type
- `Result.h` (**public**, heat **H**):
  - T1a.3.2: `IDevice::CreateFromExisting() -> Result<unique_ptr<IDevice>>`
  - T1a.5.1, T1a.6.1, T1a.8.1: All device creation returns `Result<>`
  - Phase 2+: Every API returning errors uses `Result<T>`
- `Types.h` (**public**, heat **H**):
  - T1a.3.1: `RhiTypes.h` references `float4x4` for transforms
  - T1a.9.1: `OrbitCamera` uses `float3`, `float4x4`
  - Phase 2: Material system uses `float3` for colors
  - Phase 5: ECS uses `AABB`, `BoundingSphere`, `FrustumPlanes`, `Ray`

### Upstream Contracts

- T1a.1.1: CMake project with C++23 support, toolchain validated (headers/cpp, no modules)

### Technical Direction

- **C++23 `std::expected`**: `Result<T>` = `std::expected<T, ErrorCode>`. Use monadic `.and_then()` / `.transform()` / `.or_else()` chaining.
- **glm interop**: `Types.h` types are thin wrappers or aliases over `glm::vec3/vec4/mat4` internally, but the public API uses miki's own types for ABI stability. `static_assert` sizes match GPU expectations.
- **Module error ranges**: Each module has a reserved range in `ErrorCode` (see `.windsurfrules` 5.6). Ranges must not overlap.
- **`alignas(16)` discipline**: All GPU-shared types are 16-byte aligned with explicit padding. `static_assert(sizeof(float4x4) == 64)`, etc.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/core/ErrorCode.h` | **public** | **H** | Universal error type — 10+ phase consumers |
| Create | `include/miki/core/Result.h` | **public** | **H** | `std::expected<T, ErrorCode>` alias |
| Create | `include/miki/core/Types.h` | **public** | **H** | Math types — GPU-compatible, `alignas(16)` |
| Create | `src/miki/core/CMakeLists.txt` | internal | L | Library target |
| Create | `tests/unit/test_foundation.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define `ErrorCode` + `Result<T>`
      **Files**: `ErrorCode.h` (**public** H), `Result.h` (**public** H)

      **Signatures** (`ErrorCode.h`):

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `ErrorCode` | `enum class : uint32_t { Ok = 0, InvalidArgument = 0x0001, OutOfMemory = 0x0002, NotSupported = 0x0003, NotImplemented = 0x0004, DeviceLost = 0xF000, ... }` | Module ranges per `.windsurfrules` 5.6 |
      | `ToString(ErrorCode)` | `(ErrorCode) -> std::string_view` | `[[nodiscard]]` `constexpr` |

      **Signatures** (`Result.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `Result<T>` | `template<typename T> using Result = std::expected<T, ErrorCode>` | — |
      | `VoidResult` | `using VoidResult = std::expected<void, ErrorCode>` | — |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | `ErrorCode::Ok` | `== 0` |
      | Namespace | `miki::core` |
      | Ranges | Non-overlapping per module |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Define math types in `Types.h`
      **Files**: `Types.h` (**public** H)

      **Signatures** (`Types.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `float2` | `{ x:f32, y:f32 }` | `alignas(8)` |
      | `float3` | `{ x:f32, y:f32, z:f32, _pad:f32 }` | `alignas(16)` |
      | `float4` | `{ x:f32, y:f32, z:f32, w:f32 }` | `alignas(16)` |
      | `float4x4` | `{ columns[4]:float4 }` | `alignas(16)` |
      | `uint2` | `{ x:u32, y:u32 }` | `alignas(8)` |
      | `uint3` | `{ x:u32, y:u32, z:u32, _pad:u32 }` | `alignas(16)` |
      | `uint4` | `{ x:u32, y:u32, z:u32, w:u32 }` | `alignas(16)` |
      | `AABB` | `{ min:float3, max:float3 }` | `alignas(16)` |
      | `BoundingSphere` | `{ center:float3, radius:f32 }` | `alignas(16)` |
      | `FrustumPlanes` | `{ planes[6]:float4 }` | `alignas(16)` |
      | `Ray` | `{ origin:float3, direction:float3 }` | `alignas(16)` |
      | `Plane` | `{ normal:float3, distance:f32 }` | `alignas(16)` |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | `static_assert(sizeof(float3))` | `== 16` |
      | `static_assert(sizeof(float4))` | `== 16` |
      | `static_assert(sizeof(float4x4))` | `== 64` |
      | `static_assert(sizeof(AABB))` | `== 32` |
      | `static_assert(sizeof(BoundingSphere))` | `== 16` (packed into float3.w) |
      | `static_assert(sizeof(FrustumPlanes))` | `== 96` |
      | Namespace | `miki::core` |

      **Acceptance**: compiles, static_asserts pass
      `[verify: compile]`

- [x] **Step 3**: CMake target + unit tests
      **Files**: `src/miki/core/CMakeLists.txt` (internal L), `tests/unit/test_foundation.cpp` (internal L)
      Create `miki::core` target. Tests: ErrorCode range validation, Result monadic chaining,
      Types sizeof/alignof assertions, basic math operations.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(ErrorCode, OkIsZero)` | Unit | Step 1 — ErrorCode::Ok == 0 | 1 |
| `TEST(ErrorCode, RangesNoOverlap)` | Unit | Step 1 — module ranges | 1 |
| `TEST(ErrorCode, ToString)` | Unit | Step 1 — string conversion | 1 |
| `TEST(Result, MonadicChain)` | Unit | Step 1 — and_then/transform/or_else | 1 |
| `TEST(Result, VoidResult)` | Unit | Step 1 — void expected | 1 |
| `TEST(Types, Float3Size)` | Unit | Step 2 — sizeof == 16 | 2 |
| `TEST(Types, Float4x4Size)` | Unit | Step 2 — sizeof == 64 | 2 |
| `TEST(Types, AABBAlignment)` | Unit | Step 2 — alignas(16) | 2 |

## Design Decisions

- **INTERFACE library**: `miki::core` is header-only (INTERFACE target). No .cpp files needed for Foundation types.
- **BoundingSphere/Plane packing**: Radius and distance stored in `float3._pad` to achieve `sizeof==16` without a separate field. Getter/setter methods for clarity.
- **No glm dependency in public headers**: Types.h defines its own structs for ABI stability. glm linked as INTERFACE dep for future internal use but not exposed in public types.
- **gtest_discover_tests**: Uses CMake's GoogleTest module for automatic test discovery with `Foundation.` prefix.

## Implementation Notes

- Contract check: PASS (30/30 items)
- Build: 2/2 new targets (test_foundation.cpp.obj + test_foundation.exe), 0 errors
- Tests: 20/20 pass (17 Foundation + 3 Toolchain)
- No TODO/STUB/FIXME in task files
