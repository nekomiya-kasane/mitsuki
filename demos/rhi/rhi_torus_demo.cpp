/** @file rhi_torus_demo.cpp
 *  @brief RHI torus demo — renders a lit torus with arcball interaction on ALL backends.
 *
 *  Usage: rhi_torus_demo [--backend vulkan|vulkan11|d3d12|opengl|webgpu]
 *  Default: VulkanCompat (desktop), WebGPU (Emscripten).
 *
 *  Demonstrates:
 *    - CPU-side parametric mesh generation (DemoMeshGen.h)
 *    - Push constants for MVP transform (MathUtils.h column-major float4x4)
 *    - Blinn-Phong per-pixel lighting
 *    - Arcball camera (mouse drag rotate, scroll zoom)
 *    - Live resize rendering during Win32 modal drag
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
#pragma clang diagnostic pop

using namespace miki::demo;
using namespace miki::platform;
using namespace miki::rhi;
using namespace miki::core;

// ============================================================================
// Push constant layout (must match shader: column-major float4x4)
// ============================================================================

struct PushConst {
    float4x4 mvp;
    float4x4 model;
};
static_assert(sizeof(PushConst) == 128);

// ============================================================================
// TorusRenderer
// ============================================================================

struct TorusRenderer {
    DeviceHandle device;
    ShaderModuleHandle vertModule, fragModule;
    PipelineLayoutHandle pipelineLayout;
    PipelineHandle pipeline;
    BufferHandle vertexBuffer, indexBuffer;
    uint32_t indexCount = 0;
    SurfaceManager* sm = nullptr;
    WindowHandle window{};

    // Camera state
    float rotX = 0.3f, rotY = 0.0f;
    float distance = 3.5f;
    bool dragging = false;

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
            std::println("[torus] VS create failed");
            return false;
        }
        vertModule = *vs;

        ShaderModuleDesc fd{
            .stage = ShaderStage::Fragment, .code = shaders.fs.data, .entryPoint = shaders.fs.entryPoint.c_str()
        };
        auto fs = createShader(fd);
        if (!fs) {
            std::println("[torus] FS create failed");
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
            std::println("[torus] PipelineLayout failed");
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
            std::println("[torus] PSO failed");
            return false;
        }
        pipeline = *pso;

        auto mesh = GenerateTorus(0.7f, 0.3f, 48, 32);
        indexCount = static_cast<uint32_t>(mesh.indices.size());
        return UploadMesh(mesh);
    }

    auto UploadMesh(const DemoMeshData& mesh) -> bool {
        uint64_t vbSize = mesh.vertices.size() * sizeof(DemoVertex);
        uint64_t ibSize = mesh.indices.size() * sizeof(uint32_t);

        auto createBuf = [&](const BufferDesc& bd) -> RhiResult<BufferHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<BufferHandle> { return d.CreateBuffer(bd); });
        };

        auto vbResult = createBuf(
            {.size = vbSize, .usage = BufferUsage::Vertex, .memory = MemoryLocation::CpuToGpu, .debugName = "TorusVB"}
        );
        if (!vbResult) {
            std::println("[torus] VB create failed");
            return false;
        }
        vertexBuffer = *vbResult;

        auto ibResult = createBuf(
            {.size = ibSize, .usage = BufferUsage::Index, .memory = MemoryLocation::CpuToGpu, .debugName = "TorusIB"}
        );
        if (!ibResult) {
            std::println("[torus] IB create failed");
            return false;
        }
        indexBuffer = *ibResult;

        auto mapAndCopy = [&](BufferHandle h, const void* data, uint64_t size) -> bool {
            auto ptr = device.Dispatch([&](auto& d) -> RhiResult<void*> { return d.MapBuffer(h); });
            if (!ptr) {
                return false;
            }
            std::memcpy(*ptr, data, size);
            (void)device.Dispatch([&](auto& d) {
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

    auto RecordFrame(TextureViewHandle colorView, uint32_t w, uint32_t h) -> std::optional<CommandBufferHandle> {
        auto* fm = sm->GetFrameManager(window);
        if (!fm) {
            return std::nullopt;
        }
        auto acqResult = fm->AcquireCommandList(QueueType::Graphics);
        if (!acqResult) {
            return std::nullopt;
        }
        auto cmdAcq = *acqResult;

        TextureHandle colorTex = device.Dispatch([&](auto& d) { return d.GetTextureViewTexture(colorView); });

        float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
        auto model = Mul4x4(MakeRotateX(rotX), MakeRotateY(rotY));
        auto view = MakeLookAt({0, 0, distance}, {0, 0, 0}, {0, 1, 0});
        auto proj = MakePerspective(std::numbers::pi_v<float> / 4.0f, aspect, 0.1f, 100.0f);
        PushConst pc{.mvp = Mul4x4(proj, Mul4x4(view, model)), .model = model};

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
            colorAtt.view = colorView;
            colorAtt.loadOp = AttachmentLoadOp::Clear;
            colorAtt.storeOp = AttachmentStoreOp::Store;
            colorAtt.clearValue = clearVal;
            RenderingDesc rd{
                .renderArea = Rect2D{.offset = {0, 0}, .extent = {w, h}},
                .colorAttachments = {&colorAtt, 1},
            };

            cmd.CmdBeginRendering(rd);
            cmd.CmdBindPipeline(pipeline);
            cmd.CmdSetViewport(
                {.x = 0,
                 .y = 0,
                 .width = static_cast<float>(w),
                 .height = static_cast<float>(h),
                 .minDepth = 0,
                 .maxDepth = 1}
            );
            cmd.CmdSetScissor({.offset = {0, 0}, .extent = {w, h}});
            cmd.CmdPushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(PushConst), &pc);
            cmd.CmdBindVertexBuffer(0, vertexBuffer, 0);
            cmd.CmdBindIndexBuffer(indexBuffer, 0, IndexType::Uint32);
            cmd.CmdDrawIndexed(indexCount, 1, 0, 0, 0);
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
        return cmdAcq.bufferHandle;
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

static WindowManager* g_wm = nullptr;
static SurfaceManager* g_sm = nullptr;
static TorusRenderer* g_renderer = nullptr;
static WindowHandle g_mainWindow;
static bool g_shouldQuit = false;

static void RenderOneFrame(WindowHandle w) {
    auto frameResult = g_sm->BeginFrame(w);
    if (!frameResult) {
        return;
    }
    auto& frame = *frameResult;
    auto cmdBuf = g_renderer->RecordFrame(frame.swapchainImageView, frame.width, frame.height);
    if (!cmdBuf) {
        return;
    }
    (void)g_sm->EndFrame(w, *cmdBuf);
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
                        g_renderer->dragging = (e.action == neko::platform::Action::Press);
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::MouseMove>) {
                    if (g_renderer->dragging) {
                        g_renderer->rotY += static_cast<float>(e.dx) * 0.01f;
                        g_renderer->rotX += static_cast<float>(e.dy) * 0.01f;
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::Scroll>) {
                    g_renderer->distance -= static_cast<float>(e.dy) * 0.3f;
                    g_renderer->distance = std::clamp(g_renderer->distance, 1.5f, 10.0f);
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
        g_renderer->Cleanup();
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
    std::println("[torus] Starting with {} backend", BackendName(backend));

    auto glfwBackend = std::make_unique<GlfwWindowBackend>(backend, true);
    auto* backendPtr = glfwBackend.get();
    auto wmResult = WindowManager::Create(std::move(glfwBackend));
    if (!wmResult) {
        std::println("[torus] WindowManager::Create failed");
        return 1;
    }
    auto wm = std::move(*wmResult);
    g_wm = &wm;

    auto winResult = wm.CreateWindow({.title = "miki Torus", .width = 1280, .height = 720});
    if (!winResult) {
        std::println("[torus] CreateWindow failed");
        return 1;
    }
    g_mainWindow = *winResult;
    auto nativeHandle = wm.GetNativeHandle(g_mainWindow);

    DeviceDesc desc{
        .backend = backend,
        .enableValidation = true,
        .windowBackend = backendPtr,
        .nativeToken = wm.GetNativeToken(g_mainWindow)
    };
    auto deviceResult = CreateDevice(desc);
    if (!deviceResult) {
        std::println("[torus] Device creation failed");
        return 1;
    }
    auto device = std::move(*deviceResult);

    auto smResult = SurfaceManager::Create(device.GetHandle());
    if (!smResult) {
        std::println("[torus] SurfaceManager::Create failed");
        return 1;
    }
    auto surfMgr = std::move(*smResult);
    g_sm = &surfMgr;

    auto attachResult = surfMgr.AttachSurface(g_mainWindow, nativeHandle, {.presentMode = PresentMode::Fifo});
    if (!attachResult) {
        std::println("[torus] AttachSurface failed");
        return 1;
    }

    auto shaders = CompileShaderPair(kTorusSlangSource, backend, "torus");
    if (!shaders) {
        std::println("[torus] Shader compilation failed");
        return 1;
    }

    TorusRenderer renderer;
    g_renderer = &renderer;
    if (!renderer.Init(device.GetHandle(), *shaders, &surfMgr, g_mainWindow)) {
        std::println("[torus] Renderer init failed");
        return 1;
    }
    std::println("[torus] {} torus running. Drag to rotate, scroll to zoom. ESC to quit.", BackendName(backend));

#ifndef __EMSCRIPTEN__
    backendPtr->SetLiveResizeCallback([](WindowHandle w, uint32_t width, uint32_t height) {
        (void)g_sm->ResizeSurface(w, width, height);
        RenderOneFrame(w);
    });
#endif

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!g_shouldQuit) {
        MainLoopIteration();
    }
    renderer.Cleanup();
    logger.Shutdown();
#endif
    return 0;
}
