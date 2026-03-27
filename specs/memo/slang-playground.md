# Slang Playground — Design Plan

> **Goal**: A native C++ playground demo for rapid Slang shader experimentation with hot-reload.
> **Status**: PLAN (not yet implemented)
> **Date**: 2026-03-10

---

## 1. Motivation

Slang 官方提供了浏览器端 playground（shader-slang.com/slang-playground），但它：
- 只能编译，不能绑定真实 GPU 管线执行
- 无法测试 miki RHI 特性（如 dual-target SPIR-V + DXIL）
- 无法调试运行时行为（barrier、resource binding、compute dispatch）

我们需要一个**本地 native playground**，核心需求：
1. 编辑 `.slang` 文件 → 保存 → 自动重编译 → GPU 立即执行新 shader
2. 编译错误实时反馈（控制台 + 窗口 overlay）
3. 支持 vertex/fragment（全屏 quad）和 compute shader
4. 支持 SPIR-V 和 DXIL 双目标

---

## 2. Architecture Decision: 独立于 miki RHI

| 方案 | 优点 | 缺点 |
|------|------|------|
| **A. 基于 miki RHI** | 复用现有抽象；测试 miki 管线 | Phase 1a 的 pipeline 是 stub，无法真正渲染；需要等 Phase 2 |
| **B. 直接使用 Vulkan + Slang API** | 立即可用；不依赖 miki 进度；最小依赖 | 不测试 miki 抽象层 |
| **C. 混合：GLFW + Vulkan raw + miki::shader** | 复用 SlangCompiler；独立于 RHI pipeline；立即可用 | 需要少量 raw Vulkan 代码 |

**选择：方案 C**

理由：
- `miki::shader::SlangCompiler` 已经封装好 Slang session/module/compile 流程，直接复用
- GLFW 已是项目依赖，窗口创建零成本
- Raw Vulkan pipeline 代码量约 300-400 行（fullscreen quad + compute），一次性成本
- 不阻塞于 miki RHI Phase 2 进度
- 后续 miki RHI pipeline 成熟后，可以切换为 miki 版本

---

## 3. Feature Set

### 3.1 Core Features (MVP)

| # | Feature | Detail |
|---|---------|--------|
| F1 | **File watcher** | 监控指定 `.slang` 文件的 `last_write_time`，变化时触发重编译 |
| F2 | **Runtime recompilation** | 通过 `SlangCompiler::Compile()` 重编译 vertex+fragment 或 compute |
| F3 | **Error feedback** | 编译失败 → 控制台打印诊断信息 + 保持旧 pipeline 继续渲染（graceful fallback） |
| F4 | **Fullscreen quad mode** | `--mode fragment`：vertex shader 生成全屏三角形，fragment shader 是用户 playground（Shadertoy 风格） |
| F5 | **Compute mode** | `--mode compute`：dispatch compute shader，结果 readback 到 CPU 打印 |
| F6 | **Uniform push constants** | 自动注入 `iTime`, `iResolution`, `iFrame`, `iMouse`, `iKeyboard` (Shadertoy 风格) + `[playground::KEY("KeyA")]` 键盘状态 |
| F7 | **Hot pipeline swap** | 新 VkPipeline 创建成功后，下一帧原子切换；旧 pipeline 延迟销毁 |
| F8 | **Mock vertex data** | 内置多种几何体（fullscreen quad, cube, sphere, plane），通过 `--geometry` 选择 |
| F9 | **OBJ mesh loading** | `--obj <path>` 加载 .obj 文件，自动生成 VkBuffer (position + normal + texcoord) |
| F10 | **Multi-stage shader** | 用户可同时指定 vertex + fragment `.slang` 文件，两者均支持 hot-reload |
| F11 | **RenderDoc integration** | 按 `F10` 触发单帧 capture，自动保存 `.rdc` 文件；运行时动态检测 RenderDoc |
| F12 | **Nsight Graphics integration** | 按 `F11` 触发单帧 capture；运行时动态检测 Nsight SDK，无 SDK 时静默降级 |
| F13 | **Ray tracing mode** | `--mode raytrace`：raygen + miss + closest-hit，需 VK_KHR_ray_tracing_pipeline |
| F14 | **Mesh shader mode** | `--mode meshshader`：task + mesh + fragment，需 VK_EXT_mesh_shader |
| F15 | **Multi-pass mode** | `--mode multipass`：多 pass 串联（G-Buffer → lighting → post），JSON 配置驱动 |
| F16 | **Shader stdlib** | `shaders/playground/include/` 公共 import 库：math、noise、SDF、color、PBR、sampling、rand（内置 PRNG） |
| F17 | **Target Code Reflection** | 按 `F9` 输出编译产物 + reflection 到控制台/文件（SPIR-V disasm、HLSL、GLSL、WGSL、**Metal**、**CUDA** + binding/vertex/push constant 信息） |
| F18 | **Attribute-driven resource system** | Shader 通过 `[playground::XXX]` 属性自描述所需资源（buffer/texture/sampler），运行时通过 reflection 自动分配 GPU 资源。对标官方 slang-playground 核心范式。支持: `ZEROS(n)`, `BLACK(w,h)`, `BLACK_3D(w,h,d)`, `BLACK_SCREEN(sx,sy)`, `URL("...")`, `SAMPLER`, `RAND(n)`, `DATA("url")`, `TIME`, `FRAME_ID`, `MOUSE_POSITION`, `KEY("key")`, `SLIDER(def,min,max)`, `COLOR_PICK(r,g,b)` |
| F19 | **Multi-kernel compute** | 同一 shader 文件中多个 `[shader("compute")]` entry point，各自用 `[playground::CALL(x,y,z)]` / `CALL_SIZE_OF("res")` / `CALL_INDIRECT("buf",ofs)` 指定 dispatch 方式，`CALL_ONCE` 仅首帧执行 |
| F20 | **GPU printf** | `import printing;` 提供 `printf(format, args...)` 函数，基于 RWStructuredBuffer + hashed string + CPU readback 解析。输出到控制台 output 面板 |
| F21 | **Frame step controls** | 暂停/继续/单帧前进/单帧后退/重置到第 0 帧。键盘快捷键: Space=暂停切换, →=前进, ←=后退, Home=重置 |
| F22 | **Entry point discovery** | 自动解析 shader 源码中所有 `[shader("...")]` entry point，用户可通过 CLI `--entrypoint <name>` 选择。F17 反射时列出所有 entry point |

### 3.2 Nice-to-have (Post-MVP)

| # | Feature | Detail |
|---|---------|--------|
| N1 | **ImGui overlay** | 编译状态、FPS/帧时间显示、uniform 滑块（由 `SLIDER`/`COLOR_PICK` 属性自动生成）、geometry 切换、capture 状态指示、reflection 面板、entry point 选择 |
| N2 | **DXIL parallel compile** | 同时编译 SPIR-V + DXIL，对比反射差异 |
| N4 | **Shader screenshot** | F12 截图保存为 PNG |
| N5 | **Multiple shader files** | 监控整个目录，支持 `import` 依赖 |
| N6 | **Geometry shader / tessellation** | 扩展 multi-stage 支持 geometry/hull/domain stages |
| N7 | **glTF loading** | 扩展 OBJ loader 支持 glTF 2.0 (PBR materials) |
| N8 | **Nsight Aftermath** | GPU crash dump 集成，捕获 device lost 时的 GPU mini-dump |
| N9 | **Demo shader library** | 15+ 内置示例 shader，覆盖: simple print、image processing、multi-kernel、ShaderToy 效果、painting、volume slicing、2D gaussian splatting、properties、generics、operator overloading、variadic generics、automatic differentiation、atomics、lambda expressions、image-from-URL。通过 `--demo <name>` 加载 |
| N10 | **Fullscreen toggle** | F12 / 双击切换全屏显示 |
| N11 | **Compiler version display** | `--version` 显示 miki playground 版本 + Slang 编译器版本；窗口标题栏也显示 |

---

## 4. Directory Layout

```
demos/
  slang-playground/
    CMakeLists.txt
    main.cpp                      # Entry point, CLI parsing, main loop
    VulkanContext.h/.cpp          # Raw Vulkan init (instance, device, swapchain, RT/mesh ext)
    PipelineManager.h/.cpp        # Create/destroy/hot-swap VkPipeline (graphics/compute/RT/mesh)
    ResourceAllocator.h/.cpp      # Attribute-driven GPU resource allocation (F18) — parses playground::XXX → VkBuffer/VkImage/VkSampler
    PlaygroundCompiler.h/.cpp     # Compile + parse attributes + multi-kernel orchestration (F19, F22)
    PrintfParser.h/.cpp           # GPU printf readback + hashed string resolution (F20)
    FileWatcher.h/.cpp            # Polling-based file modification watcher
    GeometryProvider.h/.cpp       # Mock geometry (quad/cube/sphere) + OBJ loading
    GpuDebugger.h/.cpp            # RenderDoc + Nsight in-app capture integration
    TargetCodeReflector.h/.cpp    # Multi-target compile + reflection output (F9/F17)
    AccelStructBuilder.h/.cpp     # BLAS/TLAS builder for ray tracing mode
    MultiPassRunner.h/.cpp        # JSON-driven multi-pass orchestrator
    FrameController.h/.cpp        # Frame step controls: pause/resume/step/reset (F21)
    ShaderToy.h                   # Push constant struct (iTime, iResolution, etc.)
    third_party/
      renderdoc_app.h             # Vendored RenderDoc API header
      tiny_obj_loader.h           # Vendored tinyobjloader
shaders/
  playground/
    include/                  # ===== Shader stdlib (import-able) =====
      playground.slang        # Attribute definitions: ZEROS, BLACK, BLACK_3D, BLACK_SCREEN, URL, SAMPLER, RAND, DATA, TIME, FRAME_ID, MOUSE_POSITION, KEY, SLIDER, COLOR_PICK, CALL, CALL_SIZE_OF, CALL_INDIRECT, CALL_ONCE
      printing.slang          # GPU printf: import printing; → printf(format, args...) via RWStructuredBuffer + hashed strings
      rendering.slang         # Image output: import rendering; → outputTexture + drawPixel(loc, renderFunc)
      rand_float.slang        # Built-in Hybrid Tausworthe PRNG compute shader for RAND buffer initialization
      math_utils.slang        # remapRange, smootherstep, saturate, PI, TAU, etc.
      noise.slang             # hash, valueNoise, perlinNoise2D/3D, fbm, worleyNoise
      sdf.slang               # sdSphere, sdBox, sdTorus, sdCylinder, opUnion/Smooth/Sub
      color.slang             # hsvToRgb, rgbToHsv, linearToSrgb, srgbToLinear, palette
      sampling.slang          # hammersley, sampleHemisphereCosine, sampleSphereUniform
      pbr.slang               # GGX NDF, Smith G, fresnelSchlick, evaluateBrdf
      raymarching.slang       # rayMarch, calcNormal, softShadow, ambientOcclusion
      tonemapping.slang       # reinhardTonemap, ACESFilmic, uncharted2
    # ----- Mode: fragment (Shadertoy-style) -----
    default_frag.slang        # Default fragment shader
    fullscreen_vert.slang     # Fullscreen triangle vertex shader (built-in)
    # ----- Mode: mesh (vertex + fragment) -----
    default_vert.slang        # Default mesh vertex shader (MVP transform)
    lit_frag.slang            # Basic lit fragment shader
    # ----- Mode: raytrace -----
    default_raygen.slang      # Default ray generation shader
    default_miss.slang        # Default miss shader (gradient sky)
    default_closesthit.slang  # Default closest-hit shader (normal shading)
    # ----- Mode: meshshader -----
    default_task.slang        # Default task/amplification shader
    default_mesh.slang        # Default mesh shader (procedural cube)
    # ----- Mode: multipass -----
    multipass_example.json    # Example multi-pass configuration
    gbuffer_vert.slang        # G-Buffer vertex shader
    gbuffer_frag.slang        # G-Buffer fragment shader
    lighting_frag.slang       # Deferred lighting pass
    post_frag.slang           # Post-processing pass
    # ----- Demo shaders (N9) -----
    demos/
      simple_print.slang      # GPU printf demo
      circle.slang            # ShaderToy circle effect (TIME + FRAME_ID)
      ocean.slang             # ShaderToy ocean effect
      multiple_kernels.slang  # Multi-kernel: fill buffer → render image (CALL_ONCE + RAND)
      painting.slang          # Mouse-interactive persistent painting (MOUSE_POSITION)
      volume_slice.slang      # 3D texture volume slicing (BLACK_3D)
      gsplat2d.slang          # 2D gaussian splatting (RAND + groupshared + Atomic)
      autodiff.slang          # Automatic differentiation (fwd_diff / bwd_diff)
      atomics.slang           # Atomic operations demo
      image_url.slang         # Image loading from URL (URL + SAMPLER)
      entrypoint.slang        # Vertex + fragment entry points (compile-only)
      generics.slang          # Slang generics feature demo
      lambda.slang            # Lambda expressions demo
      slider_color.slang      # SLIDER + COLOR_PICK uniform UI demo
      key_input.slang         # KEY keyboard input demo
```

---

## 5. Technical Design

### 5.1 File Watcher

**轮询方案**（简单可靠，跨平台）：

```
每帧（或每 200ms）:
  stat = std::filesystem::last_write_time(shaderPath)
  if stat != lastStat:
    lastStat = stat
    trigger recompile
```

不使用 OS-specific file notification API（`ReadDirectoryChangesW` / `inotify`），因为：
- 轮询对单文件足够高效（单次 syscall）
- 避免平台 #ifdef
- 文件通知在编辑器"保存"行为上有 rename/delete/create 的竞态问题

### 5.2 Hot Pipeline Swap

```
Frame N:   检测到文件变化
           → SlangCompiler::Compile(vertex) + Compile(fragment)
           → 成功？创建新 VkPipeline
           → pendingPipeline_ = newPipeline
           → 失败？打印错误，保持 currentPipeline_

Frame N+1: if pendingPipeline_ != null:
             vkDeviceWaitIdle()           // 确保旧 pipeline 不在 GPU 执行
             destroy(currentPipeline_)
             currentPipeline_ = pendingPipeline_
             pendingPipeline_ = null
           render with currentPipeline_
```

生产级方案会用 per-frame fence + deferred destruction queue，但 playground 用途下 `WaitIdle` 足够。

### 5.3 Vulkan Context (Minimal)

仅需：
- `VkInstance` (validation layers enabled)
- `VkPhysicalDevice` + `VkDevice` (single queue, graphics + compute)
- `VkSurfaceKHR` + `VkSwapchainKHR` (GLFW surface)
- `VkRenderPass` (single color attachment, RGBA8)
- `VkFramebuffer` per swapchain image
- `VkCommandPool` + `VkCommandBuffer` (single-buffered for simplicity)
- Sync: `VkSemaphore` (acquire + present) + `VkFence` (frame)

约 250 行初始化代码。可考虑使用 `vk-bootstrap` 库进一步简化。

### 5.4 Push Constants Layout

```cpp
// Fragment mode (fullscreen quad, no MVP)
struct PlaygroundPushConstants {
    float iTime;           // seconds since start
    float iTimeDelta;      // frame delta time
    float iFrame;          // frame count (as float)
    float _pad0;
    float iResolution[2];  // viewport width, height
    float iMouse[2];       // mouse x, y (pixel coords)
};
static_assert(sizeof(PlaygroundPushConstants) == 32);

// Mesh mode (with MVP transform)
struct MeshPushConstants {
    float    iTime;
    float    iTimeDelta;
    float    iFrame;
    float    _pad0;
    float    iResolution[2];
    float    iMouse[2];
    float    iModelViewProj[16]; // column-major 4x4
};
static_assert(sizeof(MeshPushConstants) == 96);
```

对应 Slang 侧：

```slang
struct Globals {
    float  iTime;
    float  iTimeDelta;
    float  iFrame;
    float  _pad0;
    float2 iResolution;
    float2 iMouse;
};

[[vk::push_constant]] Globals globals;
```

### 5.5 Geometry Provider

```cpp
// GeometryProvider.h
enum class GeometryType : uint8_t {
    FullscreenQuad,   // 3 verts, no vertex buffer, SV_VertexID driven
    Cube,             // 36 verts (unindexed), position + normal + uv
    Sphere,           // UV sphere, configurable subdivisions
    Plane,            // 2-triangle quad, position + normal + uv
    ObjFile,          // Loaded from .obj file
};

struct Vertex {
    float position[3];
    float normal[3];
    float texcoord[2];
};
static_assert(sizeof(Vertex) == 32);

struct MeshData {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;     // empty = non-indexed draw
};

class GeometryProvider {
public:
    /** @brief Generate built-in geometry. */
    [[nodiscard]] static auto Generate(GeometryType iType) -> MeshData;

    /** @brief Load mesh from .obj file (tinyobjloader). */
    [[nodiscard]] static auto LoadObj(std::filesystem::path const& iPath)
        -> core::Result<MeshData>;
};
```

**Vertex buffer layout**：
- Binding 0: interleaved `Vertex` (stride 32)
- `location 0` = position (vec3, offset 0)
- `location 1` = normal (vec3, offset 12)
- `location 2` = texcoord (vec2, offset 24)

**FullscreenQuad 特殊路径**：不创建 vertex buffer，vertex shader 用 `SV_VertexID` 生成坐标。

### 5.6 Multi-Stage Shader Pipeline

**Render modes 与 shader stage 对应关系**：

| Mode | Shader Stages | Pipeline Type | Vulkan Extensions | Notes |
|------|--------------|---------------|-------------------|-------|
| `fragment` | VS (built-in) + FS (user) | Graphics | core | Shadertoy 风格，无 VB |
| `mesh` (default) | VS (user) + FS (user) | Graphics | core | 有 vertex buffer，默认 cube |
| `compute` | CS (user) | Compute | core | Dispatch + readback |
| `raytrace` | RayGen + Miss + ClosestHit (user) | RT | `VK_KHR_ray_tracing_pipeline`, `VK_KHR_acceleration_structure` | 需要 BLAS/TLAS |
| `meshshader` | Task + Mesh + FS (user) | Graphics (mesh) | `VK_EXT_mesh_shader` | 无 vertex buffer，mesh shader 生成图元 |
| `multipass` | 多 pass，每 pass 有 VS+FS (user) | Graphics x N | core | JSON 配置文件驱动 |

**Hot-reload 范围**：
- `fragment` mode：仅 fragment shader 被监控
- `mesh` mode：vertex + fragment 各自独立监控，任一变化 → 重建整个 pipeline
- `compute` mode：compute shader 被监控
- `raytrace` mode：raygen/miss/closesthit 任一变化 → 重建 RT pipeline
- `meshshader` mode：task/mesh/fragment 任一变化 → 重建 pipeline
- `multipass` mode：任一 pass 的 shader 变化 → 重建该 pass 的 pipeline；JSON 变化 → 重建全部

**FileWatcher 扩展**：支持多文件监控列表

```cpp
class FileWatcher {
public:
    auto Watch(std::filesystem::path const& iPath) -> void;
    auto Poll() -> std::vector<std::filesystem::path>;  // returns changed files
};
```

### 5.7 Default Shader Templates

#### 5.7.1 Default Fragment Shader (Shadertoy-style)

```slang
// shaders/playground/default_frag.slang
struct Globals {
    float  iTime;
    float  iTimeDelta;
    float  iFrame;
    float  _pad0;
    float2 iResolution;
    float2 iMouse;
};

[[vk::push_constant]] Globals globals;

struct FSInput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

[shader("fragment")]
float4 fragmentMain(FSInput input) : SV_Target0 {
    float2 uv = input.uv;
    float3 col = 0.5 + 0.5 * cos(globals.iTime + uv.xyx + float3(0, 2, 4));
    return float4(col, 1.0);
}
```

#### 5.7.2 Fullscreen Triangle (Built-in)

```slang
// shaders/playground/fullscreen_vert.slang
struct VSOutput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    // Generate fullscreen triangle (3 vertices, no vertex buffer)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}
```

#### 5.7.3 Default Mesh Vertex Shader

```slang
// shaders/playground/default_vert.slang
struct Globals {
    float  iTime;
    float  iTimeDelta;
    float  iFrame;
    float  _pad0;
    float2 iResolution;
    float2 iMouse;
    float4x4 iModelViewProj;
};

[[vk::push_constant]] Globals globals;

struct VSInput {
    float3 position : POSITION0;
    float3 normal   : NORMAL0;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 normal   : NORMAL0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    output.position = mul(globals.iModelViewProj, float4(input.position, 1.0));
    output.normal   = input.normal;
    output.texcoord = input.texcoord;
    output.worldPos = input.position;
    return output;
}
```

#### 5.7.4 Basic Lit Fragment Shader

```slang
// shaders/playground/lit_frag.slang
struct Globals {
    float  iTime;
    float  iTimeDelta;
    float  iFrame;
    float  _pad0;
    float2 iResolution;
    float2 iMouse;
    float4x4 iModelViewProj;
};

[[vk::push_constant]] Globals globals;

struct FSInput {
    float4 position : SV_Position;
    float3 normal   : NORMAL0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

[shader("fragment")]
float4 fragmentMain(FSInput input) : SV_Target0 {
    float3 lightDir = normalize(float3(1, 1, 1));
    float  NdotL    = max(dot(normalize(input.normal), lightDir), 0.0);
    float3 color    = float3(0.8, 0.8, 0.8) * (0.2 + 0.8 * NdotL);
    return float4(color, 1.0);
}
```

### 5.9 GPU Debugger Integration (RenderDoc + Nsight)

**设计原则**：
- 零编译期依赖——两者均通过运行时动态加载检测
- 无 SDK 时静默降级（仅打印 "RenderDoc/Nsight not detected"）
- 统一接口 `GpuDebugger`，上层代码不关心具体后端

#### 5.9.1 RenderDoc In-App API

```cpp
// GpuDebugger.h
#include "renderdoc_app.h"  // single header, vendored in project

class GpuDebugger {
public:
    GpuDebugger();

    /** @brief Check if any GPU debugger is attached. */
    [[nodiscard]] auto IsAttached() const -> bool;

    /** @brief Trigger a single-frame capture.
     *  Call before the frame's vkQueueSubmit / vkQueuePresentKHR.
     */
    auto StartFrameCapture() -> void;

    /** @brief End the frame capture.
     *  Call after vkQueuePresentKHR.
     */
    auto EndFrameCapture() -> void;

    /** @brief Get path of the last saved capture file. */
    [[nodiscard]] auto GetLastCapturePath() const -> std::string;

private:
    RENDERDOC_API_1_6_0* rdocApi_ = nullptr;
    bool                 nsightAvailable_ = false;
    bool                 captureRequested_ = false;
};
```

**RenderDoc 检测流程** (Windows):

```cpp
GpuDebugger::GpuDebugger() {
    // RenderDoc: check if renderdoc.dll is already loaded
    // (app was launched from RenderDoc UI or injected)
    if (HMODULE mod = GetModuleHandleA("renderdoc.dll")) {
        auto getApi = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        if (getApi) {
            getApi(eRENDERDOC_API_Version_1_6_0, (void**)&rdocApi_);
        }
    }
    if (rdocApi_) {
        rdocApi_->SetCaptureOptionU32(
            eRENDERDOC_Option_CaptureCallstacks, 1);
        rdocApi_->SetCaptureOptionU32(
            eRENDERDOC_Option_APIValidation, 1);
        std::println("[GpuDebugger] RenderDoc API v{}.{}.{} attached",
            /* major, minor, patch from GetAPIVersion */);
    }
}
```

**Capture 触发** (按 F10):

```cpp
// In main loop:
if (keyPressed(F10) && debugger.IsAttached()) {
    debugger.StartFrameCapture();   // before rendering
    // ... render frame ...
    debugger.EndFrameCapture();     // after present
    std::println("[GpuDebugger] Capture saved: {}",
        debugger.GetLastCapturePath());
}
```

**RenderDoc API 关键函数**:

| Function | Usage |
|----------|-------|
| `StartFrameCapture(NULL, NULL)` | 开始捕获当前帧 |
| `EndFrameCapture(NULL, NULL)` | 结束捕获并写入 .rdc 文件 |
| `SetCaptureFilePathTemplate(path)` | 设置 .rdc 保存路径模板 |
| `GetCapture(idx, ...)` | 获取已保存 capture 的文件路径 |
| `IsFrameCapturing()` | 查询当前是否在 capture 中 |
| `LaunchReplayUI(1, nullptr)` | 自动打开 RenderDoc UI 查看 capture |

#### 5.9.2 Nsight Graphics SDK

```cpp
// Nsight: runtime detection via NGFX_Injection
// SDK header: NGFX_Injection.h (from Nsight Graphics install)
// DLL: NGFX_Injection.dll (loaded dynamically)

// Detection:
//   1. Search Nsight install dirs under
//      "C:\Program Files\NVIDIA Corporation\Nsight Graphics *"
//   2. LoadLibrary("NGFX_Injection.dll")
//   3. GetProcAddress for NGFX_Injection_* functions
//   4. If any step fails → nsightAvailable_ = false (silent)
```

**Nsight capture 流程**:

```
NGFX_Injection_EnumerateInstallations()  → find latest Nsight install
NGFX_Injection_EnumerateActivities()     → select Frame Debugger
NGFX_Injection_InjectToProcess()         → inject into current process
NGFX_Injection_ExecuteActivityCommand()  → trigger single-frame capture
```

**限制**：
- Nsight SDK 只支持单帧捕获（下一个 frame delimiter 后开始+结束）
- 不支持任意 start/stop 范围
- 需要机器上安装了 Nsight Graphics（SDK DLL 随安装附带）

#### 5.9.3 Debugger Priority & Coexistence

| Scenario | Behavior |
|----------|----------|
| 从 RenderDoc UI 启动 playground | RenderDoc API 自动可用；F10 触发 capture |
| 从命令行启动，机器有 Nsight | 尝试加载 Nsight SDK；F11 触发 capture |
| 从命令行启动，无任何调试器 | 两者都不可用；按键打印提示信息 |
| RenderDoc + Nsight 同时存在 | RenderDoc 优先（它先注入），Nsight 作为备选 |

#### 5.9.4 `renderdoc_app.h` Vendoring

`renderdoc_app.h` 是 RenderDoc 官方的 single-header API 定义（MIT license，~500 行）。
直接 vendor 到 `demos/slang-playground/third_party/renderdoc_app.h`，不需要链接任何库。

### 5.10 Compute Mode

```
--mode compute --shader my_compute.slang --dispatch 256,1,1
```

- 创建 `VkPipeline` (compute)
- 分配 `VkBuffer` (storage, 64KB default)
- Dispatch → readback → 打印前 N 个 uint32
- 文件变化 → 重新编译 + 重新 dispatch

### 5.11 Target Code Reflection (F17)

**对标 Slang Playground 的 "Target Code" + "Reflection" 面板**，用户按 `F9` 后：

1. **多 Target 编译**：对当前 shader 同时编译到所有支持的 target
2. **Target Code 输出**：编译产物的人类可读形式
3. **Reflection 输出**：结构化的 binding / vertex input / push constant 信息

#### 输出 Targets

| Target | 输出格式 | 方法 |
|--------|----------|------|
| SPIR-V | 反汇编文本 | `spirv-dis`（Vulkan SDK 自带）或内置 SPIR-V → text 解析 |
| HLSL | HLSL 源码 | Slang 直接编译到 `SLANG_HLSL` target |
| GLSL | GLSL 源码 | Slang 编译到 `SLANG_GLSL` target |
| WGSL | WGSL 源码 | Slang 编译到 `SLANG_WGSL` target |

#### Reflection 输出内容

| 类别 | 数据 | 来源 |
|------|------|------|
| **Bindings** | set, binding, type (UBO/SSBO/Texture/Sampler), name, count | `SlangCompiler::Reflect()` → `ShaderReflection::bindings` |
| **Push Constants** | 总大小 (bytes), 各字段名+类型+offset | `ShaderReflection::pushConstantSize` + layout 遍历 |
| **Vertex Inputs** | location, format, offset, name, semantic | `ShaderReflection::vertexInputs` |
| **Thread Group Size** | X, Y, Z (compute only) | `ShaderReflection::threadGroupSize` |
| **Entry Points** | name, stage, parameter count | Slang `IComponentType::getLayout()` |

#### 实现方案

```cpp
// TargetCodeReflector.h
class TargetCodeReflector {
public:
    struct Output {
        std::string spirvDisasm;     // SPIR-V disassembly text
        std::string hlslSource;      // Generated HLSL
        std::string glslSource;      // Generated GLSL
        std::string wgslSource;      // Generated WGSL
        ShaderReflection reflection; // Structured reflection
        std::string reflectionJson;  // JSON serialized reflection
    };

    /** @brief Compile current shader to all targets and extract reflection.
     *  @param iCompiler  SlangCompiler instance (with search paths configured)
     *  @param iDesc      Base compile descriptor (source, entry point, stage)
     *  @return All target outputs + reflection, or error.
     */
    [[nodiscard]] static auto Generate(
        SlangCompiler& iCompiler,
        ShaderCompileDesc const& iDesc) -> core::Result<Output>;
};
```

**工作流**（按 F9 触发）：
```
1. 从当前 active shader 构建 ShaderCompileDesc
2. 对 4 个 target (SPIRV/HLSL/GLSL/WGSL) 分别调用 SlangCompiler::Compile()
3. SPIR-V blob → 调用 spirv-dis 或内置解析生成反汇编文本
4. HLSL/GLSL/WGSL blob → 直接转为 string（Slang 输出的就是文本）
5. 调用 SlangCompiler::Reflect() 获取结构化 reflection
6. 序列化 reflection 为 JSON
7. 输出到控制台 + 保存到 output/ 目录
```

**输出文件结构**：
```
output/
  <shader_name>.spv.disasm   # SPIR-V disassembly
  <shader_name>.hlsl         # Generated HLSL
  <shader_name>.glsl         # Generated GLSL
  <shader_name>.wgsl         # Generated WGSL
  <shader_name>.reflect.json # Reflection JSON
```

**Reflection JSON 格式**：
```json
{
    "entryPoints": [
        { "name": "fragmentMain", "stage": "fragment" }
    ],
    "bindings": [
        { "set": 0, "binding": 0, "type": "UniformBuffer", "name": "globals", "count": 1 }
    ],
    "pushConstants": {
        "size": 32,
        "fields": [
            { "name": "iTime",       "type": "float",  "offset": 0 },
            { "name": "iTimeDelta",  "type": "float",  "offset": 4 },
            { "name": "iFrame",      "type": "float",  "offset": 8 },
            { "name": "iResolution", "type": "float2", "offset": 16 },
            { "name": "iMouse",      "type": "float2", "offset": 24 }
        ]
    },
    "vertexInputs": [
        { "location": 0, "format": "RGBA32_FLOAT", "offset": 0, "name": "position" }
    ],
    "threadGroupSize": [64, 1, 1]
}
```

**ImGui 树状展示**（按 F9 后在侧边栏显示，对标官方 playground 的 ReflectionView）：

```
┌─ Target Code Reflection ─────────────────────┐
│ Shader: my_effect.slang (fragment)            │
│                                               │
│ ▼ Target Code                                 │
│   ├─ [Tab] SPIR-V  HLSL  GLSL  WGSL          │
│   └─ (syntax-highlighted source in scrollable │
│       ImGui::InputTextMultiline, read-only)   │
│                                               │
│ ▼ Reflection                                  │
│   ▼ Entry Points                              │
│     └─ fragmentMain (fragment)                │
│   ▼ Push Constants (32 bytes)                 │
│     ├─ iTime       : float   @ offset 0      │
│     ├─ iTimeDelta  : float   @ offset 4      │
│     ├─ iFrame      : float   @ offset 8      │
│     ├─ _pad0       : float   @ offset 12     │
│     ├─ iResolution : float2  @ offset 16     │
│     └─ iMouse      : float2  @ offset 24     │
│   ▷ Bindings (0)                              │
│   ▷ Vertex Inputs (0)                         │
│   ▷ Thread Group Size: N/A                    │
│                                               │
│ [Copy JSON]  [Save Files]  [Auto-refresh ☐]  │
└───────────────────────────────────────────────┘
```

**ImGui 实现要点**：

```cpp
// 在 ImGui overlay 中（N1 已有框架）
void DrawReflectionPanel(TargetCodeReflector::Output const& output) {
    if (ImGui::Begin("Target Code Reflection")) {
        // Target Code 选项卡
        if (ImGui::CollapsingHeader("Target Code", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTabBar("##targets")) {
                if (ImGui::BeginTabItem("SPIR-V")) {
                    ImGui::InputTextMultiline("##spirv", &output.spirvDisasm,
                        ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);
                    ImGui::EndTabItem();
                }
                // ... HLSL, GLSL, WGSL, Metal, CUDA tabs
                ImGui::EndTabBar();
            }
        }

        // Reflection 树
        if (ImGui::CollapsingHeader("Reflection", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto const& r = output.reflection;

            if (ImGui::TreeNode("Entry Points")) {
                for (auto const& ep : r.entryPoints)
                    ImGui::BulletText("%s (%s)", ep.name.c_str(), ep.stage.c_str());
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Push Constants",
                    ImGuiTreeNodeFlags_DefaultOpen, "Push Constants (%u bytes)", r.pushConstantSize)) {
                for (auto const& f : r.pushConstantFields)
                    ImGui::Text("  %-12s : %-8s @ offset %u", f.name.c_str(), f.type.c_str(), f.offset);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Bindings (%zu)", r.bindings.size())) {
                for (auto const& b : r.bindings)
                    ImGui::Text("  set=%u binding=%u %s \"%s\"", b.set, b.binding, b.type.c_str(), b.name.c_str());
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Vertex Inputs (%zu)", r.vertexInputs.size())) {
                for (auto const& v : r.vertexInputs)
                    ImGui::Text("  location=%u %s \"%s\"", v.location, v.format.c_str(), v.name.c_str());
                ImGui::TreePop();
            }

            if (r.threadGroupSize[0] > 0)
                ImGui::Text("Thread Group Size: [%u, %u, %u]",
                    r.threadGroupSize[0], r.threadGroupSize[1], r.threadGroupSize[2]);
        }

        // 底部按钮
        if (ImGui::Button("Copy JSON"))
            ImGui::SetClipboardText(output.reflectionJson.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Save Files"))
            ; // save to output/ directory
        ImGui::SameLine();
        ImGui::Checkbox("Auto-refresh", &autoRefreshReflection_);
    }
    ImGui::End();
}
```

**行为说明**：
- F9 触发时**打开/关闭** ImGui 反射面板（toggle），面板内容实时保持最新
- `--auto-reflect` 模式下，每次 hot-reload 自动刷新面板内容（无需再按 F9）
- **Copy JSON** 按钮将 reflection JSON 复制到系统剪贴板
- **Save Files** 按钮将所有 target code + reflection JSON 保存到 `output/` 目录
- Target Code 使用 `ImGui::InputTextMultiline` 的 ReadOnly 模式展示，支持滚动和文本选择/复制
- 面板可拖拽调整大小和位置（ImGui 默认行为）

**SPIR-V 反汇编方案**：
优先尝试调用 Vulkan SDK 自带的 `spirv-dis`（零新依赖）。
若 `spirv-dis` 不在 PATH 中，fallback 到内置简易解析（仅输出 OpName + OpDecorate）。

**与 hot-reload 的交互**：
每次 shader hot-reload 成功后，如果用户之前按过 F9，自动更新 output/ 中的文件。
可通过 `--auto-reflect` CLI flag 启用"每次 reload 自动输出 reflection"。

### 5.12 Ray Tracing Mode (F13)

**前置条件**：GPU 支持 `VK_KHR_ray_tracing_pipeline` + `VK_KHR_acceleration_structure`。
启动时检测，不支持则打印提示并 fallback 到 fragment mode。

**基础设施**：
- `AccelStructBuilder`：从 `MeshData`（GeometryProvider 输出）构建 BLAS + TLAS
- Storage image（RGBA8）作为 ray tracing 输出，每帧 blit 到 swapchain
- SBT（Shader Binding Table）管理：raygen / miss / hit group

**CLI**：
```bash
slang_playground --mode raytrace \
    --raygen my_raygen.slang \
    --miss my_miss.slang \
    --closesthit my_hit.slang \
    --geometry sphere          # BLAS 来源
```

**默认 shader** (`default_raygen.slang`)：
```slang
import playground;

[[vk::push_constant]] Globals globals;
RWTexture2D<float4> gOutput;
RaytracingAccelerationStructure gScene;

[shader("raygeneration")]
void raygenMain() {
    uint2 launchIdx = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;
    float2 uv = (float2(launchIdx) + 0.5) / float2(launchDim);

    RayDesc ray;
    ray.Origin    = float3(0, 0, -3);
    ray.Direction = normalize(float3(uv * 2.0 - 1.0, 1.0));
    ray.TMin      = 0.001;
    ray.TMax      = 1000.0;

    RayPayload payload = {};
    TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    gOutput[launchIdx] = float4(payload.color, 1.0);
}
```

### 5.12 Mesh Shader Mode

**前置条件**：GPU 支持 `VK_EXT_mesh_shader`。不支持则 fallback。

**Pipeline 结构**：Task (optional) → Mesh → Fragment

**CLI**：
```bash
slang_playground --mode meshshader \
    --task my_task.slang \
    --meshshader my_mesh.slang \
    --frag my_frag.slang
```

**默认 shader** (`default_mesh.slang`)：
```slang
import playground;

struct MeshVertex {
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

static const uint kMaxVertices   = 64;
static const uint kMaxPrimitives = 126;

[outputtopology("triangle")]
[numthreads(32, 1, 1)]
[shader("mesh")]
void meshMain(
    uint gtid : SV_GroupThreadID,
    uint gid  : SV_GroupID,
    OutputVertices<MeshVertex, kMaxVertices> verts,
    OutputIndices<uint3, kMaxPrimitives> tris)
{
    // Procedural cube: 8 vertices, 12 triangles
    const uint numVerts = 8;
    const uint numTris  = 12;
    SetMeshOutputCounts(numVerts, numTris);

    if (gtid < numVerts) {
        // ... generate cube vertex
    }
    if (gtid < numTris) {
        // ... generate triangle indices
    }
}
```

### 5.13 Multi-Pass Mode

**JSON 配置文件驱动**，支持任意数量 pass 串联：

```json
{
    "passes": [
        {
            "name": "gbuffer",
            "vert": "gbuffer_vert.slang",
            "frag": "gbuffer_frag.slang",
            "outputs": ["albedo:RGBA8", "normal:RGBA16F", "depth:D32F"],
            "geometry": "obj",
            "obj": "bunny.obj"
        },
        {
            "name": "lighting",
            "vert": null,
            "frag": "lighting_frag.slang",
            "inputs": ["albedo", "normal", "depth"],
            "outputs": ["hdr:RGBA16F"],
            "geometry": "fullscreen"
        },
        {
            "name": "post",
            "vert": null,
            "frag": "post_frag.slang",
            "inputs": ["hdr"],
            "outputs": ["swapchain"],
            "geometry": "fullscreen"
        }
    ]
}
```

**MultiPassRunner 职责**：
- 解析 JSON → 创建 N 个 `VkRenderPass` + `VkFramebuffer` + 中间纹理
- 每 pass 独立的 pipeline，独立 hot-reload
- 前一 pass 的 output 作为下一 pass 的 input（`VkDescriptorSet` 绑定为 sampled texture）
- JSON 文件本身也被 FileWatcher 监控，变化时完全重建 pass chain

### 5.14 Shader Stdlib (`shaders/playground/include/`)

**设计理念**：对标 Slang 官方 playground 的内置辅助 + Shadertoy 的常用函数，
让用户只需 `import playground;` 就能获得所有常用工具函数。

**自动 search path**：playground 启动时自动将 `shaders/playground/include/` 加入
`SlangCompiler::AddSearchPath()`，用户 shader 直接 `import` 即可。

#### 模块清单

| Module | Key Functions | Notes |
|--------|--------------|-------|
| **`playground.slang`** | umbrella import | `import playground;` 一行导入所有 |
| **`math_utils.slang`** | `PI`, `TAU`, `saturate`, `remapRange`, `smootherstep`, `mod289`, `permute`, `rotateZ/Y/X` | 所有 Shadertoy 必备数学工具 |
| **`noise.slang`** | `hash11/21/31`, `valueNoise2D/3D`, `perlinNoise2D/3D`, `simplexNoise2D/3D`, `fbm`, `worleyNoise` | GPU-friendly 纯函数噪声 |
| **`sdf.slang`** | `sdSphere`, `sdBox`, `sdTorus`, `sdCylinder`, `sdCone`, `sdCapsule`, `opUnion`, `opSmoothUnion`, `opSubtract`, `opIntersect`, `opRound`, `opRepeat` | 完整 SDF 原语 + 布尔运算 |
| **`color.slang`** | `hsvToRgb`, `rgbToHsv`, `linearToSrgb`, `srgbToLinear`, `palette` (cosine palette), `luminance` | 颜色空间转换 |
| **`sampling.slang`** | `hammersley`, `sampleHemisphereCosine`, `sampleHemisphereUniform`, `sampleSphereUniform`, `sampleDisk`, `importanceSampleGGX` | 蒙特卡罗采样 |
| **`pbr.slang`** | `distributionGGX`, `geometrySmith`, `fresnelSchlick`, `fresnelSchlickRoughness`, `evaluateBrdf` | Cook-Torrance BRDF |
| **`raymarching.slang`** | `rayMarch`, `calcNormal`, `softShadow`, `ambientOcclusion`, `cheapAO` | SDF 场景 raymarching 全套 |
| **`tonemapping.slang`** | `reinhardTonemap`, `reinhardExtended`, `ACESFilmic`, `uncharted2Tonemap`, `gammaCorrect` | HDR → LDR |

#### `playground.slang` (umbrella)

```slang
// shaders/playground/include/playground.slang
// Master umbrella — one import to rule them all.

// Re-export all stdlib modules
public import playground.math_utils;
public import playground.noise;
public import playground.sdf;
public import playground.color;
public import playground.sampling;
public import playground.pbr;
public import playground.raymarching;
public import playground.tonemapping;

// Common globals struct (matches C++ push constants)
public struct Globals {
    float    iTime;
    float    iTimeDelta;
    float    iFrame;
    float    _pad0;
    float2   iResolution;
    float2   iMouse;
};

// Ray tracing payload (for raytrace mode)
public struct RayPayload {
    float3 color;
    float  hitT;
};
```

#### 用户使用示例

```slang
// my_cool_effect.slang
import playground;   // 一行获得所有工具

[[vk::push_constant]] Globals globals;

struct FSInput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

[shader("fragment")]
float4 fragmentMain(FSInput input) : SV_Target0 {
    float2 uv = input.uv;
    float2 p  = uv * 2.0 - 1.0;
    p.x *= globals.iResolution.x / globals.iResolution.y;

    // SDF scene using stdlib
    float d = sdSphere(float3(p, 0), 0.5);
    d = opSmoothUnion(d, sdBox(float3(p - float2(0.5, 0), 0), float3(0.3)), 0.1);

    // Noise + color
    float n = fbm(float3(p * 3.0, globals.iTime * 0.5));
    float3 col = palette(d + n, float3(0.5), float3(0.5), float3(1.0), float3(0.0, 0.33, 0.67));

    // Tonemapping
    col = ACESFilmic(col);
    col = gammaCorrect(col, 2.2);

    return float4(col, 1.0);
}
```

### 5.16 Attribute-Driven Resource System (F18)

**对标官方 slang-playground 的核心设计范式**：shader 通过 `[playground::XXX]` 属性自描述所需的 GPU 资源，运行时通过 Slang reflection (`userAttribs`) 解析属性并自动分配。

#### 设计流程

```
1. 编译 shader → 获取 ReflectionJSON
2. 遍历 parameters[].userAttribs[]，匹配 playground_* 前缀
3. 根据属性类型分配 VkBuffer / VkImage / VkSampler
4. 为 uniform 参数创建 UBO (替代 push constants，当 shader 使用属性系统时)
5. 创建 VkDescriptorSet 绑定所有资源
```

#### 属性 → Vulkan 资源映射

| Attribute | 参数类型约束 | 分配的 GPU 资源 |
|-----------|-------------|----------------|
| `ZEROS(n)` | `RWStructuredBuffer<T>` | `VkBuffer` (STORAGE, n * sizeof(T) bytes, zero-filled) |
| `BLACK(w,h)` | `RWTexture2D<T>` / `Texture2D<T>` | `VkImage` 2D (STORAGE + SAMPLED, zero-filled) |
| `BLACK_3D(w,h,d)` | `RWTexture3D<T>` / `Texture3D<T>` | `VkImage` 3D (STORAGE + SAMPLED, zero-filled) |
| `BLACK_SCREEN(sx,sy)` | `RWTexture2D<T>` | `VkImage` 2D (size = window * scale, resize-aware) |
| `URL("path")` | `Texture2D<T>` | `VkImage` 2D (loaded from file via stb_image) |
| `SAMPLER` | `SamplerState` | `VkSampler` (linear filter, repeat wrap) |
| `RAND(n)` | `RWStructuredBuffer<float>` | `VkBuffer` (STORAGE, n * 4 bytes, random float fill via PRNG compute) |
| `DATA("path")` | `RWStructuredBuffer<T>` | `VkBuffer` (STORAGE, loaded from binary file) |
| `TIME` | uniform scalar | UBO field: `float time_ms` (auto-updated each frame) |
| `FRAME_ID` | uniform scalar | UBO field: `uint frame_id` (auto-incremented) |
| `MOUSE_POSITION` | uniform `float4` | UBO field: `float4 mouse` (xy=current, zw=click, sign=button state) |
| `KEY("KeyA")` | uniform scalar | UBO field: `float key` (1.0 if pressed, 0.0 otherwise) |
| `SLIDER(def,min,max)` | uniform `float` | UBO field + ImGui slider (N1) |
| `COLOR_PICK(r,g,b)` | uniform `float3` | UBO field + ImGui color picker (N1) |

#### 实现方案

```cpp
// ResourceAllocator.h
class ResourceAllocator {
public:
    struct AllocatedResources {
        std::unordered_map<std::string, VkBuffer>  buffers;
        std::unordered_map<std::string, VkImage>   images;
        std::unordered_map<std::string, VkSampler> samplers;
        VkBuffer                                    uniformBuffer;  // for TIME/FRAME_ID/MOUSE/KEY/SLIDER/COLOR_PICK
        uint32_t                                    uniformSize = 0;
        VkDescriptorSet                             descriptorSet;
    };

    /** @brief Parse reflection attributes and allocate all GPU resources.
     *  @param iReflection  Shader reflection from SlangCompiler::Reflect()
     *  @param iDevice      Vulkan device
     *  @param iAllocator   VMA allocator
     *  @return Allocated resources or error.
     */
    [[nodiscard]] static auto Allocate(
        ShaderReflection const& iReflection,
        VkDevice iDevice,
        VmaAllocator iAllocator)
        -> core::Result<AllocatedResources>;

    /** @brief Update per-frame uniform data (time, frame, mouse, keys). */
    static auto UpdateUniforms(
        AllocatedResources& ioResources,
        float iTimeMs,
        uint32_t iFrame,
        float const iMouse[4],
        std::unordered_set<std::string> const& iPressedKeys)
        -> void;
};
```

#### 与 push constants 的关系

当 shader 使用属性系统时（检测到 `playground_*` userAttribs），uniform 数据通过 **UBO** 传递（而非 push constants）。这是因为：
- 属性系统的 uniform layout 是动态的（由 shader 声明决定）
- Push constants 要求编译时已知 layout
- UBO 支持任意大小（push constants 限制 128-256 bytes）

传统 mode（fragment/mesh/compute）仍使用 push constants 的硬编码 `Globals` struct。两种模式互斥。

### 5.17 Multi-Kernel Compute (F19)

**对标官方 slang-playground 的多 entry point 执行模型**。

同一 shader 文件可包含多个 `[shader("compute")]` entry point，每个 entry point 通过 `[playground::CALL*]` 属性声明 dispatch 方式。运行时按声明顺序依次执行。

#### Dispatch 属性

| Attribute | 含义 |
|-----------|------|
| `[playground::CALL(x,y,z)]` | 固定 thread grid 大小，自动按 `numthreads` 计算 workgroup 数 |
| `[playground::CALL_SIZE_OF("resourceName")]` | 按资源大小决定 dispatch：buffer → `size/elementSize` threads, texture → `width*height` threads |
| `[playground::CALL_INDIRECT("bufferName", offset)]` | GPU-driven indirect dispatch |
| `[playground::CALL_ONCE]` | 仅首帧执行（可与上面任一 CALL 组合使用） |

#### 执行流程

```
1. 解析 reflection.entryPoints[].userAttribs[] 中的 playground_CALL* 属性
2. 按 entry point 出现顺序创建 VkComputePipeline 数组
3. 每帧循环:
   a. 更新 uniform buffer
   b. 对每个 entry point:
      - 如果 CALL_ONCE && !firstFrame → skip
      - beginComputePass
      - bindPipeline + bindDescriptorSets
      - 根据 CALL 类型计算 workgroup count
      - dispatch / dispatchIndirect
      - endComputePass
   c. 如果有 outputTexture → blit 到 swapchain
   d. 如果有 g_printedBuffer → readback + parse printf
```

### 5.18 GPU Printf (F20)

**对标官方 slang-playground 的 `import printing;` 模块**。

#### 原理

Shader 端通过 `print(format, args...)` 将格式化数据写入 `RWStructuredBuffer<FormattedStruct>`，每个元素 12 bytes：

```slang
struct FormattedStruct {
    uint32_t type;  // 1=format string, 2=normal string, 3=integer, 4=float, 0xFFFFFFFF=terminator
    uint32_t low;   // hashed string index, or reinterpret_cast<uint32_t>(float), or integer value
    uint32_t high;  // reserved for 64-bit (currently unused)
};
```

CPU 端每帧 readback buffer，通过 hashed string table 还原格式化字符串，解析 printf 格式符（`%d`, `%f`, `%s`, `%x`, etc.）。

#### 实现

```cpp
// PrintfParser.h
class PrintfParser {
public:
    /** @brief Parse GPU printf buffer and return formatted output strings.
     *  @param iBuffer      Mapped pointer to FormattedStruct array
     *  @param iBufferSize  Buffer size in bytes
     *  @param iHashedStrings  Hash → string lookup table (from Slang compilation)
     *  @return Vector of formatted output strings
     */
    [[nodiscard]] static auto Parse(
        void const* iBuffer,
        size_t iBufferSize,
        std::unordered_map<uint32_t, std::string> const& iHashedStrings)
        -> std::vector<std::string>;
};
```

Hashed strings 来自编译结果 `ReflectionJSON::hashedStrings`（Slang 将所有字符串字面量 hash 后嵌入 shader binary，编译器同时输出 hash→string 映射）。

### 5.19 Frame Step Controls (F21)

```cpp
// FrameController.h
class FrameController {
public:
    auto Update() -> bool;       // returns true if should render this frame
    auto Pause() -> void;
    auto Resume() -> void;
    auto TogglePause() -> void;
    auto StepForward() -> void;
    auto StepBackward() -> void;  // resets to frame N-1, re-executes
    auto Reset() -> void;         // back to frame 0

    [[nodiscard]] auto IsPaused() const -> bool;
    [[nodiscard]] auto FrameIndex() const -> uint32_t;
    [[nodiscard]] auto FrameTimeMs() const -> float;
    [[nodiscard]] auto FPS() const -> float;

private:
    uint32_t frameIndex_ = 0;
    bool     paused_     = false;
    bool     stepOnce_   = false;
    // FPS tracking
    float    timeAggregate_ = 0.f;
    uint32_t frameCount_    = 0;
    float    fps_           = 0.f;
    float    frameTimeMs_   = 0.f;
};
```

**快捷键**：Space=暂停切换, →/.]]=单帧前进, ←/[=单帧后退, Home=重置到第 0 帧

### 5.20 Entry Point Discovery (F22)

编译 shader 后，从 `ReflectionJSON::entryPoints[]` 提取所有 entry point 名称和 stage。如果有多个 entry point 且用户未指定 `--entrypoint`，对 compute mode 使用全部（multi-kernel），对 graphics mode 使用第一个 vertex + 第一个 fragment。

---

## 6. CLI Interface

```
slang_playground [options]

Options:
  # --- General ---
  --mode <mode>           "mesh" (default) | "fragment" | "compute" | "raytrace" | "meshshader" | "multipass"
  --backend <api>         "vulkan" (default) | "d3d12" (future)
  --width <N>             Window width (default: 1280)
  --height <N>            Window height (default: 720)
  --poll-ms <N>           File poll interval in ms (default: 200)
  --no-validation         Disable Vulkan validation layers
  --capture-path <dir>    Directory for RenderDoc/Nsight captures (default: ./captures/)
  --include <dir>         Extra Slang search path (repeatable; stdlib is always included)
  --auto-reflect          Auto-output target code + reflection on every hot-reload
  --entrypoint <name>     Select specific entry point (default: auto-discover all)
  --demo <name>           Load built-in demo shader by name (e.g. circle, autodiff, atomics)
  --version               Show miki playground version + Slang compiler version and exit

  # --- Fragment mode ---
  --shader <path>         Fragment .slang file (also used for compute mode)

  # --- Mesh mode (vertex + fragment) ---
  --vert <path>           Vertex shader .slang file
  --frag <path>           Fragment shader .slang file
  --geometry <type>       "quad" | "cube" | "sphere" | "plane" (default: cube)
  --obj <path>            Load .obj mesh file (overrides --geometry)

  # --- Compute mode ---
  --dispatch <X,Y,Z>      Dispatch dimensions (default: 256,1,1)

  # --- Ray tracing mode ---
  --raygen <path>         Ray generation shader .slang
  --miss <path>           Miss shader .slang
  --closesthit <path>     Closest-hit shader .slang
  --anyhit <path>         Any-hit shader .slang (optional)
  --intersection <path>   Intersection shader .slang (optional, for procedural geo)

  # --- Mesh shader mode ---
  --task <path>           Task/amplification shader .slang (optional)
  --meshshader <path>     Mesh shader .slang
  # --frag reused from mesh mode

  # --- Multi-pass mode ---
  --passes <path.json>    Multi-pass configuration JSON file
```

**Hotkeys**:

| Key | Action |
|-----|--------|
| F9 | Target Code Reflection: 输出编译产物 + reflection 到控制台和文件 |
| F10 | RenderDoc: capture current frame → save .rdc |
| F11 | Nsight: trigger frame capture |
| F10 (from RenderDoc UI) | Opens capture in RenderDoc UI automatically |
| Space | 暂停/继续渲染 |
| Right Arrow / ] | 单帧前进 |
| Left Arrow / [ | 单帧后退 |
| Home | 重置到第 0 帧 |
| F12 / Double-click | 全屏切换 (N10) |

**Mode shorthand examples**:

```bash
# Shadertoy-style: edit fragment shader, fullscreen quad
slang_playground --shader my_effect.slang

# Mesh mode with built-in cube
slang_playground --mode mesh --vert my_vert.slang --frag my_frag.slang --geometry cube

# Mesh mode with OBJ file
slang_playground --mode mesh --vert my_vert.slang --frag my_frag.slang --obj bunny.obj

# Compute
slang_playground --mode compute --shader my_compute.slang --dispatch 256,1,1

# Ray tracing with built-in sphere
slang_playground --mode raytrace --raygen my_rgen.slang --miss my_miss.slang --closesthit my_hit.slang

# Mesh shader
slang_playground --mode meshshader --meshshader my_mesh.slang --frag my_frag.slang

# Multi-pass deferred rendering
slang_playground --mode multipass --passes my_deferred.json
```

---

## 7. Dependencies

| Dependency | Source | Status in miki |
|------------|--------|----------------|
| GLFW 3.4+ | submodule | Already present |
| Vulkan SDK | system | Already required |
| Slang SDK | submodule/system | Already present via miki::shader |
| miki::shader | internal | SlangCompiler, ShaderTypes |
| tinyobjloader | single-header | **New** — OBJ mesh loading (~1200 LOC header-only) |
| renderdoc_app.h | vendored header | **New** — RenderDoc in-app API (~500 LOC, MIT, zero link cost) |
| Nsight Graphics SDK | system (optional) | Runtime-only — `NGFX_Injection.dll` loaded dynamically |
| glm (or handrolled) | header-only | **New** — MVP matrix for mesh mode (可用 `<cmath>` 手写避免依赖) |
| vk-bootstrap (optional) | single-header | **New** — simplifies Vulkan init |
| stb_image | single-header | **New** — `[playground::URL]` texture loading from file/URL (~7600 LOC header-only) |
| VMA (VulkanMemoryAllocator) | single-header | **New** — ResourceAllocator (F18) 需要高效的 buffer/image 分配 |

`vk-bootstrap` 是可选的。如果想零新依赖，手写 Vulkan init 也可接受（~250 行）。
`tinyobjloader` 是必需的（OBJ 功能核心依赖），single-header 引入零构建成本。
`glm` 可选——mesh mode 需要 MVP 矩阵，可以用 ~50 行手写 mat4 替代。
`renderdoc_app.h` 直接 vendor（MIT license），编译期零成本，运行时动态检测。
Nsight SDK 不需要编译期链接——纯运行时 `LoadLibrary` + `GetProcAddress`。
`stb_image` 用于 `[playground::URL("path")]` 属性从文件加载纹理，single-header 零构建成本。
`VMA` 是 Vulkan 最佳实践的内存分配器，F18 属性资源系统需要动态分配大量 buffer/image。

---

## 8. Implementation Plan

### 8.1 Phased Implementation

实现分三期：**Tier 1（核心）** 先跑起来，**Tier 2（高级模式）** 在核心稳定后追加，**Tier 3（Demo & Polish）** 最后完善。

#### Tier 1 — Core Playground (~23.5h)

| Step | Content | Effort |
|------|---------|--------|
| S1 | `FileWatcher` — polling `last_write_time`，多文件监控 | 0.5h |
| S2 | `VulkanContext` — instance, device, swapchain, renderpass, framebuffers, VB upload, VMA init | 3h |
| S3 | `GeometryProvider` — quad/cube/sphere/plane procedural generation (默认 cube) | 1h |
| S4 | `GeometryProvider::LoadObj` — tinyobjloader 集成 | 1h |
| S5 | `PipelineManager` — graphics/compute pipeline, vertex input, hot-swap | 1.5h |
| S6 | `main.cpp` — GLFW window, main loop, push constants, CLI, orbit camera, keyboard input | 2h |
| S7 | `TargetCodeReflector` — multi-target compile (6 targets incl. Metal/CUDA) + reflection JSON + spirv-dis + F9 | 2h |
| S8 | Shader stdlib — `playground.slang` (attributes) + `printing.slang` + `rendering.slang` + `rand_float.slang` + 8 utility modules | 3h |
| S9 | Shader templates — fragment/mesh/compute defaults (uses stdlib) | 0.5h |
| S10 | `GpuDebugger` — RenderDoc in-app API + F10 | 1h |
| S11 | `GpuDebugger` — Nsight Graphics SDK runtime + F11 | 1h |
| S12 | `ResourceAllocator` — attribute-driven GPU resource allocation (F18): parse playground::XXX → VkBuffer/VkImage/VkSampler/UBO | 3h |
| S13 | `PlaygroundCompiler` — entry point discovery + multi-kernel orchestration + CALL attribute parsing (F19, F22) | 2h |
| S14 | `PrintfParser` — GPU printf readback + hashed string resolution (F20) | 1h |
| S15 | `FrameController` — pause/resume/step/reset + FPS tracking (F21) | 0.5h |
| S16 | Error overlay (console-only) | 0.5h |
| S17 | CMakeLists.txt + integration + vendored headers (stb_image, VMA, renderdoc_app.h, tinyobjloader) | 0.5h |
| **Tier 1 Total** | | **~23.5h** |

#### Tier 2 — Advanced Modes (~8h)

| Step | Content | Effort | Vulkan Extension |
|------|---------|--------|------------------|
| S18 | `AccelStructBuilder` — BLAS/TLAS from MeshData | 2h | `VK_KHR_acceleration_structure` |
| S19 | RT pipeline — SBT, storage image blit, raygen/miss/hit hot-reload | 2.5h | `VK_KHR_ray_tracing_pipeline` |
| S20 | Mesh shader pipeline — task + mesh + fragment, no VB | 1.5h | `VK_EXT_mesh_shader` |
| S21 | `MultiPassRunner` — JSON parser, intermediate textures, descriptor binding | 2h | core |
| **Tier 2 Total** | | **~8h** |

#### Tier 3 — Demo & Polish (~3h)

| Step | Content | Effort |
|------|---------|--------|
| S22 | Demo shader library — 15+ demos covering all features (N9) | 2h |
| S23 | Fullscreen toggle (N10) + compiler version display (N11) | 0.5h |
| S24 | End-to-end integration test + documentation | 0.5h |
| **Tier 3 Total** | | **~3h** |

| | **Grand Total** | **~34.5h** |
|---|---|---|

### 8.2 Implementation Priority

```
Tier 1 (must-have):
  S1 → S2 → S3/S4 → S5 → S6 → S7 → S8/S9 → S10/S11 → S12 → S13 → S14 → S15 → S16 → S17
  Parallelizable pairs: S3/S4, S8/S9, S10/S11, S14/S15

Tier 2 (after Tier 1 stable):
  S18 → S19 (ray tracing chain)
  S20       (mesh shader, independent)
  S21       (multipass, independent)

Tier 3 (after Tier 2):
  S22 → S23 → S24
```

---

## 9. Usage Workflow

```bash
# 1. Build
cmake --build build --target slang_playground

# 2. Shadertoy-style (default): edit fragment shader with hot-reload
./slang_playground
# → edit shaders/playground/default_frag.slang → save → auto-recompiles in <200ms

# 3. Custom fragment shader with stdlib
./slang_playground --shader my_sdf_art.slang
# my_sdf_art.slang: import playground; → gets all SDF/noise/color tools

# 4. Mesh mode with OBJ + custom VS/FS
./slang_playground --mode mesh --vert pbr_vert.slang --frag pbr_frag.slang --obj bunny.obj

# 5. Compute mode
./slang_playground --mode compute --shader my_compute.slang --dispatch 256,1,1

# 6. Ray tracing mode
./slang_playground --mode raytrace --raygen my_rgen.slang --miss sky.slang --closesthit material.slang

# 7. Mesh shader mode
./slang_playground --mode meshshader --meshshader procedural.slang --frag color.slang

# 8. Multi-pass deferred rendering
./slang_playground --mode multipass --passes deferred.json

# 9. Attribute-driven compute (shader self-describes resources)
./slang_playground --mode compute --shader my_multi_kernel.slang
# my_multi_kernel.slang uses [playground::ZEROS], [playground::CALL_SIZE_OF], etc.
# Resources auto-allocated from reflection; multi-kernel dispatched in declaration order

# 10. GPU printf debugging
./slang_playground --mode compute --shader debug_print.slang
# debug_print.slang: import printing; printf("value = %f\n", myVar);
# Output appears in console after each frame

# 11. Load built-in demo
./slang_playground --demo circle
# Loads shaders/playground/demos/circle.slang with all dependencies

# 12. RenderDoc capture (launch from RenderDoc UI, then press F10 in window)
# 13. Nsight capture (press F11, requires Nsight Graphics installed)
# 14. Frame controls: Space=pause, Right=step forward, Home=reset
```

---

## 10. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Vulkan swapchain recreation on resize | Medium | 先不支持 resize；或 WaitIdle + recreate |
| Slang compile latency on complex shaders | Low | 后台线程编译（post-MVP） |
| File watcher race (editor atomic save) | Low | 延迟 50ms 再读取；retry on read failure |
| D3D12 support | Low | MVP 只做 Vulkan；D3D12 作为 future work |
| RenderDoc not attached | None | 静默降级；按 F10 打印提示 |
| Nsight not installed | None | 静默降级；按 F11 打印提示 |
| RenderDoc + Nsight conflict | Low | 设计上互斥，F10/F11 分别绑定 |
| OBJ file too large | Low | warn > 1M verts，或分 chunk upload |
| Push constant 96B (mesh mode) | Low | 在 Vulkan 保证最小 128B 范围内 |
| Vertex layout mismatch | Medium | 文档约定 POSITION0/NORMAL0/TEXCOORD0；reflection 校验 |
| GPU 不支持 RT extensions | Low | 启动时检测；fallback 到 fragment mode + 打印提示 |
| GPU 不支持 mesh shader ext | Low | 启动时检测；fallback + 打印提示 |
| Slang import resolution 失败 | Low | 自动注入 `shaders/playground/include/` search path；错误信息显示完整路径 |
| Multipass JSON 格式错误 | Low | JSON 解析失败 → 打印详细错误 + 保持旧 pass chain |
| Stdlib module 命名冲突 | Low | 所有 stdlib 在 `playground.*` namespace 下；用户 import 显式 |
| Attribute 解析与 Slang 版本耦合 | Medium | `userAttribs` 是 Slang reflection API 的一部分，需跟踪 Slang 版本更新；抽象层隔离 |
| Printf buffer overflow | Low | 固定大小 buffer（默认 64KB），超出时截断并提示 "Print buffer out of boundary" |
| RAND buffer 初始化延迟 | Low | PRNG compute shader 在首帧前 dispatch，通常 <1ms |
| URL/DATA 文件加载失败 | Low | 回退到零填充资源 + 控制台警告 |
| Multi-kernel dispatch 顺序依赖 | Medium | 按声明顺序执行，每个 kernel 之间插入 pipeline barrier |
| Metal/CUDA target 不可执行 | Low | 仅用于 F17 反射输出，不实际执行；编译失败时显示错误信息 |

---

## 11. Relationship to miki Roadmap

这是一个**开发工具 demo**，不属于任何 Phase。它的价值：
- 快速验证 Slang 语言特性和语法（vertex/fragment/compute/RT/mesh/multipass）
- 验证 `miki::shader::SlangCompiler` 的运行时编译能力
- Shader stdlib 可后续提取到 `shaders/common/` 成为 miki 引擎公共资产
- 后续 Phase 2+ 实现真正 pipeline 后，可切换为 miki RHI 版本
- RenderDoc/Nsight 集成模式可复用到所有 miki demo
- 作为 shader 开发的日常工具持续使用
