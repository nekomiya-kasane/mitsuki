/** @file rhi_torus_demo_multi.cpp
 *  @brief Multi-window RHI demo — 5 windows in a 3-level tree, ALL backends.
 *
 *  Usage: rhi_torus_demo_multi [--backend vulkan|vulkan11|d3d12|opengl|webgpu]
 *
 *  Window tree structure (3 levels):
 *    Root (window 0: torus)
 *      +-- Child A (window 1: torus)
 *      |     +-- Grandchild A1 (window 2: torus)
 *      |     +-- Grandchild A2 (window 3: torus)
 *      +-- Child B (window 4: cube)
 *
 *  Windows 0-3 share the same torus geometry buffers (one VB+IB, multiple FrameManagers).
 *  Window 4 renders a cube with a different diffuse color.
 *  Each window has independent frame pacing and live resize support.
 *  Arcball camera is shared across all torus windows.
 */

#include "common/DemoCommon.h"
#include "common/DemoMeshGen.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <numbers>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char kTorusSlangSource[] = {
#embed "shaders/torus.slang"
    , '\0'
};
static constexpr char kCubeSlangSource[] = {
#embed "shaders/cube.slang"
    , '\0'
};
#pragma clang diagnostic pop

using namespace miki::demo;
using namespace miki::platform;
using namespace miki::rhi;
using namespace miki::core;

// ============================================================================
// Push constants (matches shader layout: column-major float4x4)
// ============================================================================

struct PushConst {
    float4x4 mvp;
    float4x4 model;
};
static_assert(sizeof(PushConst) == 128);

// ============================================================================
// Scene — shared geometry + pipeline for one mesh type
// ============================================================================

struct Scene {
    PipelineHandle pipeline;
    PipelineLayoutHandle pipelineLayout;
    ShaderModuleHandle vertModule, fragModule;
    BufferHandle vertexBuffer, indexBuffer;
    uint32_t indexCount = 0;

    auto Init(DeviceHandle dev, CompiledShaderPair& shaders, Format colorFmt, const DemoMeshData& mesh) -> bool {
        auto createShader = [&](const ShaderModuleDesc& sd) -> RhiResult<ShaderModuleHandle> {
            return dev.Dispatch([&](auto& d) -> RhiResult<ShaderModuleHandle> { return d.CreateShaderModule(sd); });
        };

        ShaderModuleDesc vd{
            .stage = ShaderStage::Vertex, .code = shaders.vs.data, .entryPoint = shaders.vs.entryPoint.c_str()
        };
        auto vs = createShader(vd);
        if (!vs) {
            std::println("[multi] VS create failed");
            return false;
        }
        vertModule = *vs;

        ShaderModuleDesc fd{
            .stage = ShaderStage::Fragment, .code = shaders.fs.data, .entryPoint = shaders.fs.entryPoint.c_str()
        };
        auto fs = createShader(fd);
        if (!fs) {
            std::println("[multi] FS create failed");
            return false;
        }
        fragModule = *fs;

        PushConstantRange pcRange{
            .stages = ShaderStage::Vertex | ShaderStage::Fragment, .offset = 0, .size = sizeof(PushConst)
        };
        PipelineLayoutDesc pld{.pushConstants = {&pcRange, 1}};
        auto pl = dev.Dispatch([&](auto& d) -> RhiResult<PipelineLayoutHandle> { return d.CreatePipelineLayout(pld); });
        if (!pl) {
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
        auto pso = dev.Dispatch([&](auto& d) -> RhiResult<PipelineHandle> { return d.CreateGraphicsPipeline(gpd); });
        if (!pso) {
            return false;
        }
        pipeline = *pso;

        indexCount = static_cast<uint32_t>(mesh.indices.size());
        uint64_t vbSize = mesh.vertices.size() * sizeof(DemoVertex);
        uint64_t ibSize = mesh.indices.size() * sizeof(uint32_t);

        auto createBuf = [&](const BufferDesc& bd) -> RhiResult<BufferHandle> {
            return dev.Dispatch([&](auto& d) -> RhiResult<BufferHandle> { return d.CreateBuffer(bd); });
        };

        auto vbResult = createBuf({.size = vbSize, .usage = BufferUsage::Vertex, .memory = MemoryLocation::CpuToGpu});
        if (!vbResult) {
            return false;
        }
        vertexBuffer = *vbResult;

        auto ibResult = createBuf({.size = ibSize, .usage = BufferUsage::Index, .memory = MemoryLocation::CpuToGpu});
        if (!ibResult) {
            return false;
        }
        indexBuffer = *ibResult;

        auto mapAndCopy = [&](BufferHandle h, const void* data, uint64_t size) -> bool {
            auto ptr = dev.Dispatch([&](auto& d) -> RhiResult<void*> { return d.MapBuffer(h); });
            if (!ptr) {
                return false;
            }
            std::memcpy(*ptr, data, size);
            (void)dev.Dispatch([&](auto& d) {
                d.UnmapBuffer(h);
                return 0;
            });
            return true;
        };
        if (!mapAndCopy(vertexBuffer, mesh.vertices.data(), vbSize)) {
            return false;
        }
        if (!mapAndCopy(indexBuffer, mesh.indices.data(), ibSize)) {
            return false;
        }
        return true;
    }

    void Cleanup(DeviceHandle dev) {
        (void)dev.Dispatch([&](auto& d) {
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
            if (vertexBuffer.IsValid()) {
                d.DestroyBuffer(vertexBuffer);
            }
            if (indexBuffer.IsValid()) {
                d.DestroyBuffer(indexBuffer);
            }
            return 0;
        });
    }
};

// ============================================================================
// Global state
// ============================================================================

static constexpr uint32_t kNumWindows = 5;
static constexpr uint32_t kTorusWindowCount = 4;

static WindowManager* g_wm = nullptr;
static SurfaceManager* g_sm = nullptr;
static DeviceHandle g_device;
static std::array<WindowHandle, kNumWindows> g_windows;
static Scene* g_torusScene = nullptr;
static Scene* g_cubeScene = nullptr;
static bool g_shouldQuit = false;

// Shared camera
static float g_rotX = 0.3f, g_rotY = 0.0f, g_distance = 3.5f;
static bool g_dragging = false;

static auto BuildPushConst(float rotX, float rotY, float dist, float aspect) -> PushConst {
    auto model = Mul4x4(MakeRotateX(rotX), MakeRotateY(rotY));
    auto view = MakeLookAt({0, 0, dist}, {0, 0, 0}, {0, 1, 0});
    auto proj = MakePerspective(std::numbers::pi_v<float> / 4.0f, aspect, 0.1f, 100.0f);
    return {.mvp = Mul4x4(proj, Mul4x4(view, model)), .model = model};
}

static void RenderWindow(WindowHandle win, Scene& scene) {
    auto* fm = g_sm->GetFrameManager(win);
    if (!fm) {
        return;
    }
    auto frameResult = g_sm->BeginFrame(win);
    if (!frameResult) {
        return;
    }
    auto& frame = *frameResult;

    float aspect = (frame.height > 0) ? static_cast<float>(frame.width) / static_cast<float>(frame.height) : 1.0f;
    auto pc = BuildPushConst(g_rotX, g_rotY, g_distance, aspect);

    TextureHandle colorTex
        = g_device.Dispatch([&](auto& d) { return d.GetTextureViewTexture(frame.swapchainImageView); });

    auto acqResult = fm->AcquireCommandList(QueueType::Graphics);
    if (!acqResult) {
        return;
    }
    auto cmdAcq = *acqResult;

    (void)cmdAcq.listHandle.Dispatch([&](auto& cmd) {
        cmd.Begin();

        TextureBarrierDesc toRender{};
        toRender.srcStage = PipelineStage::TopOfPipe;
        toRender.dstStage = PipelineStage::ColorAttachmentOutput;
        toRender.srcAccess = AccessFlags::None;
        toRender.dstAccess = AccessFlags::ColorAttachmentWrite;
        toRender.oldLayout = TextureLayout::Undefined;
        toRender.newLayout = TextureLayout::ColorAttachment;
        cmd.CmdTextureBarrier(colorTex, toRender);

        ClearValue clearVal{};
        clearVal.color = {0.06f, 0.06f, 0.09f, 1.0f};
        RenderingAttachment colorAtt{};
        colorAtt.view = frame.swapchainImageView;
        colorAtt.loadOp = AttachmentLoadOp::Clear;
        colorAtt.storeOp = AttachmentStoreOp::Store;
        colorAtt.clearValue = clearVal;
        RenderingDesc rd{
            .renderArea = Rect2D{.offset = {0, 0}, .extent = {frame.width, frame.height}},
            .colorAttachments = {&colorAtt, 1},
        };

        cmd.CmdBeginRendering(rd);
        cmd.CmdBindPipeline(scene.pipeline);
        cmd.CmdSetViewport(
            {.x = 0,
             .y = 0,
             .width = static_cast<float>(frame.width),
             .height = static_cast<float>(frame.height),
             .minDepth = 0,
             .maxDepth = 1}
        );
        cmd.CmdSetScissor({.offset = {0, 0}, .extent = {frame.width, frame.height}});
        cmd.CmdPushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(PushConst), &pc);
        cmd.CmdBindVertexBuffer(0, scene.vertexBuffer, 0);
        cmd.CmdBindIndexBuffer(scene.indexBuffer, 0, IndexType::Uint32);
        cmd.CmdDrawIndexed(scene.indexCount, 1, 0, 0, 0);
        cmd.CmdEndRendering();

        TextureBarrierDesc toPresent{};
        toPresent.srcStage = PipelineStage::ColorAttachmentOutput;
        toPresent.dstStage = PipelineStage::BottomOfPipe;
        toPresent.srcAccess = AccessFlags::ColorAttachmentWrite;
        toPresent.dstAccess = AccessFlags::None;
        toPresent.oldLayout = TextureLayout::ColorAttachment;
        toPresent.newLayout = TextureLayout::Present;
        cmd.CmdTextureBarrier(colorTex, toPresent);

        cmd.End();
        return 0;
    });
    (void)g_sm->EndFrame(win, cmdAcq.bufferHandle);
}

static void RenderAllWindows() {
    for (uint32_t i = 0; i < kTorusWindowCount; ++i) {
        RenderWindow(g_windows[i], *g_torusScene);
    }
    RenderWindow(g_windows[4], *g_cubeScene);
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
                        g_dragging = (e.action == neko::platform::Action::Press);
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::MouseMove>) {
                    if (g_dragging) {
                        g_rotY += static_cast<float>(e.dx) * 0.01f;
                        g_rotX += static_cast<float>(e.dy) * 0.01f;
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::Scroll>) {
                    g_distance -= static_cast<float>(e.dy) * 0.3f;
                    g_distance = std::clamp(g_distance, 1.5f, 10.0f);
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
        g_torusScene->Cleanup(g_device);
        g_cubeScene->Cleanup(g_device);
#endif
        return;
    }
    RenderAllWindows();
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char** argv) {
    auto& logger = miki::debug::StructuredLogger::Instance();
    logger.AddSink(miki::debug::ConsoleSink{});
    logger.StartDrainThread();

    auto backend = ParseBackendFromArgs(argc, argv);
    std::println("[multi] Starting 5-window demo with {} backend", BackendName(backend));

    // 1. WindowManager
    auto glfwBackend = std::make_unique<GlfwWindowBackend>(backend, true);
    auto* backendPtr = glfwBackend.get();
    auto wmResult = WindowManager::Create(std::move(glfwBackend));
    if (!wmResult) {
        std::println("[multi] WindowManager failed");
        return 1;
    }
    auto wm = std::move(*wmResult);
    g_wm = &wm;

    // 2. Create 5 windows in a 3-level tree
    struct WinSpec {
        const char* title;
        uint32_t w, h;
        int parentIdx;
    };
    std::array<WinSpec, kNumWindows> specs = {{
        {"[Root] Torus", 800, 600, -1},
        {"[Child A] Torus", 640, 480, 0},
        {"[A1] Torus", 480, 360, 1},
        {"[A2] Torus", 480, 360, 1},
        {"[Child B] Cube", 640, 480, 0},
    }};

    for (uint32_t i = 0; i < kNumWindows; ++i) {
        WindowDesc wd{.title = specs[i].title, .width = specs[i].w, .height = specs[i].h};
        if (specs[i].parentIdx >= 0) {
            wd.parent = g_windows[static_cast<uint32_t>(specs[i].parentIdx)];
        }
        auto result = wm.CreateWindow(wd);
        if (!result) {
            std::println("[multi] CreateWindow {} failed", i);
            return 1;
        }
        g_windows[i] = *result;
        std::println(
            "[multi] Window {}: '{}' ({}x{}) parent={}", i, specs[i].title, specs[i].w, specs[i].h, specs[i].parentIdx
        );
    }

    // 3. Device
    DeviceDesc desc{
        .backend = backend,
        .enableValidation = true,
        .windowBackend = backendPtr,
        .nativeToken = wm.GetNativeToken(g_windows[0])
    };
    auto deviceResult = CreateDevice(desc);
    if (!deviceResult) {
        std::println("[multi] Device creation failed");
        return 1;
    }
    auto device = std::move(*deviceResult);
    g_device = device.GetHandle();

    // 4. SurfaceManager — attach all windows
    auto smResult = SurfaceManager::Create(g_device);
    if (!smResult) {
        std::println("[multi] SurfaceManager failed");
        return 1;
    }
    auto surfMgr = std::move(*smResult);
    g_sm = &surfMgr;

    for (uint32_t i = 0; i < kNumWindows; ++i) {
        auto nativeHandle = wm.GetNativeHandle(g_windows[i]);
        auto attachResult = surfMgr.AttachSurface(g_windows[i], nativeHandle, {.presentMode = PresentMode::Fifo});
        if (!attachResult) {
            std::println("[multi] AttachSurface {} failed", i);
            return 1;
        }
    }

    // 5. Compile shaders
    auto torusShaders = CompileShaderPair(kTorusSlangSource, backend, "torus");
    if (!torusShaders) {
        return 1;
    }
    auto cubeShaders = CompileShaderPair(kCubeSlangSource, backend, "cube");
    if (!cubeShaders) {
        return 1;
    }

    // 6. Create scenes
    auto* rs = surfMgr.GetRenderSurface(g_windows[0]);
    Format colorFmt = rs ? rs->GetFormat() : Format::BGRA8_SRGB;

    Scene torusScene, cubeScene;
    g_torusScene = &torusScene;
    g_cubeScene = &cubeScene;

    auto torusMesh = GenerateTorus(0.7f, 0.3f, 48, 32);
    if (!torusScene.Init(g_device, *torusShaders, colorFmt, torusMesh)) {
        std::println("[multi] Torus scene init failed");
        return 1;
    }
    auto cubeMesh = GenerateCube(0.6f);
    if (!cubeScene.Init(g_device, *cubeShaders, colorFmt, cubeMesh)) {
        std::println("[multi] Cube scene init failed");
        return 1;
    }

    std::println("[multi] 5 windows running. Drag to rotate, scroll to zoom. ESC to quit.");

    // 7. Live resize callback
#ifndef __EMSCRIPTEN__
    backendPtr->SetLiveResizeCallback([](WindowHandle w, uint32_t width, uint32_t height) {
        (void)g_sm->ResizeSurface(w, width, height);
        bool isCube = (w.id == g_windows[4].id && w.generation == g_windows[4].generation);
        RenderWindow(w, isCube ? *g_cubeScene : *g_torusScene);
    });
#endif

    // 8. Main loop
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!g_shouldQuit) {
        MainLoopIteration();
    }
    (void)g_device.Dispatch([](auto& d) {
        d.WaitIdle();
        return 0;
    });
    torusScene.Cleanup(g_device);
    cubeScene.Cleanup(g_device);
    logger.Shutdown();
#endif
    return 0;
}
