/** @file rhi_triangle_demo.cpp
 *  @brief RHI triangle demo — renders a color triangle on ALL backends.
 *
 *  Usage: rhi_triangle_demo [--backend vulkan|vulkan11|d3d12|opengl|webgpu]
 *  Default: VulkanCompat (desktop), WebGPU (Emscripten).
 *
 *  Demonstrates the recommended application architecture:
 *    - DeviceFactory (OwnedDevice) for device creation
 *    - SurfaceManager for swapchain lifecycle and frame pacing
 *    - AcquireCommandList for type-erased command recording
 *    - Mesh shader pipeline (Vulkan 1.4 / D3D12) with vertex shader fallback
 */

#include "common/DemoCommon.h"

#include <cstdint>

// Embed shader sources at compile time (C++23 #embed)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char kTriangleSlangSource[] = {
#embed "shaders/triangle.slang"
    , '\0'
};
static constexpr char kTriangleMeshSlangSource[] = {
#embed "shaders/triangle_mesh.slang"
    , '\0'
};
#pragma clang diagnostic pop

using namespace miki::demo;
using namespace miki::platform;
using namespace miki::rhi;
namespace shader = miki::shader;

// ============================================================================
// Shader compilation (Slang -> per-backend bytecode)
// ============================================================================

struct CompiledShaders {
    shader::ShaderBlob vs;  // empty when using mesh path
    shader::ShaderBlob ms;  // empty when using vertex path
    shader::ShaderBlob fs;
    bool useMeshShader = false;
};

static auto CompileShaders(BackendType backend, bool useMesh) -> std::optional<CompiledShaders> {
    auto compilerResult = shader::SlangCompiler::Create();
    if (!compilerResult) {
        std::println("[demo] shader::SlangCompiler::Create failed");
        return std::nullopt;
    }
    auto compiler = std::move(*compilerResult);
    auto target = shader::ShaderTargetForBackend(backend);

    if (useMesh) {
        std::string src(kTriangleMeshSlangSource);
        auto ms = CompileStage(compiler, src, "ms_main", shader::ShaderStage::Mesh, target, "MS");
        if (!ms) {
            return std::nullopt;
        }
        auto fs = CompileStage(compiler, src, "fs_mesh_main", shader::ShaderStage::Fragment, target, "FS(mesh)");
        if (!fs) {
            return std::nullopt;
        }
        return CompiledShaders{.ms = std::move(*ms), .fs = std::move(*fs), .useMeshShader = true};
    }

    std::string src(kTriangleSlangSource);
    auto vs = CompileStage(compiler, src, "vs_main", shader::ShaderStage::Vertex, target, "VS");
    if (!vs) {
        return std::nullopt;
    }
    auto fs = CompileStage(compiler, src, "fs_main", shader::ShaderStage::Fragment, target, "FS");
    if (!fs) {
        return std::nullopt;
    }
    return CompiledShaders{.vs = std::move(*vs), .fs = std::move(*fs)};
}

// ============================================================================
// TriangleRenderer — owns only pipeline resources, records commands
// ============================================================================

struct TriangleRenderer {
    DeviceHandle device;
    ShaderModuleHandle vertShader;
    ShaderModuleHandle meshShader;
    ShaderModuleHandle fragShader;
    PipelineLayoutHandle pipelineLayout;
    PipelineHandle pipeline;
    SurfaceManager* surfaceManager = nullptr;
    WindowHandle window{};
    bool useMeshPath = false;

    auto Init(DeviceHandle dev, CompiledShaders& shaders, SurfaceManager* sm, WindowHandle win) -> bool {
        device = dev;
        surfaceManager = sm;
        window = win;
        useMeshPath = shaders.useMeshShader;

        auto createShader = [&](const ShaderModuleDesc& sd) -> RhiResult<ShaderModuleHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<ShaderModuleHandle> { return d.CreateShaderModule(sd); });
        };
        auto createPipelineLayout = [&](const PipelineLayoutDesc& pld) -> RhiResult<PipelineLayoutHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<PipelineLayoutHandle> {
                return d.CreatePipelineLayout(pld);
            });
        };
        auto createPipeline = [&](const GraphicsPipelineDesc& gpd) -> RhiResult<PipelineHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<PipelineHandle> { return d.CreateGraphicsPipeline(gpd); });
        };

        // Shader modules
        if (useMeshPath) {
            ShaderModuleDesc md{
                .stage = ShaderStage::Mesh, .code = shaders.ms.data, .entryPoint = shaders.ms.entryPoint.c_str()
            };
            auto ms = createShader(md);
            if (!ms) {
                std::println("[demo] MS create failed");
                return false;
            }
            meshShader = *ms;
        } else {
            ShaderModuleDesc vd{
                .stage = ShaderStage::Vertex, .code = shaders.vs.data, .entryPoint = shaders.vs.entryPoint.c_str()
            };
            auto vs = createShader(vd);
            if (!vs) {
                std::println("[demo] VS create failed");
                return false;
            }
            vertShader = *vs;
        }

        ShaderModuleDesc fd{
            .stage = ShaderStage::Fragment, .code = shaders.fs.data, .entryPoint = shaders.fs.entryPoint.c_str()
        };
        auto fs = createShader(fd);
        if (!fs) {
            std::println("[demo] FS create failed");
            return false;
        }
        fragShader = *fs;

        auto pl = createPipelineLayout(PipelineLayoutDesc{});
        if (!pl) {
            std::println("[demo] PipelineLayout failed");
            return false;
        }
        pipelineLayout = *pl;

        // Graphics pipeline
        ColorAttachmentBlend blend{};
        auto* rs = surfaceManager->GetRenderSurface(window);
        Format colorFmt = rs ? rs->GetFormat() : Format::BGRA8_SRGB;
        GraphicsPipelineDesc gpd{};
        if (useMeshPath) {
            gpd.meshShader = meshShader;
        } else {
            gpd.vertexShader = vertShader;
        }
        gpd.fragmentShader = fragShader;
        gpd.topology = PrimitiveTopology::TriangleList;
        gpd.cullMode = CullMode::None;
        gpd.depthTestEnable = false;
        gpd.depthWriteEnable = false;
        gpd.colorBlends = {&blend, 1};
        gpd.colorFormats = {&colorFmt, 1};
        gpd.pipelineLayout = pipelineLayout;
        auto pso = createPipeline(gpd);
        if (!pso) {
            std::println("[demo] PSO failed");
            return false;
        }
        pipeline = *pso;

        return true;
    }

    /// @brief Acquire a per-frame command list from FrameManager's pool.
    auto AcquireCommandList() -> std::optional<CommandListAcquisition> {
        auto* fm = surfaceManager->GetFrameManager(window);
        if (!fm) {
            return std::nullopt;
        }
        auto acq = fm->AcquireCommandList(QueueType::Graphics);
        if (!acq) {
            return std::nullopt;
        }
        return *acq;
    }

    auto RecordFrame(TextureViewHandle colorView, uint32_t w, uint32_t h) -> std::optional<CommandBufferHandle> {
        auto cmdAcqOpt = AcquireCommandList();
        if (!cmdAcqOpt) {
            return std::nullopt;
        }
        auto cmdAcq = *cmdAcqOpt;

        // Query parent texture from view — avoids redundant parameter
        TextureHandle colorTex = device.Dispatch([&](auto& d) { return d.GetTextureViewTexture(colorView); });

        (void)cmdAcq.listHandle.Dispatch([&](auto& cmd) {
            cmd.Begin();

            // Transition: Undefined -> ColorAttachment
            TextureBarrierDesc toRender{};
            toRender.srcStage = PipelineStage::TopOfPipe;
            toRender.dstStage = PipelineStage::ColorAttachmentOutput;
            toRender.srcAccess = AccessFlags::None;
            toRender.dstAccess = AccessFlags::ColorAttachmentWrite;
            toRender.oldLayout = TextureLayout::Undefined;
            toRender.newLayout = TextureLayout::ColorAttachment;
            cmd.CmdTextureBarrier(colorTex, toRender);

            // Render pass
            ClearValue clearVal{};
            clearVal.color.r = 0.08f;
            clearVal.color.g = 0.08f;
            clearVal.color.b = 0.12f;
            clearVal.color.a = 1.0f;
            RenderingAttachment colorAtt{};
            colorAtt.view = colorView;
            colorAtt.loadOp = AttachmentLoadOp::Clear;
            colorAtt.storeOp = AttachmentStoreOp::Store;
            colorAtt.clearValue = clearVal;
            RenderingDesc rd{
                .renderArea = Rect2D{.offset = {.x = 0, .y = 0}, .extent = {.width = w, .height = h}},
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
            if (useMeshPath) {
                cmd.CmdDrawMeshTasks(1, 1, 1);
            } else {
                cmd.CmdDraw(3, 1, 0, 0);
            }
            cmd.CmdEndRendering();

            // Transition: ColorAttachment -> Present
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
            if (fragShader.IsValid()) {
                d.DestroyShaderModule(fragShader);
            }
            if (vertShader.IsValid()) {
                d.DestroyShaderModule(vertShader);
            }
            if (meshShader.IsValid()) {
                d.DestroyShaderModule(meshShader);
            }
            return 0;
        });
    }
};

// ============================================================================
// Global state (emscripten needs static callbacks)
// ============================================================================

static WindowManager* g_wm = nullptr;
static SurfaceManager* g_sm = nullptr;
static TriangleRenderer* g_renderer = nullptr;
static WindowHandle g_mainWindow;
static bool g_shouldQuit = false;

static void MainLoopIteration() {
    auto events = g_wm->PollEvents();
    for (auto& ev : events) {
        std::visit(
            [&](auto& e) {
                using enum ::miki::debug::LogCategory;
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, neko::platform::KeyDown>) {
                    MIKI_LOG_DEBUG(
                        Demo, "[Win{}] KeyDown: key={} mods={}", ev.window.id, static_cast<int>(e.key),
                        static_cast<int>(e.mods)
                    );
                    if (e.key == neko::platform::Key::Escape) {
                        g_shouldQuit = true;
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::KeyUp>) {
                    MIKI_LOG_DEBUG(Demo, "[Win{}] KeyUp: key={}", ev.window.id, static_cast<int>(e.key));
                } else if constexpr (std::is_same_v<T, neko::platform::MouseButton>) {
                    MIKI_LOG_DEBUG(
                        Demo, "[Win{}] MouseButton: btn={} action={}", ev.window.id, static_cast<int>(e.button),
                        static_cast<int>(e.action)
                    );
                } else if constexpr (std::is_same_v<T, neko::platform::Scroll>) {
                    MIKI_LOG_DEBUG(Demo, "[Win{}] Scroll: dx={:.2f} dy={:.2f}", ev.window.id, e.dx, e.dy);
                } else if constexpr (std::is_same_v<T, neko::platform::Resize>) {
                    MIKI_LOG_INFO(Demo, "[Win{}] Resize: {}x{}", ev.window.id, e.width, e.height);
                    (void)g_sm->ResizeSurface(g_mainWindow, e.width, e.height);
                } else if constexpr (std::is_same_v<T, neko::platform::Focus>) {
                    MIKI_LOG_DEBUG(Demo, "[Win{}] Focus: {}", ev.window.id, e.focused ? "gained" : "lost");
                } else if constexpr (std::is_same_v<T, neko::platform::CloseRequested>) {
                    MIKI_LOG_INFO(Demo, "[Win{}] CloseRequested", ev.window.id);
                    g_shouldQuit = true;
                } else if constexpr (std::is_same_v<T, neko::platform::TextInput>) {
                    MIKI_LOG_DEBUG(
                        Demo, "[Win{}] TextInput: U+{:04X}", ev.window.id, static_cast<unsigned>(e.codepoint)
                    );
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

    // BeginFrame: waits on in-flight fence, acquires swapchain image
    auto frameResult = g_sm->BeginFrame(g_mainWindow);
    if (!frameResult) {
        return;
    }
    auto& frame = *frameResult;

    // Record rendering commands (acquires per-frame cmd buffer from pool)
    auto cmdBuf = g_renderer->RecordFrame(frame.swapchainImageView, frame.width, frame.height);
    if (!cmdBuf) {
        return;
    }

    // EndFrame: submits command buffer with correct sync, then presents
    (void)g_sm->EndFrame(g_mainWindow, *cmdBuf);
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char** argv) {
    // Initialize logger
    auto& logger = miki::debug::StructuredLogger::Instance();
    logger.AddSink(miki::debug::ConsoleSink{});
    logger.StartDrainThread();

    // Parse CLI
    auto backend = ParseBackendFromArgs(argc, argv);

    std::println("[demo] Starting RHI Triangle with {} backend", BackendName(backend));

    // 1. WindowManager
    auto glfwBackend = std::make_unique<GlfwWindowBackend>(backend, /*visible=*/true);
    auto* backendPtr = glfwBackend.get();
    auto wmResult = WindowManager::Create(std::move(glfwBackend));
    if (!wmResult) {
        std::println("[demo] WindowManager::Create failed");
        return 1;
    }
    auto wm = WindowManager(std::move(*wmResult));
    g_wm = &wm;

    constexpr uint32_t kW = 1280, kH = 720;
    auto winResult = wm.CreateWindow({.title = "miki RHI Triangle", .width = kW, .height = kH});
    if (!winResult) {
        std::println("[demo] CreateWindow failed");
        return 1;
    }
    g_mainWindow = *winResult;

    auto nativeHandle = wm.GetNativeHandle(g_mainWindow);

    // 2. Device (one call replaces 80 lines of per-backend switch-case)
    DeviceDesc desc{
        .backend = backend,
        .enableValidation = true,
        .enableGpuCapture = false,
        .requiredExtensions = {},
        .windowBackend = backendPtr,
        .nativeToken = wm.GetNativeToken(g_mainWindow),
    };
    auto deviceResult = CreateDevice(desc);
    if (!deviceResult) {
        std::println("[demo] Device creation failed for {}", BackendName(backend));
        return 1;
    }
    auto device = std::move(*deviceResult);
    std::println("[demo] Device created successfully");

    // 3. SurfaceManager (replaces manual swapchain + semaphore management)
    auto smResult = SurfaceManager::Create(device.GetHandle());
    if (!smResult) {
        std::println("[demo] SurfaceManager::Create failed");
        return 1;
    }
    auto sm = std::move(*smResult);
    g_sm = &sm;

    auto attachResult = sm.AttachSurface(g_mainWindow, nativeHandle, {.presentMode = PresentMode::Fifo});
    if (!attachResult) {
        std::println("[demo] AttachSurface failed");
        return 1;
    }

    // 4. Compile shaders — use mesh shader path when device supports it
    bool useMesh = device.GetHandle().Dispatch([](auto& d) { return d.GetCapabilities().HasMeshShader(); });
    if (useMesh) {
        std::println("[demo] Mesh shader supported — using mesh shader pipeline (no vertex shader)");
    }
    auto shaders = CompileShaders(backend, useMesh);
    if (!shaders) {
        std::println("[demo] Shader compilation failed");
        return 1;
    }
    if (useMesh) {
        std::println(
            "[demo] Shaders compiled: MS={} bytes, FS={} bytes", shaders->ms.data.size(), shaders->fs.data.size()
        );
    } else {
        std::println(
            "[demo] Shaders compiled: VS={} bytes, FS={} bytes", shaders->vs.data.size(), shaders->fs.data.size()
        );
    }

    // 5. Initialize renderer (only pipeline resources + command list)
    TriangleRenderer renderer;
    g_renderer = &renderer;
    if (!renderer.Init(device.GetHandle(), *shaders, &sm, g_mainWindow)) {
        std::println("[demo] Renderer init failed");
        return 1;
    }
    std::println("[demo] {} triangle running. ESC to quit.", BackendName(backend));

    // 5a. Live resize callback — renders during Win32 modal drag resize
#ifndef __EMSCRIPTEN__
    backendPtr->SetLiveResizeCallback([](WindowHandle w, uint32_t width, uint32_t height) {
        (void)g_sm->ResizeSurface(w, width, height);
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
    });
#endif

    // 6. Main loop
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!g_shouldQuit) {
        MainLoopIteration();
    }
    renderer.Cleanup();
    // sm, device, wm destroyed in reverse order by RAII

    // Shutdown logger
    logger.Shutdown();
#endif
    return 0;
}
