/** @file rg_mesh_torus_cube_demo.cpp
 *  @brief RenderGraph demo — torus + cube rendered via Task + Mesh shaders.
 *
 *  Usage: rg_mesh_torus_cube_demo [--backend vulkan|d3d12]
 *  Default: VulkanCompat (desktop). Requires mesh shader support.
 *
 *  Demonstrates the mesh shader pipeline through RenderGraph:
 *    1. CPU meshlet generation from parametric geometry
 *    2. Storage buffer upload (vertices, indices, meshlet descriptors)
 *    3. Descriptor set + pipeline layout for SSBO bindings
 *    4. Task shader amplification → Mesh shader vertex emission → Fragment Blinn-Phong
 *    5. RenderGraphBuilder::AddMeshShaderPass for graph integration
 *
 *  Scene:
 *    - Pass "DrawTorus": task+mesh shader renders torus meshlets
 *    - Pass "DrawCube":  task+mesh shader renders cube meshlets (additive)
 *    - Pass "Present":   transitions backbuffer to present
 *
 *  Camera: arcball (mouse drag rotate, scroll zoom). ESC to quit.
 */

#include "../rhi/common/DemoCommon.h"
#include "../rhi/common/DemoMeshGen.h"

#include "miki/rendergraph/RenderGraphAdvanced.h"
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
static constexpr char kMeshShaderSlangSource[] = {
#embed "shaders/blinn_phong_mesh.slang"
    , '\0'
};
#pragma clang diagnostic pop

using namespace miki::demo;
using namespace miki::platform;
using namespace miki::rhi;
using namespace miki::core;
using namespace miki::rg;

// ============================================================================
// GPU-side struct layouts (must match blinn_phong_mesh.slang)
// ============================================================================

struct GpuVertex {
    float px, py, pz;
    float nx, ny, nz;
};
static_assert(sizeof(GpuVertex) == 24);

struct GpuMeshlet {
    uint32_t vertexOffset;
    uint32_t triangleCount;
    uint32_t vertexCount;
    uint32_t _pad0;
};
static_assert(sizeof(GpuMeshlet) == 16);

struct MeshPushConst {
    float4x4 mvp;
    float4x4 model;
    float diffuseR, diffuseG, diffuseB;
    uint32_t meshletCount;
};
static_assert(sizeof(MeshPushConst) == 144);

// ============================================================================
// Meshlet generation from DemoMeshData
// ============================================================================

static constexpr uint32_t kMaxMeshletVertices = 64;
static constexpr uint32_t kMaxMeshletTriangles = 124;

struct MeshletBuildResult {
    std::vector<GpuVertex> vertices;
    std::vector<uint32_t> indexBuffer;  // packed: [uniqueVertexIndices...][triangleLocalIndices...]
    std::vector<GpuMeshlet> meshlets;
};

/** @brief Build meshlets from a DemoMeshData.
 *
 *  Simple linear meshlet builder: walks triangles in order, packing up to
 *  kMaxMeshletVertices unique vertices and kMaxMeshletTriangles triangles per meshlet.
 *  The index buffer layout per meshlet is:
 *    [vertexCount unique global vertex indices] [triangleCount * 3 local triangle indices]
 */
static auto BuildMeshlets(const DemoMeshData& mesh) -> MeshletBuildResult {
    MeshletBuildResult result;

    // Copy vertices
    result.vertices.reserve(mesh.vertices.size());
    for (auto& v : mesh.vertices) {
        result.vertices.push_back({v.px, v.py, v.pz, v.nx, v.ny, v.nz});
    }

    uint32_t triCount = static_cast<uint32_t>(mesh.indices.size()) / 3;
    uint32_t triIndex = 0;

    while (triIndex < triCount) {
        // Build one meshlet
        std::vector<uint32_t> uniqueVerts;
        std::vector<uint32_t> localTriIndices;
        uniqueVerts.reserve(kMaxMeshletVertices);

        auto findOrAddVertex = [&](uint32_t globalIdx) -> uint32_t {
            for (uint32_t i = 0; i < static_cast<uint32_t>(uniqueVerts.size()); ++i) {
                if (uniqueVerts[i] == globalIdx) {
                    return i;
                }
            }
            if (uniqueVerts.size() >= kMaxMeshletVertices) {
                return UINT32_MAX;
            }
            uint32_t local = static_cast<uint32_t>(uniqueVerts.size());
            uniqueVerts.push_back(globalIdx);
            return local;
        };

        while (triIndex < triCount && localTriIndices.size() / 3 < kMaxMeshletTriangles) {
            uint32_t i0 = mesh.indices[triIndex * 3 + 0];
            uint32_t i1 = mesh.indices[triIndex * 3 + 1];
            uint32_t i2 = mesh.indices[triIndex * 3 + 2];

            // Speculatively try to add all 3 vertices
            auto savedSize = uniqueVerts.size();
            uint32_t l0 = findOrAddVertex(i0);
            uint32_t l1 = findOrAddVertex(i1);
            uint32_t l2 = findOrAddVertex(i2);

            if (l0 == UINT32_MAX || l1 == UINT32_MAX || l2 == UINT32_MAX) {
                // Rollback and break — start new meshlet
                uniqueVerts.resize(savedSize);
                break;
            }

            localTriIndices.push_back(l0);
            localTriIndices.push_back(l1);
            localTriIndices.push_back(l2);
            ++triIndex;
        }

        // Emit meshlet
        GpuMeshlet m{};
        m.vertexOffset = static_cast<uint32_t>(result.indexBuffer.size());
        m.vertexCount = static_cast<uint32_t>(uniqueVerts.size());
        m.triangleCount = static_cast<uint32_t>(localTriIndices.size()) / 3;

        // Pack: unique vertex indices first, then local triangle indices
        for (auto idx : uniqueVerts) {
            result.indexBuffer.push_back(idx);
        }
        for (auto idx : localTriIndices) {
            result.indexBuffer.push_back(idx);
        }

        result.meshlets.push_back(m);
    }

    return result;
}

// ============================================================================
// GPU resource bundle for one mesh
// ============================================================================

struct MeshGpuData {
    BufferHandle vertexBuffer;   // Storage: GpuVertex[]
    BufferHandle indexBuffer;    // Storage: uint32_t[]
    BufferHandle meshletBuffer;  // Storage: GpuMeshlet[]
    uint32_t meshletCount = 0;
};

// ============================================================================
// Scene resources
// ============================================================================

struct SceneResources {
    DeviceHandle device;
    ShaderModuleHandle taskModule, meshModule, fragModule;
    DescriptorLayoutHandle descLayout;
    PipelineLayoutHandle pipelineLayout;
    PipelineHandle pipeline;
    MeshGpuData torus;
    MeshGpuData cube;
    DescriptorSetHandle torusDescSet, cubeDescSet;
    SurfaceManager* sm = nullptr;
    WindowHandle window{};

    // Camera
    float rotX = 0.3f, rotY = 0.0f;
    float distance = 5.0f;
    bool dragging = false;

    auto UploadStorageBuffer(const void* data, uint64_t size, const char* label) -> std::optional<BufferHandle> {
        auto buf = device.Dispatch([&](auto& d) -> RhiResult<BufferHandle> {
            return d.CreateBuffer(
                {.size = size, .usage = BufferUsage::Storage, .memory = MemoryLocation::CpuToGpu, .debugName = label}
            );
        });
        if (!buf) {
            return std::nullopt;
        }

        auto ptr = device.Dispatch([&](auto& d) -> RhiResult<void*> { return d.MapBuffer(*buf); });
        if (!ptr) {
            return std::nullopt;
        }
        std::memcpy(*ptr, data, size);
        (void)device.Dispatch([&](auto& d) {
            d.UnmapBuffer(*buf);
            return 0;
        });
        return *buf;
    }

    auto UploadMesh(const DemoMeshData& mesh, const char* label) -> std::optional<MeshGpuData> {
        auto built = BuildMeshlets(mesh);
        MeshGpuData gpu;
        gpu.meshletCount = static_cast<uint32_t>(built.meshlets.size());

        auto vb = UploadStorageBuffer(built.vertices.data(), built.vertices.size() * sizeof(GpuVertex), label);
        if (!vb) {
            return std::nullopt;
        }
        gpu.vertexBuffer = *vb;

        auto ib = UploadStorageBuffer(built.indexBuffer.data(), built.indexBuffer.size() * sizeof(uint32_t), label);
        if (!ib) {
            return std::nullopt;
        }
        gpu.indexBuffer = *ib;

        auto mb = UploadStorageBuffer(built.meshlets.data(), built.meshlets.size() * sizeof(GpuMeshlet), label);
        if (!mb) {
            return std::nullopt;
        }
        gpu.meshletBuffer = *mb;

        return gpu;
    }

    auto CreateMeshDescriptorSet(const MeshGpuData& mesh) -> std::optional<DescriptorSetHandle> {
        std::array<DescriptorWrite, 3> writes = {{
            {.binding = 0, .resource = BufferBinding{.buffer = mesh.vertexBuffer}},
            {.binding = 1, .resource = BufferBinding{.buffer = mesh.indexBuffer}},
            {.binding = 2, .resource = BufferBinding{.buffer = mesh.meshletBuffer}},
        }};
        DescriptorSetDesc dsDesc{.layout = descLayout, .writes = writes};
        auto ds
            = device.Dispatch([&](auto& d) -> RhiResult<DescriptorSetHandle> { return d.CreateDescriptorSet(dsDesc); });
        if (!ds) {
            return std::nullopt;
        }
        return *ds;
    }

    struct CompiledMeshShaders {
        shader::ShaderBlob task;
        shader::ShaderBlob mesh;
        shader::ShaderBlob frag;
    };

    auto Init(DeviceHandle dev, CompiledMeshShaders& shaders, SurfaceManager* surfMgr, WindowHandle win) -> bool {
        device = dev;
        sm = surfMgr;
        window = win;

        auto createShader = [&](const ShaderModuleDesc& sd) -> RhiResult<ShaderModuleHandle> {
            return device.Dispatch([&](auto& d) -> RhiResult<ShaderModuleHandle> { return d.CreateShaderModule(sd); });
        };

        // Task shader module
        {
            ShaderModuleDesc td{
                .stage = ShaderStage::Task, .code = shaders.task.data, .entryPoint = shaders.task.entryPoint.c_str()
            };
            auto ts = createShader(td);
            if (!ts) {
                std::println("[mesh_demo] Task shader create failed");
                return false;
            }
            taskModule = *ts;
        }

        // Mesh shader module
        {
            ShaderModuleDesc md{
                .stage = ShaderStage::Mesh, .code = shaders.mesh.data, .entryPoint = shaders.mesh.entryPoint.c_str()
            };
            auto ms = createShader(md);
            if (!ms) {
                std::println("[mesh_demo] Mesh shader create failed");
                return false;
            }
            meshModule = *ms;
        }

        // Fragment shader module
        {
            ShaderModuleDesc fd{
                .stage = ShaderStage::Fragment, .code = shaders.frag.data, .entryPoint = shaders.frag.entryPoint.c_str()
            };
            auto fs = createShader(fd);
            if (!fs) {
                std::println("[mesh_demo] Fragment shader create failed");
                return false;
            }
            fragModule = *fs;
        }

        // Descriptor layout: set 0 with 3 storage buffer bindings
        std::array<BindingDesc, 3> bindings = {{
            {.binding = 0, .type = BindingType::StorageBuffer, .stages = ShaderStage::Mesh, .count = 1},
            {.binding = 1, .type = BindingType::StorageBuffer, .stages = ShaderStage::Mesh, .count = 1},
            {.binding = 2, .type = BindingType::StorageBuffer, .stages = ShaderStage::Mesh, .count = 1},
        }};
        DescriptorLayoutDesc dld{.bindings = bindings};
        auto dl = device.Dispatch([&](auto& d) -> RhiResult<DescriptorLayoutHandle> {
            return d.CreateDescriptorLayout(dld);
        });
        if (!dl) {
            std::println("[mesh_demo] DescriptorLayout failed");
            return false;
        }
        descLayout = *dl;

        // Pipeline layout: 1 descriptor set + push constants
        PushConstantRange pcRange{
            .stages = ShaderStage::Task | ShaderStage::Mesh | ShaderStage::Fragment,
            .offset = 0,
            .size = sizeof(MeshPushConst)
        };
        PipelineLayoutDesc pld{.setLayouts = std::span{&descLayout, 1}, .pushConstants = {&pcRange, 1}};
        auto pl
            = device.Dispatch([&](auto& d) -> RhiResult<PipelineLayoutHandle> { return d.CreatePipelineLayout(pld); });
        if (!pl) {
            std::println("[mesh_demo] PipelineLayout failed");
            return false;
        }
        pipelineLayout = *pl;

        // Graphics pipeline (mesh shader path)
        ColorAttachmentBlend blend{};
        auto* rs = sm->GetRenderSurface(window);
        Format colorFmt = rs ? rs->GetFormat() : Format::BGRA8_SRGB;
        GraphicsPipelineDesc gpd{};
        gpd.taskShader = taskModule;
        gpd.meshShader = meshModule;
        gpd.fragmentShader = fragModule;
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
            std::println("[mesh_demo] PSO failed");
            return false;
        }
        pipeline = *pso;

        // Upload meshes
        auto torusMesh = GenerateTorus(0.7f, 0.3f, 48, 32);
        auto torusGpu = UploadMesh(torusMesh, "TorusMeshlet");
        if (!torusGpu) {
            std::println("[mesh_demo] Torus upload failed");
            return false;
        }
        torus = *torusGpu;

        auto cubeMesh = GenerateCube(0.5f);
        auto cubeGpu = UploadMesh(cubeMesh, "CubeMeshlet");
        if (!cubeGpu) {
            std::println("[mesh_demo] Cube upload failed");
            return false;
        }
        cube = *cubeGpu;

        // Create descriptor sets
        auto torusDS = CreateMeshDescriptorSet(torus);
        if (!torusDS) {
            std::println("[mesh_demo] Torus descriptor set failed");
            return false;
        }
        torusDescSet = *torusDS;

        auto cubeDS = CreateMeshDescriptorSet(cube);
        if (!cubeDS) {
            std::println("[mesh_demo] Cube descriptor set failed");
            return false;
        }
        cubeDescSet = *cubeDS;

        std::println("[mesh_demo] Torus: {} meshlets, Cube: {} meshlets", torus.meshletCount, cube.meshletCount);
        return true;
    }

    void Cleanup() {
        (void)device.Dispatch([&](auto& d) {
            d.WaitIdle();
            auto destroy = [&](auto h) {
                if (h.IsValid()) {
                    d.DestroyBuffer(h);
                }
            };
            if (pipeline.IsValid()) {
                d.DestroyPipeline(pipeline);
            }
            if (pipelineLayout.IsValid()) {
                d.DestroyPipelineLayout(pipelineLayout);
            }
            if (descLayout.IsValid()) {
                d.DestroyDescriptorLayout(descLayout);
            }
            if (torusDescSet.IsValid()) {
                d.DestroyDescriptorSet(torusDescSet);
            }
            if (cubeDescSet.IsValid()) {
                d.DestroyDescriptorSet(cubeDescSet);
            }
            if (fragModule.IsValid()) {
                d.DestroyShaderModule(fragModule);
            }
            if (meshModule.IsValid()) {
                d.DestroyShaderModule(meshModule);
            }
            if (taskModule.IsValid()) {
                d.DestroyShaderModule(taskModule);
            }
            destroy(torus.vertexBuffer);
            destroy(torus.indexBuffer);
            destroy(torus.meshletBuffer);
            destroy(cube.vertexBuffer);
            destroy(cube.indexBuffer);
            destroy(cube.meshletBuffer);
            return 0;
        });
    }
};

// ============================================================================
// BuildPushConst
// ============================================================================

static auto BuildMeshPushConst(
    float rotX, float rotY, float dist, float aspect, float4x4 localTransform, float r, float g, float b,
    uint32_t meshletCount
) -> MeshPushConst {
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
        .meshletCount = meshletCount,
    };
}

// ============================================================================
// BuildAndExecuteRenderGraph
// ============================================================================

static auto BuildAndExecuteRenderGraph(
    SceneResources& scene, miki::frame::FrameContext& frameCtx, miki::frame::SyncScheduler& syncSched,
    miki::frame::CommandPoolAllocator& cmdPoolAlloc
) -> bool {
    uint32_t w = frameCtx.width;
    uint32_t h = frameCtx.height;
    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;

    struct FrameDims {
        uint32_t w, h;
    };
    FrameDims dims{w, h};

    auto torusPC = BuildMeshPushConst(
        scene.rotX, scene.rotY, scene.distance, aspect, MakeTranslation(-1.0f, 0.0f, 0.0f), 0.85f, 0.55f, 0.20f,
        scene.torus.meshletCount
    );
    auto cubePC = BuildMeshPushConst(
        scene.rotX, scene.rotY, scene.distance, aspect, MakeTranslation(1.0f, 0.0f, 0.0f), 0.25f, 0.60f, 0.85f,
        scene.cube.meshletCount
    );

    // Task shader workgroup count: ceil(meshletCount / 32)
    uint32_t torusTaskGroups = (scene.torus.meshletCount + 31) / 32;
    uint32_t cubeTaskGroups = (scene.cube.meshletCount + 31) / 32;

    // ── Step 1: Build render graph ─────────────────────────────────
    RenderGraphBuilder builder;
    auto backbuffer = builder.ImportBackbuffer(frameCtx.swapchainImage, "Backbuffer");

    // Torus pass (mesh shader)
    MeshShaderPassConfig torusMeshCfg{
        .taskGroupCountX = torusTaskGroups,
        .amplificationRate
        = static_cast<float>(scene.torus.meshletCount) / static_cast<float>(std::max(torusTaskGroups, 1u)),
        .verticesPerMeshlet = kMaxMeshletVertices,
        .trianglesPerMeshlet = kMaxMeshletTriangles,
    };
    backbuffer = [&] {
        auto bb = backbuffer;
        (void)builder.AddMeshShaderPass(
            "DrawTorus", torusMeshCfg,
            [&, bb](PassBuilder& pb) mutable {
                bb = pb.WriteColorAttachment(bb);
                pb.SetSideEffects();
            },
            [&scene, &torusPC, &dims](RenderPassContext& ctx) {
                ctx.DispatchCommands([&](auto& cmd) {
                    cmd.CmdBindPipeline(scene.pipeline);
                    cmd.CmdBindDescriptorSet(0, scene.torusDescSet);
                    cmd.CmdSetViewport(
                        {.x = 0,
                         .y = 0,
                         .width = static_cast<float>(dims.w),
                         .height = static_cast<float>(dims.h),
                         .minDepth = 0,
                         .maxDepth = 1}
                    );
                    cmd.CmdSetScissor({.offset = {0, 0}, .extent = {dims.w, dims.h}});
                    cmd.CmdPushConstants(
                        ShaderStage::Task | ShaderStage::Mesh | ShaderStage::Fragment, 0, sizeof(MeshPushConst),
                        &torusPC
                    );
                    uint32_t taskGroups = (torusPC.meshletCount + 31) / 32;
                    cmd.CmdDrawMeshTasks(taskGroups, 1, 1);
                });
            }
        );
        return bb;
    }();

    // Cube pass (mesh shader)
    MeshShaderPassConfig cubeMeshCfg{
        .taskGroupCountX = cubeTaskGroups,
        .amplificationRate
        = static_cast<float>(scene.cube.meshletCount) / static_cast<float>(std::max(cubeTaskGroups, 1u)),
        .verticesPerMeshlet = kMaxMeshletVertices,
        .trianglesPerMeshlet = kMaxMeshletTriangles,
    };
    backbuffer = [&] {
        auto bb = backbuffer;
        (void)builder.AddMeshShaderPass(
            "DrawCube", cubeMeshCfg,
            [&, bb](PassBuilder& pb) mutable {
                pb.ReadTexture(bb);
                bb = pb.WriteColorAttachment(bb);
                pb.SetSideEffects();
            },
            [&scene, &cubePC, &dims](RenderPassContext& ctx) {
                ctx.DispatchCommands([&](auto& cmd) {
                    cmd.CmdBindPipeline(scene.pipeline);
                    cmd.CmdBindDescriptorSet(0, scene.cubeDescSet);
                    cmd.CmdSetViewport(
                        {.x = 0,
                         .y = 0,
                         .width = static_cast<float>(dims.w),
                         .height = static_cast<float>(dims.h),
                         .minDepth = 0,
                         .maxDepth = 1}
                    );
                    cmd.CmdSetScissor({.offset = {0, 0}, .extent = {dims.w, dims.h}});
                    cmd.CmdPushConstants(
                        ShaderStage::Task | ShaderStage::Mesh | ShaderStage::Fragment, 0, sizeof(MeshPushConst), &cubePC
                    );
                    uint32_t taskGroups = (cubePC.meshletCount + 31) / 32;
                    cmd.CmdDrawMeshTasks(taskGroups, 1, 1);
                });
            }
        );
        return bb;
    }();

    // Present pass
    builder.AddPresentPass("Present", backbuffer);
    builder.Build();

    // ── Step 2: Compile ─────────────────────────────────────────────
    RenderGraphCompiler compiler;
    auto compileResult = compiler.Compile(builder);
    if (!compileResult) {
        std::println("[mesh_demo] RenderGraph compile failed");
        return false;
    }
    auto& compiled = *compileResult;

    // ── Step 3: Execute ─────────────────────────────────────────────
    RenderGraphExecutor executor;
    auto execResult = executor.Execute(compiled, builder, frameCtx, scene.device, syncSched, cmdPoolAlloc);
    if (!execResult) {
        std::println("[mesh_demo] RenderGraph execute failed");
        return false;
    }
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

    (void)fm->EndFrame(std::span<const miki::frame::FrameManager::SubmitBatch>{});
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
    std::println("[mesh_demo] RenderGraph mesh shader torus+cube demo with {} backend", BackendName(backend));

    // 1. Window manager
    auto glfwBackend = std::make_unique<GlfwWindowBackend>(backend, true);
    auto* backendPtr = glfwBackend.get();
    auto wmResult = WindowManager::Create(std::move(glfwBackend));
    if (!wmResult) {
        std::println("[mesh_demo] WindowManager failed");
        return 1;
    }
    auto wm = std::move(*wmResult);
    g_wm = &wm;

    // 2. Window
    auto winResult
        = wm.CreateWindow({.title = "miki RenderGraph — Mesh Shader Torus + Cube", .width = 1280, .height = 720});
    if (!winResult) {
        std::println("[mesh_demo] CreateWindow failed");
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
        std::println("[mesh_demo] Device creation failed");
        return 1;
    }
    auto device = std::move(*deviceResult);

    // 4. Check mesh shader support
    bool hasMesh = device.GetHandle().Dispatch([](auto& d) { return d.GetCapabilities().HasMeshShader(); });
    if (!hasMesh) {
        std::println("[mesh_demo] ERROR: This demo requires mesh shader support. Your GPU/driver does not support it.");
        return 1;
    }
    std::println("[mesh_demo] Mesh shader support confirmed");

    // 5. Surface manager
    auto smResult = SurfaceManager::Create(device.GetHandle(), wm);
    if (!smResult) {
        std::println("[mesh_demo] SurfaceManager failed");
        return 1;
    }
    auto surfMgr = std::move(*smResult);
    g_sm = &surfMgr;

    auto attachResult = surfMgr.AttachSurface(g_mainWindow, {.presentMode = PresentMode::Fifo});
    if (!attachResult) {
        std::println("[mesh_demo] AttachSurface failed");
        return 1;
    }

    // 6. Shader compilation (task + mesh + fragment)
    auto shaderTarget
        = device.GetHandle().Dispatch([](auto& d) { return miki::shader::PreferredShaderTarget(d.GetCapabilities()); });

    auto compilerResult = shader::SlangCompiler::Create();
    if (!compilerResult) {
        std::println("[mesh_demo] SlangCompiler::Create failed");
        return 1;
    }
    auto compiler = std::move(*compilerResult);
    std::string shaderSrc(kMeshShaderSlangSource);

    auto tsBlob = CompileStage(compiler, shaderSrc, "ts_main", shader::ShaderStage::Task, shaderTarget, "TaskShader");
    if (!tsBlob) {
        std::println("[mesh_demo] Task shader compilation failed");
        return 1;
    }

    auto msBlob = CompileStage(compiler, shaderSrc, "ms_main", shader::ShaderStage::Mesh, shaderTarget, "MeshShader");
    if (!msBlob) {
        std::println("[mesh_demo] Mesh shader compilation failed");
        return 1;
    }

    auto fsBlob
        = CompileStage(compiler, shaderSrc, "fs_main", shader::ShaderStage::Fragment, shaderTarget, "FragShader");
    if (!fsBlob) {
        std::println("[mesh_demo] Fragment shader compilation failed");
        return 1;
    }

    std::println(
        "[mesh_demo] Shaders compiled: TS={} B, MS={} B, FS={} B", tsBlob->data.size(), msBlob->data.size(),
        fsBlob->data.size()
    );

    SceneResources::CompiledMeshShaders shaders{
        .task = std::move(*tsBlob), .mesh = std::move(*msBlob), .frag = std::move(*fsBlob)
    };

    // 7. Scene resources
    SceneResources scene;
    g_scene = &scene;
    if (!scene.Init(device.GetHandle(), shaders, &surfMgr, g_mainWindow)) {
        std::println("[mesh_demo] Scene init failed");
        return 1;
    }

    // 8. Frame infrastructure
    auto* fm = surfMgr.GetFrameManager(g_mainWindow);
    if (!fm) {
        std::println("[mesh_demo] No FrameManager");
        return 1;
    }

    // Use the device-owned SyncScheduler (per-device singleton).
    // SurfaceManager::GetSyncScheduler() delegates to device.GetSyncScheduler().
    g_scheduler = &surfMgr.GetSyncScheduler();

    miki::frame::CommandPoolAllocator::Desc poolDesc{
        .device = device.GetHandle(),
        .framesInFlight = fm->FramesInFlight(),
    };
    auto poolResult = miki::frame::CommandPoolAllocator::Create(poolDesc);
    if (!poolResult) {
        std::println("[mesh_demo] CommandPoolAllocator failed");
        return 1;
    }
    auto cmdPoolAlloc = std::move(*poolResult);
    g_poolAllocator = &cmdPoolAlloc;

    std::println("[mesh_demo] Running. Drag to rotate, scroll to zoom. ESC to quit.");
    std::println("[mesh_demo] Each frame: Builder(2 mesh passes + present) -> Compiler -> Executor");

    // 9. Live resize
#ifndef __EMSCRIPTEN__
    backendPtr->SetLiveResizeCallback([](WindowHandle w, uint32_t width, uint32_t height) {
        (void)g_sm->ResizeSurface(w, width, height);
        RenderOneFrame(w);
    });
#endif

    // 10. Main loop
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
