/** @file rhi_pipeline_library_demo.cpp
 *  @brief Pipeline Library (split compilation) demo — measures PSO creation time savings.
 *
 *  Usage: rhi_pipeline_library_demo [--backend vulkan|d3d12]
 *
 *  Demonstrates:
 *    This demo creates 4 rendering configurations from combinatorial state:
 *      2 fragment shaders (orange material / blue material)  x
 *      2 fragment outputs (opaque blend / additive blend)
 *    sharing the same VertexInput and PreRasterization parts.
 *
 *    Without pipeline library: 4 full PSO compilations (each ~1-10ms).
 *    With pipeline library: 4 parts compiled once, then 4 near-instant links.
 *
 *    The demo measures and prints wall-clock creation times for both approaches.
 *    Each config renders a different mesh in a 2x2 grid to verify correctness.
 *
 *  Press SPACE to toggle between monolithic / linked pipelines. ESC to quit.
 */

#include "common/DemoCommon.h"
#include "common/DemoMeshGen.h"

#include <array>
#include <chrono>
#include <cstring>
#include <numbers>
#include <print>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char kSlangSource[] = {
#embed "shaders/pipeline_library.slang"
    , '\0'
};
#pragma clang diagnostic pop

using namespace miki::demo;
using namespace miki::platform;
using namespace miki::rhi;
using namespace miki::core;

// ============================================================================
// Push constant layout (must match shader)
// ============================================================================

struct PushConst {
    float4x4 mvp;
    float4x4 model;
    float4 materialColor;  // rgb = diffuse, a = specular power
};
static_assert(sizeof(PushConst) == 144);

// ============================================================================
// Config: 2 fragment shaders x 2 blend modes = 4 combinations
// ============================================================================

static constexpr uint32_t kNumConfigs = 4;

struct PipelineConfig {
    PipelineHandle monolithic;
    PipelineHandle linked;
    float4 color;
    bool additiveBlend;
    const char* label;
};

// ============================================================================
// Global state
// ============================================================================

static WindowManager* g_wm = nullptr;
static SurfaceManager* g_sm = nullptr;
static DeviceHandle g_device;
static WindowHandle g_mainWindow;
static bool g_shouldQuit = false;
static float g_time = 0.0f;
static bool g_useLinked = false;
static bool g_hasPipelineLibrary = false;

static PipelineLayoutHandle g_pipelineLayout;
static ShaderModuleHandle g_vertModule, g_fragModule;

// Mesh data
struct MeshGpu {
    BufferHandle vb;
    BufferHandle ib;
    uint32_t indexCount;
};
static std::array<MeshGpu, kNumConfigs> g_meshes;

// Pipeline configs
static std::array<PipelineConfig, kNumConfigs> g_configs;

// Pipeline library parts (only valid when g_hasPipelineLibrary)
static PipelineLibraryPartHandle g_partVertexInput;
static PipelineLibraryPartHandle g_partPreRast;
static PipelineLibraryPartHandle g_partFragShader;
static PipelineLibraryPartHandle g_partFragOutputOpaque;
static PipelineLibraryPartHandle g_partFragOutputBlend;

// Timing
static double g_monolithicTimeUs = 0.0;
static double g_linkedTimeUs = 0.0;
static double g_partCompileTimeUs = 0.0;

// ============================================================================
// Helpers
// ============================================================================

static auto UploadMesh(DeviceHandle dev, const DemoMeshData& mesh) -> MeshGpu {
    auto createBuf = [&](const BufferDesc& bd) -> BufferHandle {
        auto r = dev.Dispatch([&](auto& d) -> RhiResult<BufferHandle> { return d.CreateBuffer(bd); });
        return r ? *r : BufferHandle{};
    };

    uint64_t vbSize = mesh.vertices.size() * sizeof(DemoVertex);
    uint64_t ibSize = mesh.indices.size() * sizeof(uint32_t);

    auto vb = createBuf(
        {.size = vbSize, .usage = BufferUsage::Vertex | BufferUsage::TransferDst, .memory = MemoryLocation::CpuToGpu}
    );
    auto ib = createBuf(
        {.size = ibSize, .usage = BufferUsage::Index | BufferUsage::TransferDst, .memory = MemoryLocation::CpuToGpu}
    );

    // Map + copy
    auto mapAndCopy = [&](BufferHandle h, const void* src, uint64_t size) {
        auto ptr = dev.Dispatch([&](auto& d) -> RhiResult<void*> { return d.MapBuffer(h); });
        if (ptr) {
            std::memcpy(*ptr, src, size);
            dev.Dispatch([&](auto& d) {
                d.UnmapBuffer(h);
                return 0;
            });
        }
    };
    mapAndCopy(vb, mesh.vertices.data(), vbSize);
    mapAndCopy(ib, mesh.indices.data(), ibSize);

    return {vb, ib, static_cast<uint32_t>(mesh.indices.size())};
}

static auto NowUs() -> double {
    return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::high_resolution_clock::now().time_since_epoch()
    )
                                   .count());
}

// ============================================================================
// Create a single monolithic pipeline
// ============================================================================

static auto CreateMonolithicPipeline(Format colorFmt, bool additiveBlend) -> PipelineHandle {
    ColorAttachmentBlend blend{};
    if (additiveBlend) {
        blend.blendEnable = true;
        blend.srcColor = BlendFactor::One;
        blend.dstColor = BlendFactor::One;
        blend.colorOp = BlendOp::Add;
        blend.srcAlpha = BlendFactor::One;
        blend.dstAlpha = BlendFactor::One;
        blend.alphaOp = BlendOp::Add;
    }

    VertexInputBinding binding{.binding = 0, .stride = sizeof(DemoVertex), .inputRate = VertexInputRate::PerVertex};
    std::array<VertexInputAttribute, 2> attrs = {{
        {.location = 0, .binding = 0, .format = Format::RGB32_FLOAT, .offset = offsetof(DemoVertex, px)},
        {.location = 1, .binding = 0, .format = Format::RGB32_FLOAT, .offset = offsetof(DemoVertex, nx)},
    }};

    GraphicsPipelineDesc gpd{};
    gpd.vertexShader = g_vertModule;
    gpd.fragmentShader = g_fragModule;
    gpd.vertexInput = {.bindings = {&binding, 1}, .attributes = {attrs.data(), attrs.size()}};
    gpd.topology = PrimitiveTopology::TriangleList;
    gpd.cullMode = CullMode::Back;
    gpd.frontFace = FrontFace::CounterClockwise;
    gpd.depthTestEnable = false;
    gpd.depthWriteEnable = false;
    gpd.colorBlends = {&blend, 1};
    gpd.colorFormats = {&colorFmt, 1};
    gpd.pipelineLayout = g_pipelineLayout;

    auto r = g_device.Dispatch([&](auto& d) -> RhiResult<PipelineHandle> { return d.CreateGraphicsPipeline(gpd); });
    return r ? *r : PipelineHandle{};
}

// ============================================================================
// Create pipeline library parts + linked pipelines
// ============================================================================

static void CreateLibraryParts(Format colorFmt) {
    VertexInputBinding binding{.binding = 0, .stride = sizeof(DemoVertex), .inputRate = VertexInputRate::PerVertex};
    std::array<VertexInputAttribute, 2> attrs = {{
        {.location = 0, .binding = 0, .format = Format::RGB32_FLOAT, .offset = offsetof(DemoVertex, px)},
        {.location = 1, .binding = 0, .format = Format::RGB32_FLOAT, .offset = offsetof(DemoVertex, nx)},
    }};

    // Shared partial desc (fields used vary per part)
    GraphicsPipelineDesc partialDesc{};
    partialDesc.vertexShader = g_vertModule;
    partialDesc.fragmentShader = g_fragModule;
    partialDesc.vertexInput = {.bindings = {&binding, 1}, .attributes = {attrs.data(), attrs.size()}};
    partialDesc.topology = PrimitiveTopology::TriangleList;
    partialDesc.cullMode = CullMode::Back;
    partialDesc.frontFace = FrontFace::CounterClockwise;
    partialDesc.depthTestEnable = false;
    partialDesc.depthWriteEnable = false;
    partialDesc.pipelineLayout = g_pipelineLayout;

    ColorAttachmentBlend blendOff{};
    ColorAttachmentBlend blendOn{
        .blendEnable = true,
        .srcColor = BlendFactor::One,
        .dstColor = BlendFactor::One,
        .colorOp = BlendOp::Add,
        .srcAlpha = BlendFactor::One,
        .dstAlpha = BlendFactor::One,
        .alphaOp = BlendOp::Add
    };

    auto createPart = [&](PipelineLibraryPart part, GraphicsPipelineDesc desc) -> PipelineLibraryPartHandle {
        PipelineLibraryPartDesc pld{.part = part, .partialDesc = desc, .pipelineLayout = g_pipelineLayout};
        auto r = g_device.Dispatch([&](auto& d) -> RhiResult<PipelineLibraryPartHandle> {
            return d.CreatePipelineLibraryPart(pld);
        });
        return r ? *r : PipelineLibraryPartHandle{};
    };

    // Part 1: VertexInput
    g_partVertexInput = createPart(PipelineLibraryPart::VertexInput, partialDesc);

    // Part 2: PreRasterization (vertex shader + rasterizer)
    g_partPreRast = createPart(PipelineLibraryPart::PreRasterization, partialDesc);

    // Part 3: FragmentShader (fragment shader + depth/stencil)
    g_partFragShader = createPart(PipelineLibraryPart::FragmentShader, partialDesc);

    // Part 4a: FragmentOutput — opaque
    {
        auto desc = partialDesc;
        desc.colorBlends = {&blendOff, 1};
        desc.colorFormats = {&colorFmt, 1};
        desc.sampleCount = 1;
        g_partFragOutputOpaque = createPart(PipelineLibraryPart::FragmentOutput, desc);
    }

    // Part 4b: FragmentOutput — additive blend
    {
        auto desc = partialDesc;
        desc.colorBlends = {&blendOn, 1};
        desc.colorFormats = {&colorFmt, 1};
        desc.sampleCount = 1;
        g_partFragOutputBlend = createPart(PipelineLibraryPart::FragmentOutput, desc);
    }
}

static auto CreateLinkedPipeline(bool additiveBlend) -> PipelineHandle {
    LinkedPipelineDesc ld{};
    ld.vertexInput = g_partVertexInput;
    ld.preRasterization = g_partPreRast;
    ld.fragmentShader = g_partFragShader;
    ld.fragmentOutput = additiveBlend ? g_partFragOutputBlend : g_partFragOutputOpaque;

    auto r = g_device.Dispatch([&](auto& d) -> RhiResult<PipelineHandle> { return d.LinkGraphicsPipeline(ld); });
    return r ? *r : PipelineHandle{};
}

// ============================================================================
// Init pipelines and measure timing
// ============================================================================

static auto InitPipelines(Format colorFmt) -> bool {
    // Config table: color + blend mode
    g_configs[0] = {.color = {0.85f, 0.45f, 0.15f, 64.0f}, .additiveBlend = false, .label = "Orange Opaque"};
    g_configs[1] = {.color = {0.20f, 0.50f, 0.90f, 64.0f}, .additiveBlend = false, .label = "Blue Opaque"};
    g_configs[2] = {.color = {0.85f, 0.45f, 0.15f, 32.0f}, .additiveBlend = true, .label = "Orange Additive"};
    g_configs[3] = {.color = {0.20f, 0.50f, 0.90f, 32.0f}, .additiveBlend = true, .label = "Blue Additive"};

    // --- Measure monolithic creation ---
    double t0 = NowUs();
    for (auto& cfg : g_configs) {
        cfg.monolithic = CreateMonolithicPipeline(colorFmt, cfg.additiveBlend);
        if (!cfg.monolithic.IsValid()) {
            std::println("[plib] Monolithic pipeline creation failed for {}", cfg.label);
            return false;
        }
    }
    double t1 = NowUs();
    g_monolithicTimeUs = t1 - t0;

    // --- Measure pipeline library creation (parts + link) ---
    if (g_hasPipelineLibrary) {
        double t2 = NowUs();
        CreateLibraryParts(colorFmt);
        double t3 = NowUs();
        g_partCompileTimeUs = t3 - t2;

        double t4 = NowUs();
        for (auto& cfg : g_configs) {
            cfg.linked = CreateLinkedPipeline(cfg.additiveBlend);
            if (!cfg.linked.IsValid()) {
                std::println("[plib] Linked pipeline creation failed for {}", cfg.label);
                std::println("[plib] Pipeline library linking not supported — will use monolithic only");
                g_hasPipelineLibrary = false;
                break;
            }
        }
        double t5 = NowUs();
        g_linkedTimeUs = t5 - t4;

        if (g_hasPipelineLibrary) {
            std::println("┌──────────────────────────────────────────────────────┐");
            std::println("│         Pipeline Creation Timing Results             │");
            std::println("├──────────────────────────────────────────────────────┤");
            std::println("│ Monolithic (4 PSOs):   {:>10.1f} us                 │", g_monolithicTimeUs);
            std::println("│ Library parts (5):     {:>10.1f} us                 │", g_partCompileTimeUs);
            std::println("│ Link only (4 PSOs):    {:>10.1f} us                 │", g_linkedTimeUs);
            std::println(
                "│ Link speedup vs mono:  {:>10.1f}x                   │",
                g_linkedTimeUs > 0 ? g_monolithicTimeUs / g_linkedTimeUs : 0.0
            );
            std::println(
                "│ Total library:         {:>10.1f} us                 │", g_partCompileTimeUs + g_linkedTimeUs
            );
            std::println("└──────────────────────────────────────────────────────┘");
            std::println("[plib] In a real engine with 100+ material combos, parts are compiled ONCE.");
            std::println("[plib] Only the link step repeats per combination — this is the key saving.");
        }
    }

    if (!g_hasPipelineLibrary) {
        std::println("[plib] Pipeline library not available. Monolithic only. ({:.1f} us)", g_monolithicTimeUs);
    }
    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static auto BuildSlotPushConst(uint32_t idx, float aspect, float time) -> PushConst {
    uint32_t col = idx % 2;
    uint32_t row = idx / 2;

    float cellW = 2.0f / 2.0f;
    float cellH = 2.0f / 2.0f;
    float cx = -1.0f + cellW * (static_cast<float>(col) + 0.5f);
    float cy = 1.0f - cellH * (static_cast<float>(row) + 0.5f);

    float angle = time * 0.5f + static_cast<float>(idx) * 1.57f;
    auto model = Mul4x4(MakeRotateX(angle * 0.7f), MakeRotateY(angle));

    float scale = 0.35f;
    float4x4 scaleM = MakeIdentity();
    scaleM[0, 0] = scale;
    scaleM[1, 1] = scale;
    scaleM[2, 2] = scale;
    model = Mul4x4(scaleM, model);

    float4x4 translate = MakeIdentity();
    translate[0, 3] = cx;
    translate[1, 3] = cy;
    model = Mul4x4(translate, model);

    auto view = MakeLookAt({0, 0, 3.0f}, {0, 0, 0}, {0, 1, 0});
    auto proj = MakePerspective(std::numbers::pi_v<float> / 4.0f, aspect, 0.1f, 100.0f);

    return {.mvp = Mul4x4(proj, Mul4x4(view, model)), .model = model, .materialColor = g_configs[idx].color};
}

static void RenderFrame(WindowHandle win) {
    auto* fm = g_sm->GetFrameManager(win);
    if (!fm) {
        return;
    }
    auto frameResult = fm->BeginFrame();
    if (!frameResult) {
        return;
    }
    auto& frame = *frameResult;

    float aspect = (frame.height > 0) ? static_cast<float>(frame.width) / static_cast<float>(frame.height) : 1.0f;
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
        clearVal.color = {0.03f, 0.03f, 0.05f, 1.0f};
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
        cmd.CmdSetViewport(
            {.x = 0,
             .y = 0,
             .width = static_cast<float>(frame.width),
             .height = static_cast<float>(frame.height),
             .minDepth = 0,
             .maxDepth = 1}
        );
        cmd.CmdSetScissor({.offset = {0, 0}, .extent = {frame.width, frame.height}});

        for (uint32_t i = 0; i < kNumConfigs; ++i) {
            auto pso = (g_useLinked && g_hasPipelineLibrary) ? g_configs[i].linked : g_configs[i].monolithic;
            if (!pso.IsValid()) {
                continue;
            }

            cmd.CmdBindPipeline(pso);
            auto pc = BuildSlotPushConst(i, aspect, g_time);
            cmd.CmdPushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(PushConst), &pc);
            cmd.CmdBindVertexBuffer(0, g_meshes[i].vb, 0);
            cmd.CmdBindIndexBuffer(g_meshes[i].ib, 0, IndexType::Uint32);
            cmd.CmdDrawIndexed(g_meshes[i].indexCount, 1, 0, 0, 0);
        }

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

    miki::frame::FrameManager::SubmitBatch batch{.commandBuffers = std::span(&cmdAcq.bufferHandle, 1)};
    (void)fm->EndFrame(std::span<const miki::frame::FrameManager::SubmitBatch>{&batch, 1});
}

// ============================================================================
// Main loop
// ============================================================================

static void MainLoopIteration() {
    static auto lastTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;
    g_time += dt;

    auto events = g_wm->PollEvents();
    for (auto& ev : events) {
        std::visit(
            [&](auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, neko::platform::KeyDown>) {
                    if (e.key == neko::platform::Key::Escape) {
                        g_shouldQuit = true;
                    }
                    if (e.key == neko::platform::Key::Space) {
                        if (g_hasPipelineLibrary) {
                            g_useLinked = !g_useLinked;
                            std::println("[plib] Switched to {} pipelines", g_useLinked ? "LINKED" : "MONOLITHIC");
                        } else {
                            std::println("[plib] Pipeline library not available on this backend");
                        }
                    }
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
#endif
        return;
    }

    RenderFrame(g_mainWindow);
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char** argv) {
    auto& logger = miki::debug::StructuredLogger::Instance();
    logger.AddSink(miki::debug::ConsoleSink{});
    logger.StartDrainThread();

    auto backend = ParseBackendFromArgs(argc, argv);
    std::println("[plib] Pipeline Library demo — backend: {}", BackendName(backend));
    std::println("[plib] Press SPACE to toggle monolithic/linked, ESC to quit.");

    // 1. WindowManager
    auto glfwBackend = std::make_unique<GlfwWindowBackend>(backend, true);
    auto* backendPtr = glfwBackend.get();
    auto wmResult = WindowManager::Create(std::move(glfwBackend));
    if (!wmResult) {
        std::println("[plib] WindowManager failed");
        return 1;
    }
    auto wm = std::move(*wmResult);
    g_wm = &wm;

    auto winResult = wm.CreateWindow({.title = "miki Pipeline Library Demo", .width = 960, .height = 960});
    if (!winResult) {
        std::println("[plib] CreateWindow failed");
        return 1;
    }
    g_mainWindow = *winResult;

    // 2. Device
    DeviceDesc desc{
        .backend = backend,
        .enableValidation = true,
        .windowBackend = backendPtr,
        .nativeToken = wm.GetNativeToken(g_mainWindow)
    };
    auto deviceResult = CreateDevice(desc);
    if (!deviceResult) {
        std::println("[plib] Device creation failed");
        return 1;
    }
    auto device = std::move(*deviceResult);
    g_device = device.GetHandle();

    // Check pipeline library support
    g_hasPipelineLibrary = g_device.Dispatch([](auto& d) { return d.GetCapabilities().hasGraphicsPipelineLibrary; });
    std::println("[plib] Graphics pipeline library supported: {}", g_hasPipelineLibrary ? "YES" : "NO");

    // 3. SurfaceManager
    auto smResult = SurfaceManager::Create(g_device, wm);
    if (!smResult) {
        std::println("[plib] SurfaceManager failed");
        return 1;
    }
    auto surfMgr = std::move(*smResult);
    g_sm = &surfMgr;

    auto attachResult = surfMgr.AttachSurface(g_mainWindow, {.presentMode = PresentMode::Fifo});
    if (!attachResult) {
        std::println("[plib] AttachSurface failed");
        return 1;
    }

    // 4. Compile shaders
    auto shaderTarget
        = g_device.Dispatch([](auto& d) { return miki::shader::PreferredShaderTarget(d.GetCapabilities()); });
    auto compilerResult = shader::SlangCompiler::Create();
    if (!compilerResult) {
        std::println("[plib] SlangCompiler::Create failed");
        return 1;
    }
    auto compiler = std::move(*compilerResult);
    std::string source(kSlangSource);

    auto vs = CompileStage(compiler, source, "vs_main", shader::ShaderStage::Vertex, shaderTarget, "plib_vs");
    auto fs = CompileStage(compiler, source, "fs_main", shader::ShaderStage::Fragment, shaderTarget, "plib_fs");
    if (!vs || !fs) {
        return 1;
    }

    auto createShader = [&](const ShaderModuleDesc& sd) -> ShaderModuleHandle {
        auto r = g_device.Dispatch([&](auto& d) -> RhiResult<ShaderModuleHandle> { return d.CreateShaderModule(sd); });
        return r ? *r : ShaderModuleHandle{};
    };
    g_vertModule = createShader({.stage = ShaderStage::Vertex, .code = vs->data, .entryPoint = vs->entryPoint.c_str()});
    g_fragModule
        = createShader({.stage = ShaderStage::Fragment, .code = fs->data, .entryPoint = fs->entryPoint.c_str()});
    if (!g_vertModule.IsValid() || !g_fragModule.IsValid()) {
        std::println("[plib] Shader module creation failed");
        return 1;
    }

    // 5. Pipeline layout
    PushConstantRange pcRange{
        .stages = ShaderStage::Vertex | ShaderStage::Fragment, .offset = 0, .size = sizeof(PushConst)
    };
    PipelineLayoutDesc pld{.pushConstants = {&pcRange, 1}};
    auto plResult
        = g_device.Dispatch([&](auto& d) -> RhiResult<PipelineLayoutHandle> { return d.CreatePipelineLayout(pld); });
    if (!plResult) {
        std::println("[plib] PipelineLayout failed");
        return 1;
    }
    g_pipelineLayout = *plResult;

    // 6. Upload meshes (torus, cube, sphere, cylinder — one per config)
    g_meshes[0] = UploadMesh(g_device, GenerateTorus(0.4f, 0.15f, 32, 24));
    g_meshes[1] = UploadMesh(g_device, GenerateCube(0.35f));
    g_meshes[2] = UploadMesh(g_device, GenerateUVSphere(0.35f, 24, 16));
    g_meshes[3] = UploadMesh(g_device, GenerateCylinder(0.2f, 0.6f, 24));

    // 7. Create pipelines and measure timing
    auto* rs = surfMgr.GetRenderSurface(g_mainWindow);
    Format colorFmt = rs ? rs->GetFormat() : Format::BGRA8_SRGB;
    if (!InitPipelines(colorFmt)) {
        std::println("[plib] Pipeline init failed");
        return 1;
    }

    // 8. Live resize
#ifndef __EMSCRIPTEN__
    backendPtr->SetLiveResizeCallback([](WindowHandle w, uint32_t width, uint32_t height) {
        (void)g_sm->ResizeSurface(w, width, height);
        RenderFrame(w);
    });
#endif

    // 9. Main loop
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!g_shouldQuit) {
        MainLoopIteration();
    }

    // Cleanup
    (void)g_device.Dispatch([](auto& d) {
        d.WaitIdle();
        return 0;
    });

    // Destroy pipelines
    (void)g_device.Dispatch([&](auto& d) {
        for (auto& cfg : g_configs) {
            if (cfg.monolithic.IsValid()) {
                d.DestroyPipeline(cfg.monolithic);
            }
            if (cfg.linked.IsValid()) {
                d.DestroyPipeline(cfg.linked);
            }
        }
        // Destroy library parts
        if (g_partVertexInput.IsValid()) {
            d.DestroyPipelineLibraryPart(g_partVertexInput);
        }
        if (g_partPreRast.IsValid()) {
            d.DestroyPipelineLibraryPart(g_partPreRast);
        }
        if (g_partFragShader.IsValid()) {
            d.DestroyPipelineLibraryPart(g_partFragShader);
        }
        if (g_partFragOutputOpaque.IsValid()) {
            d.DestroyPipelineLibraryPart(g_partFragOutputOpaque);
        }
        if (g_partFragOutputBlend.IsValid()) {
            d.DestroyPipelineLibraryPart(g_partFragOutputBlend);
        }
        // Destroy shared resources
        if (g_pipelineLayout.IsValid()) {
            d.DestroyPipelineLayout(g_pipelineLayout);
        }
        if (g_fragModule.IsValid()) {
            d.DestroyShaderModule(g_fragModule);
        }
        if (g_vertModule.IsValid()) {
            d.DestroyShaderModule(g_vertModule);
        }
        for (auto& m : g_meshes) {
            if (m.vb.IsValid()) {
                d.DestroyBuffer(m.vb);
            }
            if (m.ib.IsValid()) {
                d.DestroyBuffer(m.ib);
            }
        }
        return 0;
    });

    logger.Shutdown();
#endif
    return 0;
}
