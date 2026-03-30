/** @file rhi_triangle_demo.cpp
 *  @brief RHI triangle demo — renders a color triangle on ALL backends.
 *
 *  Usage: rhi_triangle_demo [--backend vulkan|d3d12|opengl|webgpu]
 *  Default: Vulkan14 (desktop), WebGPU (Emscripten).
 *
 *  Demonstrates the recommended application architecture:
 *    - DeviceFactory (OwnedDevice) for device creation
 *    - SurfaceManager for swapchain lifecycle and frame pacing
 *    - AcquireCommandList for type-erased command recording
 */

#include "miki/platform/WindowManager.h"
#include "miki/platform/glfw/GlfwWindowBackend.h"
#include "miki/rhi/backend/AllBackends.h"
#if MIKI_BUILD_VULKAN
#    include "miki/rhi/backend/VulkanCommandBuffer.h"
#endif
#if MIKI_BUILD_D3D12
#    include "miki/rhi/backend/D3D12CommandBuffer.h"
#endif
#if MIKI_BUILD_OPENGL
#    include "miki/rhi/backend/OpenGLCommandBuffer.h"
#endif
#if MIKI_BUILD_WEBGPU
#    include "miki/rhi/backend/WebGPUCommandBuffer.h"
#endif
#include "miki/rhi/backend/MockCommandBuffer.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/DeviceFactory.h"
#include "miki/rhi/Pipeline.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/Shader.h"
#include "miki/rhi/SurfaceManager.h"
#include "miki/shader/ShaderTypes.h"
#include "miki/shader/SlangCompiler.h"

// Win32 macro conflicts
#ifdef CreateWindow
#    undef CreateWindow
#endif
#ifdef CreateSemaphore
#    undef CreateSemaphore
#endif

#if defined(__EMSCRIPTEN__)
#    include <emscripten/emscripten.h>
#endif

#include <cstdint>
#include <print>
#include <string_view>

// Embed triangle shader source at compile time (C++23 #embed)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char kTriangleSlangSource[] = {
#embed "shaders/triangle.slang"
    , '\0'
};
#pragma clang diagnostic pop

using namespace miki::platform;
using namespace miki::rhi;
namespace shader = miki::shader;

// ============================================================================
// CLI parsing
// ============================================================================

static auto ParseBackend(std::string_view s) -> BackendType {
    if (s == "vulkan" || s == "vk") {
        return BackendType::Vulkan14;
    }
    if (s == "d3d12" || s == "dx12") {
        return BackendType::D3D12;
    }
    if (s == "opengl" || s == "gl") {
        return BackendType::OpenGL43;
    }
    if (s == "webgpu" || s == "wgpu") {
        return BackendType::WebGPU;
    }
    return BackendType::Vulkan14;
}

static auto BackendName(BackendType t) -> const char* {
    switch (t) {
        case BackendType::Vulkan14: return "Vulkan";
        case BackendType::D3D12: return "D3D12";
        case BackendType::OpenGL43: return "OpenGL";
        case BackendType::WebGPU: return "WebGPU";
        default: return "Unknown";
    }
}

// ============================================================================
// Shader compilation (Slang -> per-backend bytecode)
// ============================================================================

struct CompiledShaders {
    shader::ShaderBlob vs;
    shader::ShaderBlob fs;
};

static auto CompileShaders(BackendType backend) -> std::optional<CompiledShaders> {
    auto compilerResult = shader::SlangCompiler::Create();
    if (!compilerResult) {
        std::println("[demo] shader::SlangCompiler::Create failed");
        return std::nullopt;
    }
    auto compiler = std::move(*compilerResult);
    auto target = shader::ShaderTargetForBackend(backend);
    std::string src(kTriangleSlangSource);

    shader::ShaderCompileDesc vsDesc{
        .sourcePath = {},
        .sourceCode = src,
        .entryPoint = "vs_main",
        .stage = shader::ShaderStage::Vertex,
        .target = target,
        .permutation = {},
        .defines = {}
    };
    auto vsResult = compiler.Compile(vsDesc);
    if (!vsResult) {
        std::println("[demo] VS compilation failed for {}", BackendName(backend));
        for (auto& d : compiler.GetLastDiagnostics()) {
            std::println("  {}:{}:{}: {}", d.filePath, d.line, d.column, d.message);
        }
        return std::nullopt;
    }

    shader::ShaderCompileDesc fsDesc{
        .sourcePath = {},
        .sourceCode = src,
        .entryPoint = "fs_main",
        .stage = shader::ShaderStage::Fragment,
        .target = target,
        .permutation = {},
        .defines = {}
    };
    auto fsResult = compiler.Compile(fsDesc);
    if (!fsResult) {
        std::println("[demo] FS compilation failed for {}", BackendName(backend));
        for (auto& d : compiler.GetLastDiagnostics()) {
            std::println("  {}:{}:{}: {}", d.filePath, d.line, d.column, d.message);
        }
        return std::nullopt;
    }

    return CompiledShaders{.vs = std::move(*vsResult), .fs = std::move(*fsResult)};
}

// ============================================================================
// TriangleRenderer — owns only pipeline resources, records commands
// ============================================================================

struct TriangleRenderer {
    DeviceHandle device;
    ShaderModuleHandle vertShader;
    ShaderModuleHandle fragShader;
    PipelineLayoutHandle pipelineLayout;
    PipelineHandle pipeline;
    SurfaceManager* surfaceManager = nullptr;
    WindowHandle window{};

    auto Init(DeviceHandle dev, CompiledShaders& shaders, SurfaceManager* sm, WindowHandle win) -> bool {
        device = dev;
        surfaceManager = sm;
        window = win;

        // Helper lambdas — explicit return type to avoid multi-backend type mismatch
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

        // Shaders
        ShaderModuleDesc vd{
            .stage = ShaderStage::Vertex,
            .code = std::span<const uint8_t>(shaders.vs.data),
            .entryPoint = shaders.vs.entryPoint.c_str()
        };
        auto vs = createShader(vd);
        if (!vs) {
            std::println("[demo] VS create failed");
            return false;
        }
        vertShader = *vs;

        ShaderModuleDesc fd{
            .stage = ShaderStage::Fragment,
            .code = std::span<const uint8_t>(shaders.fs.data),
            .entryPoint = shaders.fs.entryPoint.c_str()
        };
        auto fs = createShader(fd);
        if (!fs) {
            std::println("[demo] FS create failed");
            return false;
        }
        fragShader = *fs;

        // Pipeline layout (empty)
        auto pl = createPipelineLayout(PipelineLayoutDesc{});
        if (!pl) {
            std::println("[demo] PipelineLayout failed");
            return false;
        }
        pipelineLayout = *pl;

        // Graphics pipeline
        ColorAttachmentBlend blend{};
        Format colorFmt = Format::BGRA8_UNORM;
        GraphicsPipelineDesc gpd{};
        gpd.vertexShader = vertShader;
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

    auto RecordFrame(TextureHandle colorTex, uint32_t w, uint32_t h) -> std::optional<CommandBufferHandle> {
        auto cmdAcqOpt = AcquireCommandList();
        if (!cmdAcqOpt) {
            return std::nullopt;
        }
        auto cmdAcq = *cmdAcqOpt;

        auto tvRes = device.Dispatch([&](auto& d) -> RhiResult<TextureViewHandle> {
            TextureViewDesc tvd{.texture = colorTex};
            return d.CreateTextureView(tvd);
        });
        if (!tvRes) {
            return std::nullopt;
        }
        TextureViewHandle colorView = *tvRes;

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
            cmd.CmdDraw(3, 1, 0, 0);
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

        (void)device.Dispatch([&](auto& d) {
            d.DestroyTextureView(colorView);
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
            [](auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, neko::platform::KeyDown>) {
                    if (e.key == neko::platform::Key::Escape) {
                        g_shouldQuit = true;
                    }
                } else if constexpr (std::is_same_v<T, neko::platform::CloseRequested>) {
                    g_shouldQuit = true;
                } else if constexpr (std::is_same_v<T, neko::platform::Resize>) {
                    (void)g_sm->ResizeSurface(g_mainWindow, e.width, e.height);
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
    auto cmdBuf = g_renderer->RecordFrame(frame.swapchainImage, frame.width, frame.height);
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
    // Parse CLI
    BackendType backend = BackendType::Vulkan14;
#if defined(__EMSCRIPTEN__)
    backend = BackendType::WebGPU;
#else
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--backend" && i + 1 < argc) {
            backend = ParseBackend(argv[++i]);
        }
    }
#endif

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

    // 4. Compile shaders
    auto shaders = CompileShaders(backend);
    if (!shaders) {
        std::println("[demo] Shader compilation failed");
        return 1;
    }
    std::println("[demo] Shaders compiled: VS={} bytes, FS={} bytes", shaders->vs.data.size(), shaders->fs.data.size());

    // 5. Initialize renderer (only pipeline resources + command list)
    TriangleRenderer renderer;
    g_renderer = &renderer;
    if (!renderer.Init(device.GetHandle(), *shaders, &sm, g_mainWindow)) {
        std::println("[demo] Renderer init failed");
        return 1;
    }
    std::println("[demo] {} triangle running. ESC to quit.", BackendName(backend));

    // 6. Main loop
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!g_shouldQuit) {
        MainLoopIteration();
    }
    renderer.Cleanup();
    // sm, device, wm destroyed in reverse order by RAII
#endif
    return 0;
}
