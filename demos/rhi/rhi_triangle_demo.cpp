/** @file rhi_triangle_demo.cpp
 *  @brief RHI triangle demo — renders a color triangle on WebGPU or OpenGL backend.
 *
 *  Usage: rhi_triangle_demo [--backend webgpu|opengl]
 *  On Emscripten: always WebGPU, main loop via emscripten_set_main_loop.
 *
 *  The triangle is defined entirely in the vertex shader (no vertex buffer needed).
 *  Shaders are passed as raw byte spans to ShaderModuleDesc.code.
 *
 *  Command recording uses the concrete CRTP command buffer type directly —
 *  no type-erased CommandListHandle needed for a single-backend demo.
 */

#include "miki/rhi/backend/AllBackends.h"
#if MIKI_BUILD_WEBGPU
#    include "miki/rhi/backend/WebGPUCommandBuffer.h"
#endif
#if MIKI_BUILD_OPENGL
#    include "miki/rhi/backend/OpenGLCommandBuffer.h"
#endif
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Pipeline.h"
#include "miki/rhi/Shader.h"
#include "miki/rhi/Sync.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#    define GLFW_EXPOSE_NATIVE_WIN32
#    include <GLFW/glfw3native.h>
#    ifdef CreateSemaphore
#        undef CreateSemaphore  // Win32 macro conflicts with DeviceBase::CreateSemaphore
#    endif
#elif defined(__APPLE__)
#    define GLFW_EXPOSE_NATIVE_COCOA
#    include <GLFW/glfw3native.h>
#elif defined(__linux__)
#    define GLFW_EXPOSE_NATIVE_X11
#    include <GLFW/glfw3native.h>
#endif

#if defined(__EMSCRIPTEN__)
#    include <emscripten/emscripten.h>
#endif

#include <cstdint>
#include <memory>
#include <print>
#include <span>
#include <string_view>

// ============================================================================
// Shader source strings (passed as byte spans to ShaderModuleDesc.code)
// ============================================================================

static constexpr std::string_view kWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
    var pos = array<vec2f, 3>(vec2f(0.0, 0.6), vec2f(-0.6, -0.6), vec2f(0.6, -0.6));
    return vec4f(pos[vi], 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(0.4, 0.7, 1.0, 1.0); }
)";

#if MIKI_BUILD_OPENGL
static constexpr std::string_view kGLSL_Vert = R"(
#version 430 core
const vec2 kPos[3] = vec2[3](vec2(0.0,0.6), vec2(-0.6,-0.6), vec2(0.6,-0.6));
void main() { gl_Position = vec4(kPos[gl_VertexID], 0.0, 1.0); }
)";

static constexpr std::string_view kGLSL_Frag = R"(
#version 430 core
out vec4 oColor;
void main() { oColor = vec4(0.4, 0.7, 1.0, 1.0); }
)";
#endif

// ============================================================================
// Helpers
// ============================================================================

static auto ToBytes(std::string_view sv) -> std::span<const uint8_t> {
    return {reinterpret_cast<const uint8_t*>(sv.data()), sv.size()};
}

static auto ParseBackend(std::string_view s) -> miki::rhi::BackendType {
    if (s == "opengl") {
        return miki::rhi::BackendType::OpenGL43;
    }
    return miki::rhi::BackendType::WebGPU;
}

static auto MakeNativeHandle(GLFWwindow* w) -> miki::rhi::NativeWindowHandle {
#if defined(__EMSCRIPTEN__)
    (void)w;
    return miki::rhi::EmscriptenCanvas{.selector = "#canvas"};
#elif defined(_WIN32)
    return miki::rhi::Win32Window{.hwnd = glfwGetWin32Window(w), .hinstance = GetModuleHandle(nullptr)};
#elif defined(__APPLE__)
    return miki::rhi::CocoaWindow{.nsWindow = glfwGetCocoaWindow(w)};
#elif defined(__linux__)
    return miki::rhi::X11Window{
        .display = glfwGetX11Display(),
        .window = static_cast<uint64_t>(glfwGetX11Window(w)),
    };
#else
    (void)w;
    return miki::rhi::Win32Window{};
#endif
}

// ============================================================================
// Per-backend demo runner (templated on concrete Dev + CmdBuf types)
// ============================================================================

template <typename Dev, typename CmdBuf>
struct TriangleDemo {
    Dev* dev = nullptr;
    GLFWwindow* window = nullptr;

    miki::rhi::SwapchainHandle swapchain;
    miki::rhi::ShaderModuleHandle vertShader;
    miki::rhi::ShaderModuleHandle fragShader;
    miki::rhi::PipelineLayoutHandle pipelineLayout;
    miki::rhi::PipelineHandle pipeline;
    miki::rhi::CommandBufferHandle cmdHandle;
    miki::rhi::SemaphoreHandle acquireSem;
    CmdBuf cmd;

    uint32_t width = 1280;
    uint32_t height = 720;
    miki::rhi::BackendType backendType = miki::rhi::BackendType::WebGPU;

    auto Init(
        std::string_view vertSrc, std::string_view fragSrc, const char* vertEntry, const char* fragEntry,
        const miki::rhi::NativeWindowHandle& nativeHandle
    ) -> bool {
        using namespace miki::rhi;

        // Swapchain
        SwapchainDesc sc{};
        sc.surface = nativeHandle;
        sc.width = width;
        sc.height = height;
        sc.preferredFormat = Format::BGRA8_UNORM;
        sc.presentMode = PresentMode::Fifo;
        sc.debugName = "TriangleSwapchain";
        auto scr = dev->CreateSwapchain(sc);
        if (!scr) {
            std::println("[demo] CreateSwapchain failed: {}", static_cast<int>(scr.error()));
            return false;
        }
        swapchain = *scr;

        // Shaders — code passed as byte span of source text
        ShaderModuleDesc vd{.stage = ShaderStage::Vertex, .code = ToBytes(vertSrc), .entryPoint = vertEntry};
        auto vs = dev->CreateShaderModule(vd);
        if (!vs) {
            std::println("[demo] VS failed");
            return false;
        }
        vertShader = *vs;

        ShaderModuleDesc fd{.stage = ShaderStage::Fragment, .code = ToBytes(fragSrc), .entryPoint = fragEntry};
        auto fs = dev->CreateShaderModule(fd);
        if (!fs) {
            std::println("[demo] FS failed");
            return false;
        }
        fragShader = *fs;

        // Pipeline layout (no user descriptors)
        PipelineLayoutDesc pld{};
        auto pl = dev->CreatePipelineLayout(pld);
        if (!pl) {
            std::println("[demo] PipelineLayout failed");
            return false;
        }
        pipelineLayout = *pl;

        // Graphics pipeline
        ColorAttachmentBlend blend{};  // defaults: blendEnable=false, writeMask=All
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
        auto pso = dev->CreateGraphicsPipeline(gpd);
        if (!pso) {
            std::println("[demo] PSO failed");
            return false;
        }
        pipeline = *pso;

        // Command buffer + concrete CRTP object
        CommandBufferDesc cbd{.type = QueueType::Graphics};
        auto cb = dev->CreateCommandBuffer(cbd);
        if (!cb) {
            std::println("[demo] CommandBuffer failed");
            return false;
        }
        cmdHandle = *cb;
        if constexpr (requires { cmd.Init(dev, cmdHandle); }) {
            cmd.Init(dev, cmdHandle);
        } else {
            cmd.Init(dev);
        }

        // Semaphore (binary, for acquire sync)
        SemaphoreDesc sd{.type = SemaphoreType::Binary};
        auto sem = dev->CreateSemaphore(sd);
        if (!sem) {
            std::println("[demo] Semaphore failed");
            return false;
        }
        acquireSem = *sem;

        return true;
    }

    void Resize(uint32_t w, uint32_t h) {
        width = w;
        height = h;
        if (swapchain.IsValid()) {
            (void)dev->ResizeSwapchain(swapchain, w, h);
        }
    }

    void RenderFrame() {
        using namespace miki::rhi;

        auto imgRes = dev->AcquireNextImage(swapchain, acquireSem);
        if (!imgRes) {
            return;
        }

        TextureHandle colorTex = dev->GetSwapchainTexture(swapchain, *imgRes);

        // Need a TextureView for RenderingAttachment
        TextureViewDesc tvd{};
        tvd.texture = colorTex;
        auto tvRes = dev->CreateTextureView(tvd);
        if (!tvRes) {
            return;
        }
        TextureViewHandle colorView = *tvRes;

        cmd.Begin();

        // Transition: Undefined → ColorAttachment
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

        // Transition: ColorAttachment → PresentSrc
        TextureBarrierDesc toPresent{};
        toPresent.srcStage = PipelineStage::ColorAttachmentOutput;
        toPresent.dstStage = PipelineStage::BottomOfPipe;
        toPresent.srcAccess = AccessFlags::ColorAttachmentWrite;
        toPresent.dstAccess = AccessFlags::None;
        toPresent.oldLayout = TextureLayout::ColorAttachment;
        toPresent.newLayout = TextureLayout::Present;
        cmd.CmdTextureBarrier(colorTex, toPresent);

        cmd.End();

        SemaphoreSubmitInfo waitInfo{};
        waitInfo.semaphore = acquireSem;
        waitInfo.stageMask = PipelineStage::ColorAttachmentOutput;
        SubmitDesc submitDesc{};
        submitDesc.commandBuffers = {&cmdHandle, 1};
        submitDesc.waitSemaphores = {&waitInfo, 1};
        dev->Submit(QueueType::Graphics, submitDesc);

        dev->Present(swapchain, {});
        dev->DestroyTextureView(colorView);
    }

    void Cleanup() {
        dev->WaitIdle();
        if (acquireSem.IsValid()) {
            dev->DestroySemaphore(acquireSem);
        }
        if (cmdHandle.IsValid()) {
            dev->DestroyCommandBuffer(cmdHandle);
        }
        if (pipeline.IsValid()) {
            dev->DestroyPipeline(pipeline);
        }
        if (pipelineLayout.IsValid()) {
            dev->DestroyPipelineLayout(pipelineLayout);
        }
        if (fragShader.IsValid()) {
            dev->DestroyShaderModule(fragShader);
        }
        if (vertShader.IsValid()) {
            dev->DestroyShaderModule(vertShader);
        }
        if (swapchain.IsValid()) {
            dev->DestroySwapchain(swapchain);
        }
    }
};

// ============================================================================
// Global state (kept minimal — only what emscripten_set_main_loop needs)
// ============================================================================

static GLFWwindow* g_window = nullptr;
static void (*g_tick)() = nullptr;
static void (*g_cleanup)() = nullptr;

static void MainLoopIteration() {
    glfwPollEvents();
    if (glfwWindowShouldClose(g_window)) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
        if (g_cleanup) {
            g_cleanup();
        }
#endif
        return;
    }
    if (g_tick) {
        g_tick();
    }
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char** argv) {
    // Parse CLI
    miki::rhi::BackendType backend = miki::rhi::BackendType::WebGPU;
#if defined(__EMSCRIPTEN__)
    backend = miki::rhi::BackendType::WebGPU;
#else
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--backend" && i + 1 < argc) {
            backend = ParseBackend(argv[++i]);
        }
    }
#endif

    // GLFW
    if (!glfwInit()) {
        std::println("[demo] glfwInit failed");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    constexpr uint32_t kW = 1280, kH = 720;
    g_window = glfwCreateWindow(static_cast<int>(kW), static_cast<int>(kH), "miki RHI Triangle", nullptr, nullptr);
    if (!g_window) {
        glfwTerminate();
        return 1;
    }

    glfwSetKeyCallback(g_window, [](GLFWwindow* w, int k, int, int a, int) {
        if (k == GLFW_KEY_ESCAPE && a == GLFW_PRESS) {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
    });

    auto nativeHandle = MakeNativeHandle(g_window);

#if MIKI_BUILD_WEBGPU
    if (backend == miki::rhi::BackendType::WebGPU) {
        static miki::rhi::WebGPUDevice dev;
        static TriangleDemo<miki::rhi::WebGPUDevice, miki::rhi::WebGPUCommandBuffer> demo;

        miki::rhi::WebGPUDeviceDesc dd{.enableValidation = true};
        if (auto r = dev.Init(dd); !r) {
            std::println("[demo] WebGPU init failed");
            glfwTerminate();
            return 1;
        }

        demo.dev = &dev;
        demo.width = kW;
        demo.height = kH;
        demo.backendType = backend;

        if (!demo.Init(kWGSL, kWGSL, "vs_main", "fs_main", nativeHandle)) {
            glfwTerminate();
            return 1;
        }

        glfwSetFramebufferSizeCallback(g_window, [](GLFWwindow*, int w, int h) {
            if (w > 0 && h > 0) {
                demo.Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }
        });

        g_tick = []() { demo.RenderFrame(); };
        g_cleanup = []() { demo.Cleanup(); };
        std::println("[demo] WebGPU triangle running. ESC to quit.");
    }
#endif

#if MIKI_BUILD_OPENGL
    if (backend == miki::rhi::BackendType::OpenGL43) {
        static miki::rhi::OpenGLDevice dev;
        static TriangleDemo<miki::rhi::OpenGLDevice, miki::rhi::OpenGLCommandBuffer> demo;

        miki::rhi::OpenGLDeviceDesc dd{.enableValidation = true};
        if (auto r = dev.Init(dd); !r) {
            std::println("[demo] OpenGL init failed");
            glfwTerminate();
            return 1;
        }

        demo.dev = &dev;
        demo.width = kW;
        demo.height = kH;
        demo.backendType = backend;

        if (!demo.Init(kGLSL_Vert, kGLSL_Frag, "main", "main", nativeHandle)) {
            glfwTerminate();
            return 1;
        }

        glfwSetFramebufferSizeCallback(g_window, [](GLFWwindow*, int w, int h) {
            if (w > 0 && h > 0) {
                demo.Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }
        });

        g_tick = []() { demo.RenderFrame(); };
        g_cleanup = []() { demo.Cleanup(); };
        std::println("[demo] OpenGL triangle running. ESC to quit.");
    }
#endif

    if (!g_tick) {
        std::println("[demo] No backend available for {}.", static_cast<int>(backend));
        glfwTerminate();
        return 1;
    }

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!glfwWindowShouldClose(g_window)) {
        MainLoopIteration();
    }
    if (g_cleanup) {
        g_cleanup();
    }
    glfwDestroyWindow(g_window);
    glfwTerminate();
#endif
    return 0;
}
