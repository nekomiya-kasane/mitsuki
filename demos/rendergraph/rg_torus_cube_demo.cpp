/** @file rg_torus_cube_demo.cpp
 *  @brief RenderGraph demo — torus + cube rendered via Builder → Compiler → Executor.
 *
 *  Usage: rg_torus_cube_demo [--backend vulkan|vulkan11|d3d12|opengl|webgpu]
 *  Default: VulkanCompat (desktop), WebGPU (Emscripten).
 *
 *  Demonstrates the full RenderGraph pipeline:
 *    1. RenderGraphBuilder: declare passes, resources, and dependencies
 *    2. RenderGraphCompiler: compile to optimized execution plan
 *    3. RenderGraphExecutor: allocate transients, record, submit
 *
 *  Scene:
 *    - Pass "DrawTorus": renders a torus to transient color RT
 *    - Pass "DrawCube":  renders a cube  to the SAME transient color RT (load=Load)
 *    - Pass "Present":   reads the color RT, blits to swapchain backbuffer
 *    - RenderGraph automatically handles barriers, lifetimes, and batching.
 *
 *  Camera: arcball (mouse drag rotate, scroll zoom). ESC to quit.
 */

#include "../rhi/common/DemoCommon.h"
#include "../rhi/common/DemoMeshGen.h"

#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphCompiler.h"
#include "miki/rendergraph/RenderGraphExecutor.h"
#include "miki/rendergraph/RenderPassContext.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <numbers>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char kBlinnPhongSlangSource[] = {
#embed "../rhi/shaders/blinn_phong.slang"
    , '\0'
};
#pragma clang diagnostic pop

using namespace miki::demo;
using namespace miki::platform;
using namespace miki::rhi;
using namespace miki::core;
using namespace miki::rg;

// ============================================================================
// Push constant layout (matches blinn_phong.slang)
// ============================================================================

struct PushConst {
    float4x4 mvp;
    float4x4 model;
    float diffuseR, diffuseG, diffuseB;
    float _pad0;
};
static_assert(sizeof(PushConst) == 144);

// ============================================================================
// Mesh + Pipeline resources (created once, used every frame)
// ============================================================================

struct MeshGpuData {
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    uint32_t indexCount = 0;
};

struct SceneResources {
    DeviceHandle device;
    ShaderModuleHandle vertModule, fragModule;
    PipelineLayoutHandle pipelineLayout;
    PipelineHandle pipeline;
    MeshGpuData torus;
    MeshGpuData cube;
    SurfaceManager* sm = nullptr;
    WindowHandle window{};

    // Camera
    float rotX = 0.3f, rotY = 0.0f;
    float distance = 5.0f;
    bool dragging = false;

    auto UploadMesh(const DemoMeshData& mesh, const char* label) -> std::optional<MeshGpuData> {
        MeshGpuData gpu;
        gpu.indexCount = static_cast<uint32_t>(mesh.indices.size());
        uint64_t vbSize = mesh.vertices.size() * sizeof(DemoVertex);
        uint64_t ibSize = mesh.indices.size() * sizeof(uint32_t);

        auto createBuf = [&](const BufferDesc& bd) -> RhiResult<BufferHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<BufferHandle> { return d.CreateBuffer(bd); });
        };
        auto vb = createBuf(
            {.size = vbSize, .usage = BufferUsage::Vertex, .memory = MemoryLocation::CpuToGpu, .debugName = label}
        );
        if (!vb) {
            return std::nullopt;
        }
        gpu.vertexBuffer = *vb;

        auto ib = createBuf(
            {.size = ibSize, .usage = BufferUsage::Index, .memory = MemoryLocation::CpuToGpu, .debugName = label}
        );
        if (!ib) {
            return std::nullopt;
        }
        gpu.indexBuffer = *ib;

        auto mapCopy = [&](BufferHandle h, const void* data, uint64_t sz) -> bool {
            auto ptr = device.Dispatch([&](auto& d) -> RhiResult<void*> { return d.MapBuffer(h); });
            if (!ptr) {
                return false;
            }
            std::memcpy(*ptr, data, sz);
            (void)device.Dispatch([&](auto& d) {
                d.UnmapBuffer(h);
                return 0;
            });
            return true;
        };
        if (!mapCopy(gpu.vertexBuffer, mesh.vertices.data(), vbSize)) {
            return std::nullopt;
        }
        if (!mapCopy(gpu.indexBuffer, mesh.indices.data(), ibSize)) {
            return std::nullopt;
        }
        return gpu;
    }

    auto Init(DeviceHandle dev, CompiledShaderPair& shaders, SurfaceManager* surfMgr, WindowHandle win) -> bool {
        device = dev;
        sm = surfMgr;
        window = win;

        auto createShader = [&](const ShaderModuleDesc& sd) -> RhiResult<ShaderModuleHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<ShaderModuleHandle> { return d.CreateShaderModule(sd); });
        };

        ShaderModuleDesc vd{
            .stage = ShaderStage::Vertex, .code = shaders.vs.data, .entryPoint = shaders.vs.entryPoint.c_str()
        };
        auto vs = createShader(vd);
        if (!vs) {
            std::println("[rg_demo] VS create failed");
            return false;
        }
        vertModule = *vs;

        ShaderModuleDesc fd{
            .stage = ShaderStage::Fragment, .code = shaders.fs.data, .entryPoint = shaders.fs.entryPoint.c_str()
        };
        auto fs = createShader(fd);
        if (!fs) {
            std::println("[rg_demo] FS create failed");
            return false;
        }
        fragModule = *fs;

        PushConstantRange pcRange{
            .stages = ShaderStage::Vertex | ShaderStage::Fragment, .offset = 0, .size = sizeof(PushConst)
        };
        PipelineLayoutDesc pld{.pushConstants = {&pcRange, 1}};
        auto pl
            = device.Dispatch([&](auto& d) -> RhiResult<PipelineLayoutHandle> { return d.CreatePipelineLayout(pld); });
        if (!pl) {
            std::println("[rg_demo] PipelineLayout failed");
            return false;
        }
        pipelineLayout = *pl;

        VertexInputBinding binding{.binding = 0, .stride = sizeof(DemoVertex), .inputRate = VertexInputRate::PerVertex};
        std::array<VertexInputAttribute, 2> attrs = {{
            {.location = 0, .binding = 0, .format = Format::RGB32_FLOAT, .offset = offsetof(DemoVertex, px)},
            {.location = 1, .binding = 0, .format = Format::RGB32_FLOAT, .offset = offsetof(DemoVertex, nx)},
        }};
        VertexInputState vis{.bindings = {&binding, 1}, .attributes = {attrs.data(), attrs.size()}};

        ColorAttachmentBlend blend{};
        auto* rs = sm->GetRenderSurface(window);
        Format colorFmt = rs ? rs->GetFormat() : Format::BGRA8_SRGB;
        GraphicsPipelineDesc gpd{};
        gpd.vertexShader = vertModule;
        gpd.fragmentShader = fragModule;
        gpd.vertexInput = vis;
        gpd.topology = PrimitiveTopology::TriangleList;
        gpd.cullMode = CullMode::Back;
        gpd.frontFace = FrontFace::CounterClockwise;
        gpd.depthTestEnable = false;
        gpd.depthWriteEnable = false;
        gpd.colorBlends = {&blend, 1};
        gpd.colorFormats = {&colorFmt, 1};
        gpd.pipelineLayout = pipelineLayout;
        auto pso = device.Dispatch([&](auto& d) -> RhiResult<PipelineHandle> { return d.CreateGraphicsPipeline(gpd); });
        if (!pso) {
            std::println("[rg_demo] PSO failed");
            return false;
        }
        pipeline = *pso;

        auto torusMesh = GenerateTorus(0.7f, 0.3f, 48, 32);
        auto torusGpu = UploadMesh(torusMesh, "TorusVB");
        if (!torusGpu) {
            std::println("[rg_demo] Torus mesh upload failed");
            return false;
        }
        torus = *torusGpu;

        auto cubeMesh = GenerateCube(0.5f);
        auto cubeGpu = UploadMesh(cubeMesh, "CubeVB");
        if (!cubeGpu) {
            std::println("[rg_demo] Cube mesh upload failed");
            return false;
        }
        cube = *cubeGpu;

        return true;
    }

    void Cleanup() {
        (void)device.Dispatch([&](auto& d) {
            d.WaitIdle();
            if (pipeline.IsValid()) {
                d.DestroyPipeline(pipeline);
            }
            if (pipelineLayout.IsValid()) {
                d.DestroyPipelineLayout(pipelineLayout);
            }
            if (fragModule.IsValid()) {
                d.DestroyShaderModule(fragModule);
            }
            if (vertModule.IsValid()) {
                d.DestroyShaderModule(vertModule);
            }
            if (torus.vertexBuffer.IsValid()) {
                d.DestroyBuffer(torus.vertexBuffer);
            }
            if (torus.indexBuffer.IsValid()) {
                d.DestroyBuffer(torus.indexBuffer);
            }
            if (cube.vertexBuffer.IsValid()) {
                d.DestroyBuffer(cube.vertexBuffer);
            }
            if (cube.indexBuffer.IsValid()) {
                d.DestroyBuffer(cube.indexBuffer);
            }
            return 0;
        });
    }
};

// ============================================================================
// BuildPushConst — compute MVP from camera state
// ============================================================================

static auto BuildPushConst(
    float rotX, float rotY, float dist, float aspect, float4x4 localTransform, float r, float g, float b
) -> PushConst {
    auto view = MakeLookAt({.x = 0, .y = 0, .z = dist}, {.x = 0, .y = 0, .z = 0}, {.x = 0, .y = 1, .z = 0});
    auto proj = MakePerspective(std::numbers::pi_v<float> / 4.0f, aspect, 0.1f, 100.0f);
    auto camera = Mul4x4(MakeRotateX(rotX), MakeRotateY(rotY));
    auto model = Mul4x4(camera, localTransform);
    return {
        .mvp = Mul4x4(proj, Mul4x4(view, model)),
        .model = model,
        .diffuseR = r,
        .diffuseG = g,
        .diffuseB = b,
        ._pad0 = 0.0f,
    };
}

// ============================================================================
// BuildAndExecuteRenderGraph — per-frame graph construction + execution
// ============================================================================

static auto BuildAndExecuteRenderGraph(
    SceneResources& scene, miki::frame::FrameContext& frameCtx, miki::frame::SyncScheduler& syncSched,
    miki::frame::CommandPoolAllocator& cmdPoolAlloc
) -> bool {
    uint32_t w = frameCtx.width;
    uint32_t h = frameCtx.height;
    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;

    // Per-frame data on stack — captured by pointer in execute lambdas (safe: synchronous execution)
    struct FrameDims {
        uint32_t w, h;
    };
    FrameDims dims{w, h};

    auto torusPC = BuildPushConst(
        scene.rotX, scene.rotY, scene.distance, aspect, MakeTranslation(-1.0f, 0.0f, 0.0f), 0.85f, 0.55f, 0.20f
    );
    auto cubePC = BuildPushConst(
        scene.rotX, scene.rotY, scene.distance, aspect, MakeTranslation(1.0f, 0.0f, 0.0f), 0.25f, 0.60f, 0.85f
    );

    // ── Step 1: Build the render graph ──────────────────────────────
    RenderGraphBuilder builder;

    // Import swapchain backbuffer as external resource
    auto backbuffer = builder.ImportBackbuffer(frameCtx.swapchainImage, "Backbuffer");

    // Torus pass: clear + draw torus → writes backbuffer
    backbuffer = [&] {
        auto bb = backbuffer;
        builder.AddGraphicsPass(
            "DrawTorus",
            [&, bb](PassBuilder& pb) mutable {
                bb = pb.WriteColorAttachment(bb);
                pb.SetSideEffects();
            },
            [&scene, &torusPC, &dims](RenderPassContext& ctx) {
                ctx.DispatchCommands([&](auto& cmd) {
                    cmd.CmdBindPipeline(scene.pipeline);
                    cmd.CmdSetViewport(
                        {.x = 0,
                         .y = 0,
                         .width = static_cast<float>(dims.w),
                         .height = static_cast<float>(dims.h),
                         .minDepth = 0,
                         .maxDepth = 1}
                    );
                    cmd.CmdSetScissor({.offset = {0, 0}, .extent = {dims.w, dims.h}});
                    cmd.CmdPushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(PushConst), &torusPC);
                    cmd.CmdBindVertexBuffer(0, scene.torus.vertexBuffer, 0);
                    cmd.CmdBindIndexBuffer(scene.torus.indexBuffer, 0, IndexType::Uint32);
                    cmd.CmdDrawIndexed(scene.torus.indexCount, 1, 0, 0, 0);
                });
            }
        );
        return bb;
    }();

    // Cube pass: load existing + draw cube → writes backbuffer (additive)
    backbuffer = [&] {
        auto bb = backbuffer;
        builder.AddGraphicsPass(
            "DrawCube",
            [&, bb](PassBuilder& pb) mutable {
                pb.ReadTexture(bb);
                bb = pb.WriteColorAttachment(bb);
                pb.SetSideEffects();
            },
            [&scene, &cubePC, &dims](RenderPassContext& ctx) {
                ctx.DispatchCommands([&](auto& cmd) {
                    cmd.CmdBindPipeline(scene.pipeline);
                    cmd.CmdSetViewport(
                        {.x = 0,
                         .y = 0,
                         .width = static_cast<float>(dims.w),
                         .height = static_cast<float>(dims.h),
                         .minDepth = 0,
                         .maxDepth = 1}
                    );
                    cmd.CmdSetScissor({.offset = {0, 0}, .extent = {dims.w, dims.h}});
                    cmd.CmdPushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(PushConst), &cubePC);
                    cmd.CmdBindVertexBuffer(0, scene.cube.vertexBuffer, 0);
                    cmd.CmdBindIndexBuffer(scene.cube.indexBuffer, 0, IndexType::Uint32);
                    cmd.CmdDrawIndexed(scene.cube.indexCount, 1, 0, 0, 0);
                });
            }
        );
        return bb;
    }();

    // Present pass: transition backbuffer to present layout
    builder.AddPresentPass("Present", backbuffer);

    builder.Build();

    // ── Step 2: Compile the render graph ────────────────────────────
    RenderGraphCompiler compiler;
    auto compileResult = compiler.Compile(builder);
    if (!compileResult) {
        std::println("[rg_demo] RenderGraph compile failed");
        return false;
    }
    auto& compiled = *compileResult;

    // ── Step 3: Execute the render graph ────────────────────────────
    RenderGraphExecutor executor;
    auto execResult = executor.Execute(compiled, builder, frameCtx, scene.device, syncSched, cmdPoolAlloc);
    if (!execResult) {
        std::println("[rg_demo] RenderGraph execute failed");
        return false;
    }

    auto& stats = executor.GetStats();
    (void)stats;  // Available for profiling overlay in the future

    return true;
}

// ============================================================================
// Global state
// ============================================================================

static WindowManager* g_wm = nullptr;
static SurfaceManager* g_sm = nullptr;
static SceneResources* g_scene = nullptr;
static WindowHandle g_mainWindow;
static bool g_shouldQuit = false;
static miki::frame::SyncScheduler* g_scheduler = nullptr;
static miki::frame::CommandPoolAllocator* g_poolAllocator = nullptr;

static void RenderOneFrame(WindowHandle win) {
    auto* fm = g_sm->GetFrameManager(win);
    if (!fm) {
        return;
    }
    auto frameResult = fm->BeginFrame();
    if (!frameResult) {
        return;
    }
    auto& frameCtx = *frameResult;

    if (!BuildAndExecuteRenderGraph(*g_scene, frameCtx, *g_scheduler, *g_poolAllocator)) {
        return;
    }

    // EndFrame with no command buffers — executor already submitted via SyncScheduler
    // We still need to signal present, so pass an empty span
    (void)fm->EndFrame(std::span<const CommandBufferHandle>{});
}

static void MainLoopIteration() {
    auto events = g_wm->PollEvents();
    for (auto& ev : events) {
        std::visit(
            [&](auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, neko::platform::KeyDown>) {
                    if (e.key == neko::platform::Key::Escape) {
                        g_shouldQuit = true;
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::MouseButton>) {
                    if (e.button == neko::platform::MouseBtn::Left) {
                        g_scene->dragging = (e.action == neko::platform::Action::Press);
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::MouseMove>) {
                    if (g_scene->dragging) {
                        g_scene->rotY += static_cast<float>(e.dx) * 0.01f;
                        g_scene->rotX += static_cast<float>(e.dy) * 0.01f;
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::Scroll>) {
                    g_scene->distance -= static_cast<float>(e.dy) * 0.3f;
                    g_scene->distance = std::clamp(g_scene->distance, 2.0f, 15.0f);
                } else if constexpr (std::is_same_v<T, neko::platform::Resize>) {
                    (void)g_sm->ResizeSurface(ev.window, e.width, e.height);
                } else if constexpr (std::is_same_v<T, neko::platform::CloseRequested>) {
                    g_shouldQuit = true;
                }
            },
            ev.event
        );
    }

    if (g_shouldQuit) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
        g_scene->Cleanup();
#endif
        return;
    }
    RenderOneFrame(g_mainWindow);
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char** argv) {
    auto& logger = miki::debug::StructuredLogger::Instance();
    logger.AddSink(miki::debug::ConsoleSink{});
    logger.StartDrainThread();

    auto backend = ParseBackendFromArgs(argc, argv);
    std::println("[rg_demo] RenderGraph torus+cube demo with {} backend", BackendName(backend));

    // 1. Window manager
    auto glfwBackend = std::make_unique<GlfwWindowBackend>(backend, true);
    auto* backendPtr = glfwBackend.get();
    auto wmResult = WindowManager::Create(std::move(glfwBackend));
    if (!wmResult) {
        std::println("[rg_demo] WindowManager failed");
        return 1;
    }
    auto wm = std::move(*wmResult);
    g_wm = &wm;

    // 2. Window
    auto winResult = wm.CreateWindow({.title = "miki RenderGraph — Torus + Cube", .width = 1280, .height = 720});
    if (!winResult) {
        std::println("[rg_demo] CreateWindow failed");
        return 1;
    }
    g_mainWindow = *winResult;

    // 3. Device
    DeviceDesc desc{
        .backend = backend,
        .enableValidation = true,
        .windowBackend = backendPtr,
        .nativeToken = wm.GetNativeToken(g_mainWindow)
    };
    auto deviceResult = CreateDevice(desc);
    if (!deviceResult) {
        std::println("[rg_demo] Device creation failed");
        return 1;
    }
    auto device = std::move(*deviceResult);

    // 4. Surface manager
    auto smResult = SurfaceManager::Create(device.GetHandle(), wm);
    if (!smResult) {
        std::println("[rg_demo] SurfaceManager failed");
        return 1;
    }
    auto surfMgr = std::move(*smResult);
    g_sm = &surfMgr;

    auto attachResult = surfMgr.AttachSurface(g_mainWindow, {.presentMode = PresentMode::Fifo});
    if (!attachResult) {
        std::println("[rg_demo] AttachSurface failed");
        return 1;
    }

    // 5. Shader compilation
    auto shaderTarget
        = device.GetHandle().Dispatch([](auto& d) { return miki::shader::PreferredShaderTarget(d.GetCapabilities()); });
    auto shaders = CompileShaderPair(kBlinnPhongSlangSource, shaderTarget, "blinn_phong");
    if (!shaders) {
        std::println("[rg_demo] Shader compilation failed");
        return 1;
    }

    // 6. Scene resources (geometry + pipeline)
    SceneResources scene;
    g_scene = &scene;
    if (!scene.Init(device.GetHandle(), *shaders, &surfMgr, g_mainWindow)) {
        std::println("[rg_demo] Scene init failed");
        return 1;
    }

    // 7. Frame infrastructure for RenderGraph executor
    auto* fm = surfMgr.GetFrameManager(g_mainWindow);
    if (!fm) {
        std::println("[rg_demo] No FrameManager");
        return 1;
    }

    miki::frame::SyncScheduler syncSched;
    auto timelines = device.GetHandle().Dispatch([](auto& d) { return d.GetQueueTimelines(); });
    syncSched.Init(timelines);
    g_scheduler = &syncSched;

    miki::frame::CommandPoolAllocator::Desc poolDesc{
        .device = device.GetHandle(),
        .framesInFlight = fm->FramesInFlight(),
    };
    auto poolResult = miki::frame::CommandPoolAllocator::Create(poolDesc);
    if (!poolResult) {
        std::println("[rg_demo] CommandPoolAllocator failed");
        return 1;
    }
    auto cmdPoolAlloc = std::move(*poolResult);
    g_poolAllocator = &cmdPoolAlloc;

    std::println("[rg_demo] Running. Drag to rotate, scroll to zoom. ESC to quit.");
    std::println("[rg_demo] Each frame: Builder({} passes) -> Compiler -> Executor", 3);

    // 8. Live resize
#ifndef __EMSCRIPTEN__
    backendPtr->SetLiveResizeCallback([](WindowHandle w, uint32_t width, uint32_t height) {
        (void)g_sm->ResizeSurface(w, width, height);
        RenderOneFrame(w);
    });
#endif

    // 9. Main loop
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!g_shouldQuit) {
        MainLoopIteration();
    }
    scene.Cleanup();
    logger.Shutdown();
#endif
    return 0;
}
