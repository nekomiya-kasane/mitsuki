/** @file rhi_streaming_upload_demo.cpp
 *  @brief GPU-driven streaming upload demo — demonstrates async CPU→GPU mesh upload pipeline.
 *
 *  Usage: rhi_streaming_upload_demo [--backend vulkan|vulkan11|d3d12|opengl|webgpu]
 *
 *  Architecture:
 *    - Background loader thread generates mesh data with random sleep (simulating I/O latency).
 *    - Main thread polls completed meshes, uploads via StagingRing, renders ready slots.
 *    - N mesh slots displayed in a grid; each slot transitions: Empty → Loading → Uploading → Ready.
 *    - StagingRing + FrameManager integration demonstrates real async transfer pipeline.
 *    - Press SPACE to request a new batch of meshes. ESC to quit.
 *
 *  Technical highlights:
 *    - StagingRing::Allocate + EnqueueBufferCopy for zero-intermediate-copy upload
 *    - FrameManager::SetStagingRing + FlushTransfers for async transfer queue dispatch
 *    - std::jthread + std::atomic for lock-free producer-consumer
 *    - Frame-paced chunk reclaim lifecycle
 */

#include "common/DemoCommon.h"
#include "common/DemoMeshGen.h"

#include "miki/resource/StagingRing.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <numbers>
#include <random>
#include <thread>
#include <vector>

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
namespace resource = miki::resource;

// ============================================================================
// Constants
// ============================================================================

static constexpr uint32_t kGridCols = 4;
static constexpr uint32_t kGridRows = 3;
static constexpr uint32_t kTotalSlots = kGridCols * kGridRows;
static constexpr uint32_t kMinSleepMs = 10;
static constexpr uint32_t kMaxSleepMs = 500;

// ============================================================================
// Push constants (matches torus.slang layout)
// ============================================================================

struct PushConst {
    float4x4 mvp;
    float4x4 model;
};
static_assert(sizeof(PushConst) == 128);

// ============================================================================
// Mesh slot — tracks lifecycle of one streaming mesh
// ============================================================================

enum class SlotState : uint8_t {
    Empty,
    Loading,
    DataReady,
    Uploaded
};

struct MeshSlot {
    std::atomic<SlotState> state{SlotState::Empty};
    DemoMeshData cpuData;
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    uint32_t indexCount = 0;
    uint32_t meshType = 0;
    std::chrono::steady_clock::time_point loadStartTime;
    float loadDurationMs = 0.0f;
};

// ============================================================================
// Background loader thread — simulates async I/O with random sleep
// ============================================================================

static std::array<MeshSlot, kTotalSlots> g_slots;
static std::jthread g_loaderThread;

static auto GenerateMeshByType(uint32_t type) -> DemoMeshData {
    switch (type % 7) {
        case 0: return GenerateTorus(0.4f, 0.15f, 32, 24);
        case 1: return GenerateCube(0.35f);
        case 2: return GenerateUVSphere(0.35f, 24, 16);
        case 3: return GenerateCylinder(0.2f, 0.6f, 24);
        case 4: return GenerateCone(0.25f, 0.6f, 24);
        case 5: return GenerateIcosphere(0.35f, 2);
        default: return GeneratePlane(0.6f, 0.6f, 4, 4);
    }
}

static void LoaderThreadFunc(std::stop_token stopToken) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> sleepDist(kMinSleepMs, kMaxSleepMs);

    while (!stopToken.stop_requested()) {
        bool anyWork = false;
        for (uint32_t i = 0; i < kTotalSlots; ++i) {
            if (stopToken.stop_requested()) {
                return;
            }

            auto expected = SlotState::Loading;
            if (!g_slots[i].state.compare_exchange_strong(expected, SlotState::Loading)) {
                continue;
            }

            anyWork = true;

            // Simulate I/O latency
            auto sleepMs = sleepDist(rng);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            if (stopToken.stop_requested()) {
                return;
            }

            // Generate mesh data on this thread
            g_slots[i].cpuData = GenerateMeshByType(g_slots[i].meshType);
            g_slots[i].loadDurationMs = static_cast<float>(sleepMs);

            // Signal: data ready for upload on main thread
            g_slots[i].state.store(SlotState::DataReady, std::memory_order_release);
        }

        if (!anyWork) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

// ============================================================================
// GPU resources
// ============================================================================

static WindowManager* g_wm = nullptr;
static SurfaceManager* g_sm = nullptr;
static DeviceHandle g_device;
static WindowHandle g_mainWindow;
static bool g_shouldQuit = false;
static float g_time = 0.0f;
static uint32_t g_batchCounter = 0;

// Pipeline
static PipelineHandle g_pipeline;
static PipelineLayoutHandle g_pipelineLayout;
static ShaderModuleHandle g_vertModule, g_fragModule;

// Staging
static resource::StagingRing* g_stagingRing = nullptr;

static auto InitPipeline(DeviceHandle dev, Format colorFmt, CompiledShaderPair& shaders) -> bool {
    auto createShader = [&](const ShaderModuleDesc& sd) -> RhiResult<ShaderModuleHandle> {
        return dev.Dispatch([&](auto& d) -> RhiResult<ShaderModuleHandle> { return d.CreateShaderModule(sd); });
    };

    ShaderModuleDesc vd{
        .stage = ShaderStage::Vertex, .code = shaders.vs.data, .entryPoint = shaders.vs.entryPoint.c_str()
    };
    auto vs = createShader(vd);
    if (!vs) {
        std::println("[stream] VS create failed");
        return false;
    }
    g_vertModule = *vs;

    ShaderModuleDesc fd{
        .stage = ShaderStage::Fragment, .code = shaders.fs.data, .entryPoint = shaders.fs.entryPoint.c_str()
    };
    auto fs = createShader(fd);
    if (!fs) {
        std::println("[stream] FS create failed");
        return false;
    }
    g_fragModule = *fs;

    PushConstantRange pcRange{
        .stages = ShaderStage::Vertex | ShaderStage::Fragment, .offset = 0, .size = sizeof(PushConst)
    };
    PipelineLayoutDesc pld{.pushConstants = {&pcRange, 1}};
    auto pl = dev.Dispatch([&](auto& d) -> RhiResult<PipelineLayoutHandle> { return d.CreatePipelineLayout(pld); });
    if (!pl) {
        return false;
    }
    g_pipelineLayout = *pl;

    VertexInputBinding binding{.binding = 0, .stride = sizeof(DemoVertex), .inputRate = VertexInputRate::PerVertex};
    std::array<VertexInputAttribute, 2> attrs = {{
        {.location = 0, .binding = 0, .format = Format::RGB32_FLOAT, .offset = offsetof(DemoVertex, px)},
        {.location = 1, .binding = 0, .format = Format::RGB32_FLOAT, .offset = offsetof(DemoVertex, nx)},
    }};
    VertexInputState vis{.bindings = {&binding, 1}, .attributes = {attrs.data(), attrs.size()}};

    ColorAttachmentBlend blend{};
    GraphicsPipelineDesc gpd{};
    gpd.vertexShader = g_vertModule;
    gpd.fragmentShader = g_fragModule;
    gpd.vertexInput = vis;
    gpd.topology = PrimitiveTopology::TriangleList;
    gpd.cullMode = CullMode::Back;
    gpd.frontFace = FrontFace::CounterClockwise;
    gpd.depthTestEnable = false;
    gpd.depthWriteEnable = false;
    gpd.colorBlends = {&blend, 1};
    gpd.colorFormats = {&colorFmt, 1};
    gpd.pipelineLayout = g_pipelineLayout;
    auto pso = dev.Dispatch([&](auto& d) -> RhiResult<PipelineHandle> { return d.CreateGraphicsPipeline(gpd); });
    if (!pso) {
        return false;
    }
    g_pipeline = *pso;
    return true;
}

// ============================================================================
// Request a new batch of meshes
// ============================================================================

static void RequestNewBatch() {
    std::println("[stream] Requesting batch #{} ({} meshes)...", g_batchCounter, kTotalSlots);
    for (uint32_t i = 0; i < kTotalSlots; ++i) {
        // Destroy old GPU buffers if any
        if (g_slots[i].vertexBuffer.IsValid()) {
            (void)g_device.Dispatch([&](auto& d) {
                d.DestroyBuffer(g_slots[i].vertexBuffer);
                d.DestroyBuffer(g_slots[i].indexBuffer);
                return 0;
            });
            g_slots[i].vertexBuffer = {};
            g_slots[i].indexBuffer = {};
        }
        g_slots[i].indexCount = 0;
        g_slots[i].cpuData = {};
        g_slots[i].meshType = g_batchCounter * kTotalSlots + i;
        g_slots[i].loadStartTime = std::chrono::steady_clock::now();
        g_slots[i].state.store(SlotState::Loading, std::memory_order_release);
    }
    ++g_batchCounter;
}

// ============================================================================
// Upload completed meshes via StagingRing
// ============================================================================

static void UploadReadyMeshes() {
    for (uint32_t i = 0; i < kTotalSlots; ++i) {
        if (g_slots[i].state.load(std::memory_order_acquire) != SlotState::DataReady) {
            continue;
        }

        auto& mesh = g_slots[i].cpuData;
        uint64_t vbSize = mesh.vertices.size() * sizeof(DemoVertex);
        uint64_t ibSize = mesh.indices.size() * sizeof(uint32_t);

        // Create GPU buffers
        auto createBuf = [&](const BufferDesc& bd) -> RhiResult<BufferHandle> {
            return g_device.Dispatch([&](auto& d) -> RhiResult<BufferHandle> { return d.CreateBuffer(bd); });
        };

        auto vbResult = createBuf(
            {.size = vbSize, .usage = BufferUsage::Vertex | BufferUsage::TransferDst, .memory = MemoryLocation::GpuOnly}
        );
        if (!vbResult) {
            std::println("[stream] VB create failed for slot {}", i);
            continue;
        }
        g_slots[i].vertexBuffer = *vbResult;

        auto ibResult = createBuf(
            {.size = ibSize, .usage = BufferUsage::Index | BufferUsage::TransferDst, .memory = MemoryLocation::GpuOnly}
        );
        if (!ibResult) {
            std::println("[stream] IB create failed for slot {}", i);
            continue;
        }
        g_slots[i].indexBuffer = *ibResult;

        // Upload via StagingRing: allocate staging memory, memcpy, enqueue copy
        auto vbAlloc = g_stagingRing->Allocate(vbSize);
        if (!vbAlloc) {
            std::println("[stream] Staging alloc failed for VB slot {}", i);
            continue;
        }
        std::memcpy(vbAlloc->mappedPtr, mesh.vertices.data(), vbSize);
        g_stagingRing->EnqueueBufferCopy(*vbAlloc, g_slots[i].vertexBuffer, 0);

        auto ibAlloc = g_stagingRing->Allocate(ibSize);
        if (!ibAlloc) {
            std::println("[stream] Staging alloc failed for IB slot {}", i);
            continue;
        }
        std::memcpy(ibAlloc->mappedPtr, mesh.indices.data(), ibSize);
        g_stagingRing->EnqueueBufferCopy(*ibAlloc, g_slots[i].indexBuffer, 0);

        g_slots[i].indexCount = static_cast<uint32_t>(mesh.indices.size());
        g_slots[i].cpuData = {};  // Free CPU-side data

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - g_slots[i].loadStartTime
        )
                           .count();
        std::println(
            "[stream] Slot {} uploaded: {} verts, {} tris, load={:.0f}ms, total={}ms", i,
            g_slots[i].indexCount / 3 * 3 / 3, g_slots[i].indexCount / 3, g_slots[i].loadDurationMs, elapsed
        );

        g_slots[i].state.store(SlotState::Uploaded, std::memory_order_release);
    }
}

// ============================================================================
// Rendering
// ============================================================================

static auto BuildSlotPushConst(uint32_t slotIdx, float aspect, float time) -> PushConst {
    uint32_t col = slotIdx % kGridCols;
    uint32_t row = slotIdx / kGridCols;

    // Map slot to [-1,1] grid positions with some spacing
    float cellW = 2.0f / static_cast<float>(kGridCols);
    float cellH = 2.0f / static_cast<float>(kGridRows);
    float cx = -1.0f + cellW * (static_cast<float>(col) + 0.5f);
    float cy = 1.0f - cellH * (static_cast<float>(row) + 0.5f);

    // Each slot gets a small model with unique rotation
    float angle = time * 0.5f + static_cast<float>(slotIdx) * 0.7f;
    auto model = Mul4x4(MakeRotateX(angle * 0.7f), MakeRotateY(angle));

    // Scale down to fit grid cell
    float scale = 0.8f / static_cast<float>(std::max(kGridCols, kGridRows));
    float4x4 scaleM = MakeIdentity();
    scaleM[0, 0] = scale;
    scaleM[1, 1] = scale;
    scaleM[2, 2] = scale;
    model = Mul4x4(scaleM, model);

    // Translate to grid position
    float4x4 translate = MakeIdentity();
    translate[0, 3] = cx;
    translate[1, 3] = cy;
    translate[2, 3] = 0.0f;
    model = Mul4x4(translate, model);

    // Simple orthographic-like perspective (far camera)
    auto view = MakeLookAt({0, 0, 3.0f}, {0, 0, 0}, {0, 1, 0});
    auto proj = MakePerspective(std::numbers::pi_v<float> / 4.0f, aspect, 0.1f, 100.0f);

    return {.mvp = Mul4x4(proj, Mul4x4(view, model)), .model = model};
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

    // Eagerly flush staging transfers (overlap with cmd recording if async transfer queue available)
    fm->FlushTransfers();

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
        clearVal.color = {0.04f, 0.04f, 0.06f, 1.0f};
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
        cmd.CmdBindPipeline(g_pipeline);
        cmd.CmdSetViewport(
            {.x = 0,
             .y = 0,
             .width = static_cast<float>(frame.width),
             .height = static_cast<float>(frame.height),
             .minDepth = 0,
             .maxDepth = 1}
        );
        cmd.CmdSetScissor({.offset = {0, 0}, .extent = {frame.width, frame.height}});

        // Draw all uploaded slots
        for (uint32_t i = 0; i < kTotalSlots; ++i) {
            if (g_slots[i].state.load(std::memory_order_acquire) != SlotState::Uploaded) {
                continue;
            }
            if (!g_slots[i].vertexBuffer.IsValid()) {
                continue;
            }

            auto pc = BuildSlotPushConst(i, aspect, g_time);
            cmd.CmdPushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(PushConst), &pc);
            cmd.CmdBindVertexBuffer(0, g_slots[i].vertexBuffer, 0);
            cmd.CmdBindIndexBuffer(g_slots[i].indexBuffer, 0, IndexType::Uint32);
            cmd.CmdDrawIndexed(g_slots[i].indexCount, 1, 0, 0, 0);
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
                        RequestNewBatch();
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

    // Poll loader thread results and upload via StagingRing
    UploadReadyMeshes();

    // Print streaming stats periodically
    static float statsTimer = 0.0f;
    statsTimer += dt;
    if (statsTimer > 2.0f) {
        statsTimer = 0.0f;
        uint32_t loaded = 0, loading = 0, empty = 0;
        for (uint32_t i = 0; i < kTotalSlots; ++i) {
            auto s = g_slots[i].state.load(std::memory_order_relaxed);
            if (s == SlotState::Uploaded) {
                ++loaded;
            } else if (s == SlotState::Loading || s == SlotState::DataReady) {
                ++loading;
            } else {
                ++empty;
            }
        }
        std::println(
            "[stream] Slots: {} ready, {} loading, {} empty | StagingRing: {} active chunks, {:.1f}% util, {} bytes",
            loaded, loading, empty, g_stagingRing->GetActiveChunkCount(), g_stagingRing->GetUtilization() * 100.0f,
            g_stagingRing->GetTotalAllocatedBytes()
        );
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
    std::println("[stream] Starting streaming upload demo with {} backend", BackendName(backend));
    std::println(
        "[stream] {} mesh slots in {}x{} grid. Press SPACE for new batch, ESC to quit.", kTotalSlots, kGridCols,
        kGridRows
    );

    // 1. WindowManager
    auto glfwBackend = std::make_unique<GlfwWindowBackend>(backend, true);
    auto* backendPtr = glfwBackend.get();
    auto wmResult = WindowManager::Create(std::move(glfwBackend));
    if (!wmResult) {
        std::println("[stream] WindowManager failed");
        return 1;
    }
    auto wm = std::move(*wmResult);
    g_wm = &wm;

    auto winResult = wm.CreateWindow({.title = "miki Streaming Upload Demo", .width = 1280, .height = 720});
    if (!winResult) {
        std::println("[stream] CreateWindow failed");
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
        std::println("[stream] Device creation failed");
        return 1;
    }
    auto device = std::move(*deviceResult);
    g_device = device.GetHandle();

    // 3. SurfaceManager
    auto smResult = SurfaceManager::Create(g_device, wm);
    if (!smResult) {
        std::println("[stream] SurfaceManager failed");
        return 1;
    }
    auto surfMgr = std::move(*smResult);
    g_sm = &surfMgr;

    auto attachResult = surfMgr.AttachSurface(g_mainWindow, {.presentMode = PresentMode::Fifo});
    if (!attachResult) {
        std::println("[stream] AttachSurface failed");
        return 1;
    }

    // 4. StagingRing — the core of this demo
    auto stagingResult = resource::StagingRing::Create(g_device, {.chunkSize = uint64_t{2} << 20, .maxChunks = 32});
    if (!stagingResult) {
        std::println("[stream] StagingRing creation failed");
        return 1;
    }
    auto stagingRing = std::move(*stagingResult);
    g_stagingRing = &stagingRing;

    // Wire StagingRing into FrameManager for automatic transfer dispatch
    auto* fm = surfMgr.GetFrameManager(g_mainWindow);
    if (fm) {
        fm->SetStagingRing(g_stagingRing);
        std::println(
            "[stream] StagingRing wired to FrameManager (async transfer: {})",
            g_device.Dispatch([](auto& d) { return d.GetCapabilities().hasAsyncTransfer; }) ? "yes" : "no"
        );
    }

    // 5. Compile shaders
    auto shaderTarget
        = g_device.Dispatch([](auto& d) { return miki::shader::PreferredShaderTarget(d.GetCapabilities()); });
    auto shaders = CompileShaderPair(kTorusSlangSource, shaderTarget, "torus");
    if (!shaders) {
        return 1;
    }

    // 6. Pipeline
    auto* rs = surfMgr.GetRenderSurface(g_mainWindow);
    Format colorFmt = rs ? rs->GetFormat() : Format::BGRA8_SRGB;
    if (!InitPipeline(g_device, colorFmt, *shaders)) {
        std::println("[stream] Pipeline init failed");
        return 1;
    }

    // 7. Start loader thread
    g_loaderThread = std::jthread(LoaderThreadFunc);

    // 8. Request initial batch
    RequestNewBatch();

    std::println("[stream] Running. Press SPACE for new batch, ESC to quit.");

    // 9. Live resize callback
#ifndef __EMSCRIPTEN__
    backendPtr->SetLiveResizeCallback([](WindowHandle w, uint32_t width, uint32_t height) {
        (void)g_sm->ResizeSurface(w, width, height);
        RenderFrame(w);
    });
#endif

    // 10. Main loop
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoopIteration, 0, true);
#else
    while (!g_shouldQuit) {
        MainLoopIteration();
    }

    // Cleanup
    g_loaderThread.request_stop();
    g_loaderThread.join();

    (void)g_device.Dispatch([](auto& d) {
        d.WaitIdle();
        return 0;
    });

    // Destroy slot GPU buffers
    for (uint32_t i = 0; i < kTotalSlots; ++i) {
        if (g_slots[i].vertexBuffer.IsValid()) {
            (void)g_device.Dispatch([&](auto& d) {
                d.DestroyBuffer(g_slots[i].vertexBuffer);
                d.DestroyBuffer(g_slots[i].indexBuffer);
                return 0;
            });
        }
    }

    // Destroy pipeline resources
    (void)g_device.Dispatch([&](auto& d) {
        if (g_pipeline.IsValid()) {
            d.DestroyPipeline(g_pipeline);
        }
        if (g_pipelineLayout.IsValid()) {
            d.DestroyPipelineLayout(g_pipelineLayout);
        }
        if (g_fragModule.IsValid()) {
            d.DestroyShaderModule(g_fragModule);
        }
        if (g_vertModule.IsValid()) {
            d.DestroyShaderModule(g_vertModule);
        }
        return 0;
    });

    // stagingRing, surfMgr, device, wm destroyed by RAII
    logger.Shutdown();
#endif
    return 0;
}
