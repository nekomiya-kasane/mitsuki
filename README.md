# miki

Multi-backend GPU rendering engine for CAD/CAE applications, written in C++23.

miki targets production-grade CAD/CAE visualization with zero-overhead abstractions across Vulkan 1.4, Direct3D 12, WebGPU, and OpenGL 4.3. The engine achieves zero CPU draw calls in steady state through GPU-driven rendering, visibility buffers, and task/mesh shader pipelines.

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Key Features](#key-features)
- [Supported Backends](#supported-backends)
- [Build Requirements](#build-requirements)
- [Building](#building)
- [Project Structure](#project-structure)
- [Shader System](#shader-system)
- [Render Graph](#render-graph)
- [Testing](#testing)
- [Demos](#demos)
- [Third-Party Dependencies](#third-party-dependencies)
- [License](#license)

---

## Architecture Overview

miki is organized into 11 architectural layers:

```
Layer 11  Application & Integration    SDK, Editor, Cloud Render, LLM Agent Adapter
Layer 10  Plugin / Extension           IPlugin lifecycle, PluginRegistry, extension points
Layer 9   Interactive Tools            Gizmo, Snap, CommandBus, OpHistory, MeshEditor
Layer 8b  CAE / DFM / PointCloud       FEM visualization, STEP/JT/glTF import, DFM analysis
Layer 8a  CAD Core                     CadScene, GPU HLR, SectionPlane, OIT, PMI, Explode
Layer 7   Rendering Layer Stack        Scene / Preview / Overlay / HUD per-layer render graph
Layer 6   GPU-Driven Geometry          Task/Mesh amplification, VisBuffer, ClusterDAG
Layer 5   Core Rendering               Deferred pipeline, PBR (DSPBR), shadow, post-processing
Layer 4   Render Graph                 DAG construction, multi-queue scheduling, barrier synthesis
Layer 3   Frame & Sync                 Triple-buffered frames, timeline semaphores, async compute
Layer 2   Shader Toolchain             Slang compiler, multi-target compilation, hot-reload
Layer 1   Foundation                   RHI, core types, error codes, structured logging, debug
```

Design principles:

- **Zero-overhead abstraction on Tier 1 backends.** Vulkan 1.4 and D3D12 achieve native-level performance through CRTP compile-time dispatch. No virtual calls in hot paths.
- **Explicit, not implicit.** RHI is a thin vocabulary layer. No hidden resource tracking or implicit barrier insertion. The render graph handles all synchronization policy.
- **Single-source shading.** One `.slang` file compiles to SPIR-V, DXIL, GLSL, WGSL, and MSL. No per-backend shader forks.
- **Deterministic execution.** Same graph and same inputs produce an identical GPU command stream, enabling golden-image CI with sub-ULP tolerance.

---

## Key Features

### Rendering

- Visibility buffer decoupling geometry from materials (single PSO for all opaque geometry)
- Task/mesh shader hierarchical culling (instance, meshlet, triangle)
- 3-bucket macro-binning with GPU-driven draw submission
- Bindless descriptors and buffer device address (BDA)
- Reverse-Z depth ([1, 0] range) for precision at distance
- Clustered light culling supporting 4096 simultaneous lights (Tier 1)

### Materials

- Layered DSPBR BSDF (Dassault Enterprise PBR, 8 material layers with conditional skip)
- Metallic-roughness and specular-glossiness PBR workflows
- Clear coat, sheen, subsurface scattering, anisotropy, iridescence

### Post-Processing

- Temporal anti-aliasing (TAA), FXAA, contrast-adaptive sharpening (CAS)
- Screen-space ambient occlusion (SSAO, GTAO), ray-traced ambient occlusion (RTAO)
- Screen-space reflections (SSR), bloom, depth of field, motion blur
- Tone mapping, color grading, outline rendering

### CAD-Specific

- GPU hidden-line removal (HLR)
- Section planes and section volumes
- GPU-accelerated picking and measurement
- Draft angle analysis, boolean preview, exploded views
- Display modes: wireframe, shaded with edges, X-ray/ghost, NPR illustration

### CAE Visualization

- FEM mesh rendering with per-element/node attributes
- Scalar field visualization, streamlines, isosurfaces
- Tensor glyph rendering, point cloud processing

### Lighting

- Clustered forward/deferred hybrid
- Image-based lighting (IBL) with prefiltered environment maps
- Area lights with linearly transformed cosines (LTC)
- Dynamic diffuse global illumination (DDGI)
- ReSTIR for many-light sampling

### Shadows

- Cascaded shadow maps (CSM)
- Virtual shadow maps (VSM)
- Shadow atlas management

### Ray Tracing

- RT reflections, RT shadows, RT global illumination
- Path tracer, denoiser integration

### XR

- Stereo rendering, foveated rendering, reprojection

### Debug and Profiling

- Wireframe overlay, overdraw visualization, mip-level visualization
- Attribute visualization, LOD overlay, GPU debug lines
- Structured JSON logging, CPU/GPU profiling, RenderDoc/PIX integration

---

## Supported Backends

| Tier | Backend | API | Shader IR | Key Capabilities |
|------|---------|-----|-----------|-----------------|
| T1 | Vulkan | 1.4 (Roadmap 2026) | SPIR-V | Descriptor heap, mesh shaders, ray query, timeline semaphores, async compute |
| T1 | Direct3D 12 | Agility SDK 1.719+ | DXIL | Root signature, descriptor heap, mesh shaders, DXR, work graphs, enhanced barriers |
| T2 | Vulkan Compat | 1.1 + extensions | SPIR-V | Traditional descriptor sets, multi-draw indirect |
| T3 | WebGPU | Dawn/wgpu | WGSL | Bind groups, single queue |
| T4 | OpenGL | 4.3+ | GLSL 4.30 | Direct state access, multi-draw indirect, UBO/SSBO |

Backend selection is performed at build time via CMake options. All enabled backends are compiled into the binary simultaneously. Runtime backend switching is supported through full device teardown and recreation.

---

## Build Requirements

- **C++ Standard**: C++23
- **Build System**: CMake 3.21+ with Ninja generator
- **Compiler**: Clang (with libc++) or clang-cl on Windows
- **Platform**: Windows (primary), WebAssembly via Emscripten
- **Vulkan SDK**: Required when `MIKI_BUILD_VULKAN=ON`

---

## Building

Clone the repository with submodules:

```bash
git clone --recurse-submodules https://github.com/nekomiya-kasane/mitsuki.git
cd mitsuki
```

### Using CMake Presets

The project provides several CMake presets for common configurations:

```bash
# Debug build (default: Vulkan + D3D12 + OpenGL + WebGPU, tests enabled)
cmake --preset debug
cmake --build --preset debug

# Release build
cmake --preset release
cmake --build --preset release

# Vulkan-only debug build
cmake --preset debug-vulkan
cmake --build --preset debug-vulkan

# D3D12-only debug build
cmake --preset debug-d3d12
cmake --build build/debug-d3d12

# WebGPU-only debug build
cmake --preset debug-webgpu
cmake --build --preset debug-webgpu

# All backends enabled
cmake --preset debug-full
cmake --build build/debug-full

# WebAssembly (Emscripten + WebGPU)
cmake --preset wasm-emscripten
cmake --build build/wasm
```

Note: `cmake --build --preset <name>` is used where a matching build preset is defined in `CMakePresets.json`. For configurations without a build preset (`debug-d3d12`, `debug-full`, `wasm-emscripten`, `ubsan`), the build directory is specified directly.

### Sanitizer Presets

```bash
# AddressSanitizer
cmake --preset asan
cmake --build --preset asan

# ThreadSanitizer
cmake --preset tsan
cmake --build --preset tsan

# UndefinedBehaviorSanitizer
cmake --preset ubsan
cmake --build build/ubsan
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `MIKI_BUILD_VULKAN` | `ON` | Build Vulkan Tier 1 backend |
| `MIKI_BUILD_D3D12` | `ON` | Build D3D12 backend (Windows only) |
| `MIKI_BUILD_OPENGL` | `ON` | Build OpenGL 4.3+ Tier 4 backend |
| `MIKI_BUILD_WEBGPU` | `ON` | Build WebGPU Tier 3 backend (Dawn) |
| `MIKI_BUILD_GLFW_BACKEND` | `ON` | Build GLFW window backend |
| `MIKI_BUILD_TESTS` | `ON` | Build unit and integration tests |
| `MIKI_BUILD_EXAMPLES` | `ON` | Build example applications |
| `MIKI_ENABLE_ASAN` | `OFF` | Enable AddressSanitizer |
| `MIKI_ENABLE_TSAN` | `OFF` | Enable ThreadSanitizer |
| `MIKI_ENABLE_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |
| `MIKI_CI` | `OFF` | Enable CI mode (warnings as errors) |

### Running Tests

```bash
# Run tests with the debug preset
ctest --preset debug

# Run tests with verbose output (CI mode)
ctest --preset ci
```

---

## Project Structure

```
mitsuki/
  CMakeLists.txt              Root build configuration
  CMakePresets.json            Build presets (debug, release, sanitizers, WASM)
  include/miki/               Public headers
    core/                     Foundation types, error codes, math, allocators
    debug/                    Profiling, logging, crash handling, debug markers
    frame/                    Frame management, sync scheduling, deferred destruction
    platform/                 Window management, event system, GLFW backend
    rendergraph/              Render graph builder, compiler, executor, barrier synthesis
    resource/                 Upload manager, staging ring, readback ring
    rhi/                      Rendering hardware interface (device, command buffer, pipeline)
      adaptation/             Tier adaptation layer
      backend/                Per-backend implementations (Vulkan, D3D12, OpenGL, WebGPU)
      validation/             Runtime validation layer
    shader/                   Slang compiler wrapper, permutation cache, hot-reload
    test/                     Test utilities (visual regression)
  src/miki/                   Implementation sources (mirrors include/ layout)
  shaders/
    miki/                     Slang shader modules (13 modules, 90+ files)
      cad/                    CAD utilities (HLR, section, pick, measure, explode)
      cae/                    CAE visualization (FEM, scalar field, streamlines)
      debug/                  Debug overlays (wireframe, overdraw, mip viz)
      lighting/               Lighting models (clustered, IBL, area lights, DDGI, ReSTIR)
      postfx/                 Post-processing effects (TAA, SSAO, bloom, SSR, tone mapping)
    passes/                   Render pass shaders (geometry, deferred resolve, culling)
    tests/                    Slang feature probe tests (29 probes across 5 targets)
  tests/                      C++ unit and integration tests
  demos/                      Demo applications
    rhi/                      RHI-level demos (triangle, torus, streaming upload, pipeline library)
    rendergraph/              Render graph demos (multi-mesh scenes)
    platform/                 Window manager demo
    debug/                    Structured logger demo
  examples/                   Example applications
  specs/                      Design specifications and architecture documents
  tools/                      Development tools (debug visualization, MCP servers, LUT generators)
  cmake/                      CMake modules, compiler flags, toolchain files
  third_party/                Vendored dependencies (as Git submodules)
```

---

## Shader System

miki uses [Slang](https://shader-slang.com/) as its single-source shading language. A single `.slang` file compiles to all target IRs (SPIR-V, DXIL, GLSL, WGSL, MSL) without per-backend forks.

The shader pipeline is organized into 13 precompiled modules:

| Module | Description |
|--------|-------------|
| `miki-core` | Types, constants, bindless interface, push constants, color space, packing |
| `miki-math` | Spherical harmonics, noise, sampling, quaternion, matrix utilities |
| `miki-brdf` | GGX, diffuse, clear coat, sheen, SSS, anisotropy, iridescence, DSPBR |
| `miki-geometry` | Meshlet, culling, Hi-Z, LOD, macro-binning, software rasterizer, visibility buffer |
| `miki-lighting` | Clustered, IBL, area lights, DDGI, ReSTIR, LTC LUT |
| `miki-shadow` | Virtual shadow maps, cascaded shadow maps, shadow atlas |
| `miki-postfx` | Bloom, DoF, motion blur, tone mapping, TAA, FXAA, CAS, SSAO, GTAO, RTAO, SSR |
| `miki-debug` | Wireframe, overdraw, mip visualization, attribute visualization, LOD overlay, GPU lines |
| `miki-cad` | Hidden-line removal, section planes, picking, measurement, boolean preview, draft angle, explode |
| `miki-cae` | FEM, scalar fields, streamlines, isosurfaces, tensor glyphs, point clouds |
| `miki-rt` | Ray tracing: reflections, shadows, GI, path tracer, denoiser |
| `miki-xr` | Stereo rendering, foveated rendering, reprojection |
| `miki-neural` | Neural textures, neural denoiser, neural radiance cache (portable MLP) |

Key capabilities of the shader pipeline:

- **Precompiled modules**: `.slang` sources are compiled to `.slang-module` at build time for fast incremental builds.
- **Permutation cache**: 64-bit bitfield keys map to preprocessor defines with LRU in-memory cache and content-hashed disk cache.
- **Hot-reload**: Filesystem watcher triggers per-module recompilation. Sub-100ms reload for typical shaders.
- **Feature probing**: 29 automated probes validate Slang codegen correctness across all 5 targets at CI time.
- **Async pipeline compilation**: Background thread pool for PSO creation with priority-based batch scheduling.

---

## Render Graph

The render graph framework provides declarative, automatic GPU synchronization:

- Passes declare read/write/create on logical resources. No explicit barriers in pass code.
- The graph compiler synthesizes optimal barriers with split release/acquire for maximum overlap.
- Multi-queue scheduling across graphics, async compute, and transfer queues with timeline semaphore synchronization.
- Transient resource aliasing achieves 30-50% VRAM savings on render targets.
- Graph structural hashing enables caching: unchanged graphs skip recompilation entirely.
- Conditional passes produce zero overhead when disabled (no barriers, no allocations, no recording).
- Plugin-extensible via `IRenderGraphExtension` for custom passes without core recompilation.

---

## Testing

The test suite covers:

- **RHI layer**: Device creation, resource management, command recording across all backends
- **Shader pipeline**: Compilation, reflection, permutation cache, feature probes, layout validation, async compilation, managed pipelines, batch compilation
- **Render graph**: Graph construction, barrier synthesis, transient aliasing, multi-queue scheduling
- **Platform**: Window management, event simulation, surface integration
- **Debug**: Structured logging
- **Shader modules**: 40 Slang domain tests across all 13 shader modules

Run tests via CTest:

```bash
ctest --preset debug --output-on-failure
```

---

## Demos

| Demo | Description |
|------|-------------|
| `rhi_triangle_demo` | Minimal triangle rendering through the RHI |
| `rhi_torus_demo` | Torus mesh with per-vertex lighting |
| `rhi_torus_demo_multi` | Multi-object torus rendering |
| `rhi_streaming_upload_demo` | Dynamic buffer upload via staging ring |
| `rhi_pipeline_library_demo` | Pipeline caching and library management |
| `rg_torus_cube_demo` | Render graph orchestrated scene |
| `rg_mesh_torus_cube_demo` | Multi-mesh render graph demo |
| `window_manager_demo` | GLFW window management and event handling |
| `logger_demo` | Structured logging system demonstration |

---

## Third-Party Dependencies

All dependencies are vendored as Git submodules under `third_party/`:

| Library | Purpose |
|---------|---------|
| [GLFW](https://github.com/glfw/glfw) | Cross-platform window and input management |
| [GLM](https://github.com/g-truc/glm) | Mathematics library for graphics |
| [Google Test](https://github.com/google/googletest) | C++ testing framework |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | GPU memory allocation for Vulkan |
| [D3D12 Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) | GPU memory allocation for D3D12 |
| [DirectX Headers](https://github.com/microsoft/DirectX-Headers) | D3D12 API headers |
| [Slang](https://github.com/shader-slang/slang) | Shader compiler (single-source to SPIR-V/DXIL/GLSL/WGSL/MSL) |
| [Dear ImGui](https://github.com/ocornut/imgui) | Immediate-mode GUI for debug panels |
| [stb](https://github.com/nothings/stb) | Image loading/writing utilities |
| [FreeType](https://github.com/freetype/freetype) | Font rendering |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing and serialization |

---

## License

See the repository for license information.
