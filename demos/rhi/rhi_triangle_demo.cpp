/** @file rhi_triangle_demo.cpp
 *  @brief RHI triangle demo — renders a color triangle on ALL backends.
 *
 *  Usage: rhi_triangle_demo [--backend vulkan|d3d12|opengl|webgpu]
 *  Default: Vulkan14 (desktop), WebGPU (Emscripten).
 *
 *  Uses WindowManager + GlfwWindowBackend for window management.
 *  Uses SlangCompiler for cross-backend shader compilation.
 *  Uses DeviceHandle + CommandListHandle for type-erased rendering.
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
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Pipeline.h"
#include "miki/rhi/Shader.h"
#include "miki/rhi/Sync.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/shader/SlangCompiler.h"
#include "miki/shader/ShaderTypes.h"

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
#include <memory>
#include <print>
#include <span>
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
// Shader compilation (Slang → per-backend bytecode)
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
// Device creation (type-erased via DeviceHandle)
// ============================================================================

struct DeviceHolder {
    DeviceHandle handle;

    ~DeviceHolder() {
        if (handle.IsValid()) {
            (void)handle.Dispatch([](auto& dev) {
                dev.WaitIdle();
                return 0;
            });
            handle.Destroy();
        }
    }

#if MIKI_BUILD_VULKAN
    std::unique_ptr<VulkanDevice> vulkan_;
#endif
#if MIKI_BUILD_D3D12
    std::unique_ptr<D3D12Device> d3d12_;
#endif
#if MIKI_BUILD_OPENGL
    std::unique_ptr<OpenGLDevice> opengl_;
#endif
#if MIKI_BUILD_WEBGPU
    std::unique_ptr<WebGPUDevice> webgpu_;
#endif
};

static auto CreateDevice(BackendType backend, [[maybe_unused]] IWindowBackend* wb, [[maybe_unused]] void* nativeToken)
    -> std::unique_ptr<DeviceHolder> {
    auto holder = std::make_unique<DeviceHolder>();
    switch (backend) {
#if MIKI_BUILD_VULKAN
        case BackendType::Vulkan14: {
            holder->vulkan_ = std::make_unique<VulkanDevice>();
            VulkanDeviceDesc dd{.enableValidation = true};
            if (auto r = holder->vulkan_->Init(dd); !r) {
                return nullptr;
            }
            holder->handle = DeviceHandle(holder->vulkan_.get(), BackendType::Vulkan14);
            break;
        }
#endif
#if MIKI_BUILD_D3D12
        case BackendType::D3D12: {
            holder->d3d12_ = std::make_unique<D3D12Device>();
            D3D12DeviceDesc dd{.enableValidation = true};
            if (auto r = holder->d3d12_->Init(dd); !r) {
                return nullptr;
            }
            holder->handle = DeviceHandle(holder->d3d12_.get(), BackendType::D3D12);
            break;
        }
#endif
#if MIKI_BUILD_OPENGL
        case BackendType::OpenGL43: {
            holder->opengl_ = std::make_unique<OpenGLDevice>();
            OpenGLDeviceDesc dd{.enableValidation = true, .windowBackend = wb, .nativeToken = nativeToken};
            if (auto r = holder->opengl_->Init(dd); !r) {
                return nullptr;
            }
            holder->handle = DeviceHandle(holder->opengl_.get(), BackendType::OpenGL43);
            break;
        }
#endif
#if MIKI_BUILD_WEBGPU
        case BackendType::WebGPU: {
            holder->webgpu_ = std::make_unique<WebGPUDevice>();
            WebGPUDeviceDesc dd{.enableValidation = true};
            if (auto r = holder->webgpu_->Init(dd); !r) {
                return nullptr;
            }
            holder->handle = DeviceHandle(holder->webgpu_.get(), BackendType::WebGPU);
            break;
        }
#endif
        default: return nullptr;
    }
    return holder;
}

// ============================================================================
// Triangle renderer — type-erased via DeviceHandle + CommandListHandle
// ============================================================================

struct TriangleRenderer {
    DeviceHandle device;
    SwapchainHandle swapchain;
    ShaderModuleHandle vertShader;
    ShaderModuleHandle fragShader;
    PipelineLayoutHandle pipelineLayout;
    PipelineHandle pipeline;
    CommandBufferHandle cmdBuf;
    CommandListHandle cmdList;
    SemaphoreHandle acquireSem;
    uint32_t width = 1280;
    uint32_t height = 720;

    auto Init(DeviceHolder& holder, const NativeWindowHandle& surface, CompiledShaders& shaders, uint32_t w, uint32_t h)
        -> bool {
        device = holder.handle;
        width = w;
        height = h;

        // Helper: Dispatch with explicit return type to avoid multi-backend type mismatch
        auto createSwapchain = [&](const SwapchainDesc& sc) -> RhiResult<SwapchainHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<SwapchainHandle> { return d.CreateSwapchain(sc); });
        };
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
        auto createCmdBuf = [&](const CommandBufferDesc& cbd) -> RhiResult<CommandBufferHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<CommandBufferHandle> {
                return d.CreateCommandBuffer(cbd);
            });
        };
        auto createSem = [&](const SemaphoreDesc& sd) -> RhiResult<SemaphoreHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<SemaphoreHandle> { return d.CreateSemaphore(sd); });
        };

        // Swapchain
        SwapchainDesc sc{};
        sc.surface = surface;
        sc.width = w;
        sc.height = h;
        sc.preferredFormat = Format::BGRA8_UNORM;
        sc.presentMode = PresentMode::Fifo;
        sc.debugName = "TriangleSwapchain";
        auto scr = createSwapchain(sc);
        if (!scr) {
            std::println("[demo] CreateSwapchain failed");
            return false;
        }
        swapchain = *scr;

        // Shaders
        auto vsCode = std::span<const uint8_t>(shaders.vs.data);
        auto fsCode = std::span<const uint8_t>(shaders.fs.data);
        ShaderModuleDesc vd{
            .stage = miki::rhi::ShaderStage::Vertex, .code = vsCode, .entryPoint = shaders.vs.entryPoint.c_str()
        };
        auto vs = createShader(vd);
        if (!vs) {
            std::println("[demo] VS create failed");
            return false;
        }
        vertShader = *vs;

        ShaderModuleDesc fd{
            .stage = miki::rhi::ShaderStage::Fragment, .code = fsCode, .entryPoint = shaders.fs.entryPoint.c_str()
        };
        auto fs = createShader(fd);
        if (!fs) {
            std::println("[demo] FS create failed");
            return false;
        }
        fragShader = *fs;

        // Pipeline layout (empty)
        PipelineLayoutDesc pld{};
        auto pl = createPipelineLayout(pld);
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

        // Command buffer
        CommandBufferDesc cbd{.type = QueueType::Graphics};
        auto cb = createCmdBuf(cbd);
        if (!cb) {
            std::println("[demo] CommandBuffer failed");
            return false;
        }
        cmdBuf = *cb;

        // Initialize concrete command buffer per backend using DeviceHolder's owned pointers
        auto bt = device.GetBackendType();
        switch (bt) {
#if MIKI_BUILD_VULKAN
            case BackendType::Vulkan14: {
                static VulkanCommandBuffer cmd;
                auto* dev = holder.vulkan_.get();
                auto* data = dev->GetCommandBufferPool().Lookup(cmdBuf);
                cmd.Init(dev, data->buffer, data->pool, data->queueType);
                cmdList = CommandListHandle(&cmd, bt);
                break;
            }
#endif
#if MIKI_BUILD_D3D12
            case BackendType::D3D12: {
                static D3D12CommandBuffer cmd;
                auto* dev = holder.d3d12_.get();
                auto* data = dev->GetCommandBufferPool().Lookup(cmdBuf);
                cmd.Init(dev, data->list.Get(), data->allocator.Get(), data->queueType);
                cmdList = CommandListHandle(&cmd, bt);
                break;
            }
#endif
#if MIKI_BUILD_OPENGL
            case BackendType::OpenGL43: {
                static OpenGLCommandBuffer cmd;
                cmd.Init(holder.opengl_.get());
                cmdList = CommandListHandle(&cmd, bt);
                break;
            }
#endif
#if MIKI_BUILD_WEBGPU
            case BackendType::WebGPU: {
                static WebGPUCommandBuffer cmd;
                cmd.Init(holder.webgpu_.get(), cmdBuf);
                cmdList = CommandListHandle(&cmd, bt);
                break;
            }
#endif
            default: std::println("[demo] Unsupported backend"); return false;
        }

        // Semaphore
        SemaphoreDesc sd{.type = SemaphoreType::Binary};
        auto sem = createSem(sd);
        if (!sem) {
            std::println("[demo] Semaphore failed");
            return false;
        }
        acquireSem = *sem;

        return true;
    }

    void Resize(uint32_t w, uint32_t h) {
        if (w == 0 || h == 0) {
            return;
        }
        width = w;
        height = h;
        (void)device.Dispatch([&](auto& d) {
            (void)d.ResizeSwapchain(swapchain, w, h);
            return 0;
        });
    }

    void RenderFrame() {
        auto imgRes = device.Dispatch([&](auto& d) -> RhiResult<uint32_t> {
            return d.AcquireNextImage(swapchain, acquireSem);
        });
        if (!imgRes) {
            return;
        }

        auto colorTex
            = device.Dispatch([&](auto& d) -> TextureHandle { return d.GetSwapchainTexture(swapchain, *imgRes); });
        auto tvRes = device.Dispatch([&](auto& d) -> RhiResult<TextureViewHandle> {
            TextureViewDesc tvd{.texture = colorTex};
            return d.CreateTextureView(tvd);
        });
        if (!tvRes) {
            return;
        }
        TextureViewHandle colorView = *tvRes;

        (void)cmdList.Dispatch([&](auto& cmd) {
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
                .renderArea = Rect2D{.offset = {0, 0}, .extent = {width, height}},
                .colorAttachments = {&colorAtt, 1},
            };

            cmd.CmdBeginRendering(rd);
            cmd.CmdBindPipeline(pipeline);
            cmd.CmdSetViewport(
                {.x = 0,
                 .y = 0,
                 .width = static_cast<float>(width),
                 .height = static_cast<float>(height),
                 .minDepth = 0,
                 .maxDepth = 1}
            );
            cmd.CmdSetScissor({.offset = {0, 0}, .extent = {width, height}});
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

        SemaphoreSubmitInfo waitInfo{.semaphore = acquireSem, .stageMask = PipelineStage::ColorAttachmentOutput};
        SubmitDesc submitDesc{};
        submitDesc.commandBuffers = {&cmdBuf, 1};
        submitDesc.waitSemaphores = {&waitInfo, 1};
        (void)device.Dispatch([&](auto& d) {
            d.Submit(QueueType::Graphics, submitDesc);
            return 0;
        });
        (void)device.Dispatch([&](auto& d) {
            d.Present(swapchain, {});
            return 0;
        });
        (void)device.Dispatch([&](auto& d) {
            d.DestroyTextureView(colorView);
            return 0;
        });
    }

    void Cleanup() {
        (void)device.Dispatch([&](auto& d) {
            d.WaitIdle();
            if (acquireSem.IsValid()) {
                d.DestroySemaphore(acquireSem);
            }
            if (cmdBuf.IsValid()) {
                d.DestroyCommandBuffer(cmdBuf);
            }
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
            if (swapchain.IsValid()) {
                d.DestroySwapchain(swapchain);
            }
            return 0;
        });
    }
};

// ============================================================================
// Global state (emscripten needs static callbacks)
// ============================================================================

static std::unique_ptr<WindowManager> g_wm;
static std::unique_ptr<DeviceHolder> g_device;
static TriangleRenderer g_renderer;
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
                    g_renderer.Resize(e.width, e.height);
                }
            },
            ev.event
        );
    }

    if (g_shouldQuit) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
        g_renderer.Cleanup();
#endif
        return;
    }

    g_renderer.RenderFrame();
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

    // Create WindowManager with GLFW backend
    auto glfwBackend = std::make_unique<GlfwWindowBackend>(backend, /*visible=*/true);
    auto* backendPtr = glfwBackend.get();
    auto wmResult = WindowManager::Create(std::move(glfwBackend));
    if (!wmResult) {
        std::println("[demo] WindowManager::Create failed");
        return 1;
    }
    g_wm = std::make_unique<WindowManager>(std::move(*wmResult));

    // Create window
    constexpr uint32_t kW = 1280, kH = 720;
    auto winResult = g_wm->CreateWindow({.title = "miki RHI Triangle", .width = kW, .height = kH});
    if (!winResult) {
        std::println("[demo] CreateWindow failed");
        return 1;
    }
    g_mainWindow = *winResult;

    // Get native handle for swapchain
    auto nativeHandle = g_wm->GetNativeHandle(g_mainWindow);
    auto* nativeToken = g_wm->GetNativeToken(g_mainWindow);

    // Create device
    g_device = CreateDevice(backend, backendPtr, nativeToken);
    if (!g_device || !g_device->handle.IsValid()) {
        std::println("[demo] Device creation failed for {}", BackendName(backend));
        return 1;
    }
    std::println("[demo] Device created successfully");

    // Compile embedded shaders via Slang
    auto shaders = CompileShaders(backend);
    if (!shaders) {
        std::println("[demo] Shader compilation failed");
        return 1;
    }
    std::println("[demo] Shaders compiled: VS={} bytes, FS={} bytes", shaders->vs.data.size(), shaders->fs.data.size());

    // Initialize renderer
    if (!g_renderer.Init(*g_device, nativeHandle, *shaders, kW, kH)) {
        std::println("[demo] Renderer init failed");
        return 1;
    }
    std::println("[demo] {} triangle running. ESC to quit.", BackendName(backend));

    // Main loop
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!g_shouldQuit) {
        MainLoopIteration();
    }
    g_renderer.Cleanup();
    g_device.reset();
    g_wm.reset();
#endif
    return 0;
}
