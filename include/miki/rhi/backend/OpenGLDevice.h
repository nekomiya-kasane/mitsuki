/** @file OpenGLDevice.h
 *  @brief OpenGL 4.3+ (Tier 4) backend device.
 *
 *  Conditionally included by AllBackends.h only when MIKI_BUILD_OPENGL=1.
 *  Uses glad2 (MX multi-context) as the GL loader.
 *  DSA (Direct State Access, GL 4.5 preferred) with GL 4.3 core fallback.
 *  API-managed memory (glBufferStorage / glTexStorage2D immutable).
 *  Push constants emulated via 128B UBO at binding point 0.
 *  Immediate-mode command recording: GL calls execute directly, no deferred replay.
 *
 *  Resource storage uses typed HandlePool (O(1) alloc/free/lookup, generation-safe).
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/backend/BackendStub.h"

#pragma warning(push)
#pragma warning(disable : 4199)  // MSVC warning for language extension tokens
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#include <glad/gl.h>
#pragma clang diagnostic pop
#pragma warning(pop)

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace miki::platform {
    class IWindowBackend;
}

namespace miki::rhi {

    // =========================================================================
    // GLExtContext — holds extension function pointers not in glad2
    // =========================================================================

    class GLExtContext {
       public:
        void Init(GladGLContext* gl, GLADloadfunc loader);

        // GL_EXT_depth_bounds_test (NVIDIA-only)
        [[nodiscard]] auto HasDepthBoundsTest() const noexcept -> bool { return hasDepthBoundsTest_; }
        void DepthBoundsEXT(GLdouble zmin, GLdouble zmax) const {
            if (pfnDepthBoundsEXT_) {
                pfnDepthBoundsEXT_(zmin, zmax);
            }
        }

        // GL_ARB_gl_spirv (GL 4.6 core or extension)
        [[nodiscard]] auto HasSpirvSupport() const noexcept -> bool { return hasSpirvSupport_; }
        void SpecializeShader(
            GLuint shader, const GLchar* pEntryPoint, GLuint numSpecializationConstants, const GLuint* pConstantIndex,
            const GLuint* pConstantValue
        ) const {
            if (pfnSpecializeShader_) {
                pfnSpecializeShader_(shader, pEntryPoint, numSpecializationConstants, pConstantIndex, pConstantValue);
            }
        }

       private:
        using PFNGLDEPTHBOUNDSEXTPROC = void(GLAD_API_PTR*)(GLdouble, GLdouble);
        PFNGLDEPTHBOUNDSEXTPROC pfnDepthBoundsEXT_ = nullptr;
        bool hasDepthBoundsTest_ = false;

        // GL_ARB_gl_spirv / GL 4.6
        using PFNGLSPECIALIZESHADERPROC
            = void(GLAD_API_PTR*)(GLuint, const GLchar*, GLuint, const GLuint*, const GLuint*);
        PFNGLSPECIALIZESHADERPROC pfnSpecializeShader_ = nullptr;
        bool hasSpirvSupport_ = false;
    };

    // =========================================================================
    // GLContext — per-GL-context state tracker with diff-based thin wrappers.
    //
    // OpenGL state is global per-context. This class owns the shadow cache and
    // ensures GL calls are only issued when state actually changes.
    // Device holds one GLContext per native GL context (render, transfer, ...).
    // CommandBuffer and Device methods operate through GLContext*, never raw gl->.
    // =========================================================================

    class GLContext {
       public:
        void Init(GladGLContext* gl) { gl_ = gl; }
        [[nodiscard]] auto Raw() const noexcept -> GladGLContext* { return gl_; }

        // -- Program / VAO --
        void UseProgram(GLuint program) {
            if (cache_.program != program) {
                gl_->UseProgram(program);
                cache_.program = program;
            }
        }
        void BindVertexArray(GLuint vao) {
            if (cache_.vao != vao) {
                gl_->BindVertexArray(vao);
                cache_.vao = vao;
            }
        }

        // -- Rasterizer --
        void SetCullFace(bool enable, GLenum mode) {
            if (cache_.cullFaceEnabled != enable) {
                enable ? gl_->Enable(GL_CULL_FACE) : gl_->Disable(GL_CULL_FACE);
                cache_.cullFaceEnabled = enable;
            }
            if (enable && cache_.cullMode != mode) {
                gl_->CullFace(mode);
                cache_.cullMode = mode;
            }
        }
        void SetFrontFace(GLenum face) {
            if (cache_.frontFace != face) {
                gl_->FrontFace(face);
                cache_.frontFace = face;
            }
        }
        void SetPolygonMode(GLenum mode) {
            if (cache_.polygonMode != mode) {
                gl_->PolygonMode(GL_FRONT_AND_BACK, mode);
                cache_.polygonMode = mode;
            }
        }

        // -- Depth --
        void SetDepthTest(bool enable, GLenum func) {
            if (cache_.depthTestEnabled != enable) {
                enable ? gl_->Enable(GL_DEPTH_TEST) : gl_->Disable(GL_DEPTH_TEST);
                cache_.depthTestEnabled = enable;
            }
            if (enable && cache_.depthFunc != func) {
                gl_->DepthFunc(func);
                cache_.depthFunc = func;
            }
        }
        void SetDepthWrite(bool enable) {
            if (cache_.depthWriteEnabled != enable) {
                gl_->DepthMask(enable ? GL_TRUE : GL_FALSE);
                cache_.depthWriteEnabled = enable;
            }
        }
        void SetDepthClamp(bool enable) {
            if (cache_.depthClampEnabled != enable) {
                enable ? gl_->Enable(GL_DEPTH_CLAMP) : gl_->Disable(GL_DEPTH_CLAMP);
                cache_.depthClampEnabled = enable;
            }
        }

        // -- Stencil --
        void SetStencilTest(bool enable) {
            if (cache_.stencilTestEnabled != enable) {
                enable ? gl_->Enable(GL_STENCIL_TEST) : gl_->Disable(GL_STENCIL_TEST);
                cache_.stencilTestEnabled = enable;
            }
        }

        // -- Blend (per-attachment) --
        void SetBlendEnable(uint32_t index, bool enable) {
            if (cache_.blend[index].enabled != enable) {
                enable ? gl_->Enablei(GL_BLEND, index) : gl_->Disablei(GL_BLEND, index);
                cache_.blend[index].enabled = enable;
            }
        }
        void SetBlendFunc(uint32_t i, GLenum srcC, GLenum dstC, GLenum srcA, GLenum dstA) {
            auto& cb = cache_.blend[i];
            if (cb.srcColor != srcC || cb.dstColor != dstC || cb.srcAlpha != srcA || cb.dstAlpha != dstA) {
                gl_->BlendFuncSeparatei(i, srcC, dstC, srcA, dstA);
                cb.srcColor = srcC;
                cb.dstColor = dstC;
                cb.srcAlpha = srcA;
                cb.dstAlpha = dstA;
            }
        }
        void SetBlendEquation(uint32_t i, GLenum colorOp, GLenum alphaOp) {
            auto& cb = cache_.blend[i];
            if (cb.colorOp != colorOp || cb.alphaOp != alphaOp) {
                gl_->BlendEquationSeparatei(i, colorOp, alphaOp);
                cb.colorOp = colorOp;
                cb.alphaOp = alphaOp;
            }
        }
        void SetColorMask(uint32_t i, GLuint mask) {
            if (cache_.blend[i].writeMask != mask) {
                gl_->ColorMaski(
                    i, (mask & 0x1) ? GL_TRUE : GL_FALSE, (mask & 0x2) ? GL_TRUE : GL_FALSE,
                    (mask & 0x4) ? GL_TRUE : GL_FALSE, (mask & 0x8) ? GL_TRUE : GL_FALSE
                );
                cache_.blend[i].writeMask = mask;
            }
        }
        void SetGlobalBlendDisable() {
            if (cache_.globalBlendEnabled) {
                gl_->Disable(GL_BLEND);
                cache_.globalBlendEnabled = false;
            }
        }
        void SetGlobalBlendEnabled(bool v) { cache_.globalBlendEnabled = v; }
        [[nodiscard]] auto IsGlobalBlendEnabled() const noexcept -> bool { return cache_.globalBlendEnabled; }

        // -- FBO --
        void BindFramebuffer(GLenum target, GLuint fbo) {
            if (cache_.boundFBO != fbo) {
                gl_->BindFramebuffer(target, fbo);
                cache_.boundFBO = fbo;
            }
        }
        [[nodiscard]] auto BoundFBO() const noexcept -> GLuint { return cache_.boundFBO; }

        // -- Dynamic state: viewport --
        void SetViewport(float x, float y, float w, float h) {
            if (cache_.vpX != x || cache_.vpY != y || cache_.vpW != w || cache_.vpH != h) {
                gl_->ViewportIndexedf(0, x, y, w, h);
                cache_.vpX = x;
                cache_.vpY = y;
                cache_.vpW = w;
                cache_.vpH = h;
            }
        }
        void SetDepthRange(double nearVal, double farVal) {
            if (cache_.depthRangeNear != nearVal || cache_.depthRangeFar != farVal) {
                gl_->DepthRangeIndexed(0, nearVal, farVal);
                cache_.depthRangeNear = nearVal;
                cache_.depthRangeFar = farVal;
            }
        }

        // -- Dynamic state: scissor --
        void SetScissorTest(bool enable) {
            if (cache_.scissorTestEnabled != enable) {
                enable ? gl_->Enable(GL_SCISSOR_TEST) : gl_->Disable(GL_SCISSOR_TEST);
                cache_.scissorTestEnabled = enable;
            }
        }
        void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) {
            if (cache_.scissorX != x || cache_.scissorY != y || cache_.scissorW != w || cache_.scissorH != h) {
                gl_->ScissorIndexed(0, x, y, w, h);
                cache_.scissorX = x;
                cache_.scissorY = y;
                cache_.scissorW = w;
                cache_.scissorH = h;
            }
        }

       private:
        GladGLContext* gl_ = nullptr;

        struct StateCache {
            GLuint program = 0;
            GLuint vao = 0;
            // Rasterizer
            bool cullFaceEnabled = false;
            GLenum cullMode = GL_BACK;
            GLenum frontFace = GL_CCW;
            GLenum polygonMode = GL_FILL;
            // Depth
            bool depthTestEnabled = false;
            bool depthWriteEnabled = true;
            GLenum depthFunc = GL_LESS;
            bool depthClampEnabled = false;
            // Stencil
            bool stencilTestEnabled = false;
            // FBO
            GLuint boundFBO = 0;
            // Viewport
            float vpX = 0, vpY = 0, vpW = 0, vpH = 0;
            double depthRangeNear = 0.0, depthRangeFar = 1.0;
            // Scissor
            bool scissorTestEnabled = false;
            int32_t scissorX = 0, scissorY = 0;
            uint32_t scissorW = 0, scissorH = 0;
            // Blend (per-attachment, up to 8)
            struct AttachmentBlend {
                bool enabled = false;
                GLenum srcColor = GL_ONE, dstColor = GL_ZERO, colorOp = GL_FUNC_ADD;
                GLenum srcAlpha = GL_ONE, dstAlpha = GL_ZERO, alphaOp = GL_FUNC_ADD;
                GLuint writeMask = 0xF;
            };
            AttachmentBlend blend[8]{};
            bool globalBlendEnabled = false;
        };
        StateCache cache_;
    };

    // =========================================================================
    // Per-resource backend payloads (stored in HandlePool slots)
    // =========================================================================

    struct GLBufferData {
        GLuint buffer = 0;
        uint64_t size = 0;
        void* mappedPtr = nullptr;
        BufferUsage usage{};
        GLenum glUsage = 0;  // GL_DYNAMIC_STORAGE_BIT etc.
    };

    struct GLTextureData {
        GLuint texture = 0;
        GLenum target = GL_TEXTURE_2D;  // GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP, etc.
        GLenum internalFormat = GL_RGBA8;
        uint32_t width = 0, height = 0, depth = 0;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        TextureDimension dimension = TextureDimension::Tex2D;  // For view type inference
        bool ownsTexture = true;                               // false for swapchain (external FBO)
        bool isDefaultFramebuffer = false;  // true for swapchain default FBO (texture ID 0, no glTextureView)
    };

    struct GLTextureViewData {
        // OpenGL 4.3+ texture views via glTextureView
        GLuint viewTexture = 0;
        TextureHandle parentTexture;
        GLenum target = GL_TEXTURE_2D;
        GLuint fbo = 0;  // Pre-created single-attachment FBO (color or depth/stencil)
        // TODO (Nekomiya) this seems bad. review this in the future
        bool ownsView = false;              // true if created via glTextureView, false if alias
        bool isDefaultFramebuffer = false;  // true for swapchain default FBO (render via glBindFramebuffer(0))
        bool isDepthStencil = false;        // true if this view is a depth/stencil attachment
    };

    // FBO cache key for MRT (multi-color-attachment) rendering passes.
    // Single-color + optional depth uses the fast path (view-owned FBO).
    struct GLFBOCacheKey {
        static constexpr uint32_t kMaxCachedAttachments = 8;
        std::array<GLuint, kMaxCachedAttachments> colorTextures{};
        GLuint depthTexture = 0;
        GLuint stencilTexture = 0;
        uint32_t colorCount = 0;

        bool operator==(const GLFBOCacheKey&) const = default;
    };

    struct GLFBOCacheKeyHash {
        static constexpr size_t kHashCombineSeed = 0x9e3779b9;  // Golden ratio fractional bits
        static constexpr size_t kHashShiftLeft = 6;
        static constexpr size_t kHashShiftRight = 2;

        auto operator()(const GLFBOCacheKey& k) const noexcept -> size_t {
            size_t h = std::hash<uint32_t>{}(k.colorCount);
            for (uint32_t i = 0; i < k.colorCount; ++i) {
                h ^= std::hash<GLuint>{}(k.colorTextures[i]) + kHashCombineSeed + (h << kHashShiftLeft)
                     + (h >> kHashShiftRight);
            }
            h ^= std::hash<GLuint>{}(k.depthTexture) + kHashCombineSeed + (h << kHashShiftLeft)
                 + (h >> kHashShiftRight);
            h ^= std::hash<GLuint>{}(k.stencilTexture) + kHashCombineSeed + (h << kHashShiftLeft)
                 + (h >> kHashShiftRight);
            return h;
        }
    };

    struct GLSamplerData {
        GLuint sampler = 0;
    };

    struct GLShaderModuleData {
        std::string source;              // GLSL source text (used when isSPIRV == false)
        std::vector<uint8_t> spirvData;  // SPIR-V binary (used when isSPIRV == true)
        ShaderStage stage = ShaderStage::Vertex;
        GLuint compiledShader = 0;  // Cached compiled GL shader object
        bool isSPIRV = false;
        std::string entryPoint = "main";  // Entry point name for SPIR-V specialization
    };

    struct GLFenceData {
        GLsync sync = nullptr;
        bool signaled = false;
    };

    struct GLSemaphoreData {
        // OpenGL has no GPU-GPU semaphores; emulated via glFenceSync
        GLsync sync = nullptr;
        uint64_t value = 0;
        SemaphoreType type = SemaphoreType::Binary;
    };

    struct GLPipelineData {
        GLuint program = 0;
        bool isCompute = false;
        // Cached fixed-function state for graphics pipelines
        GLenum primitiveTopology = GL_TRIANGLES;
        GLenum cullMode = GL_BACK;
        GLenum frontFace = GL_CCW;
        GLenum polygonMode = GL_FILL;
        bool depthTestEnable = false;
        bool depthWriteEnable = false;
        GLenum depthFunc = GL_LESS;
        bool stencilTestEnable = false;
        bool depthClampEnable = false;
        bool blendEnable = false;
        // Per-attachment blend (up to 8)
        struct BlendState {
            bool enable = false;
            GLenum srcColor = GL_ONE, dstColor = GL_ZERO, colorOp = GL_FUNC_ADD;
            GLenum srcAlpha = GL_ONE, dstAlpha = GL_ZERO, alphaOp = GL_FUNC_ADD;
            GLuint writeMask = 0xF;
        };
        BlendState blendStates[8]{};
        uint32_t colorAttachmentCount = 0;
        // Stencil state
        struct StencilState {
            GLenum failOp = GL_KEEP, depthFailOp = GL_KEEP, passOp = GL_KEEP;
            GLenum compareOp = GL_ALWAYS;
            uint32_t compareMask = 0xFF, writeMask = 0xFF;
        };
        StencilState stencilFront{}, stencilBack{};
        // Vertex input (VAO is created per-pipeline in GL)
        GLuint vao = 0;
        struct VertexBinding {
            uint32_t binding = 0;
            uint32_t stride = 0;
            bool perInstance = false;
        };
        std::vector<VertexBinding> vertexBindings;
        struct VertexAttrib {
            uint32_t location = 0;
            uint32_t binding = 0;
            GLenum type = GL_FLOAT;
            GLint components = 4;
            uint32_t offset = 0;
            bool normalized = false;
        };
        std::vector<VertexAttrib> vertexAttribs;
    };

    struct GLPipelineLayoutData {
        // Push constant emulation: UBO binding 0, 128B
        uint32_t pushConstantSize = 0;
        // Descriptor set layout references (for binding point mapping)
        std::vector<DescriptorLayoutHandle> setLayouts;
    };

    struct GLDescriptorLayoutData {
        struct BindingInfo {
            uint32_t binding = 0;
            BindingType type{};
            uint32_t count = 1;
        };
        std::vector<BindingInfo> bindings;
    };

    struct GLDescriptorSetData {
        DescriptorLayoutHandle layout;
        struct BoundResource {
            uint32_t binding = 0;
            BindingType type{};
            // Union-like storage
            GLuint buffer = 0;
            uint64_t offset = 0;
            uint64_t range = 0;
            GLuint texture = 0;
            GLuint sampler = 0;
            GLenum imageFormat = GL_RGBA8;  // Cached internalFormat for StorageTexture (glBindImageTexture)
        };
        std::vector<BoundResource> resources;
    };

    struct GLPipelineCacheData {
        std::vector<uint8_t> blob;  // GL has no native pipeline cache
    };

    struct GLQueryPoolData {
        std::vector<GLuint> queries;
        QueryType type = QueryType::Timestamp;
        uint32_t count = 0;
    };

    struct GLSwapchainData {
        // OpenGL swapchain is the default framebuffer managed by window backend
        uint32_t width = 0, height = 0;
        TextureHandle colorTexture;          // Wrapper handle for default FB color
        TextureViewHandle colorTextureView;  // Pre-created view for the color texture
        platform::IWindowBackend* windowBackend = nullptr;
        void* nativeToken = nullptr;
        uint32_t currentImage = 0;
    };

    struct GLCommandBufferData {
        QueueType queueType = QueueType::Graphics;
        bool isSecondary = false;
    };

    struct GLCommandPoolData {
        QueueType queueType = QueueType::Graphics;
        // Cached C++ wrappers + handles for pool-reset reuse (spec §19)
        struct CachedEntry {
            CommandBufferHandle bufHandle;
            std::unique_ptr<OpenGLCommandBuffer> wrapper;
        };
        std::vector<CachedEntry> cachedEntries;
        uint32_t nextFreeIndex = 0;
    };

    // =========================================================================
    // OpenGL device description
    // =========================================================================

    struct OpenGLDeviceDesc {
        bool enableValidation = true;
        platform::IWindowBackend* windowBackend = nullptr;  // Required for swapchain and GL proc loading
        void* nativeToken = nullptr;                        // Native window token from backend
    };

    // =========================================================================
    // OpenGLDevice — Tier 4 backend
    // =========================================================================

    class OpenGLDevice : public DeviceBase<OpenGLDevice> {
       public:
        OpenGLDevice();
        ~OpenGLDevice();

        OpenGLDevice(const OpenGLDevice&) = delete;
        auto operator=(const OpenGLDevice&) -> OpenGLDevice& = delete;
        OpenGLDevice(OpenGLDevice&&) = delete;
        auto operator=(OpenGLDevice&&) -> OpenGLDevice& = delete;

        [[nodiscard]] auto Init(const OpenGLDeviceDesc& desc = {}) -> RhiResult<void>;

        // -- Native accessors --
        [[nodiscard]] auto GetGLContext() const noexcept -> GladGLContext* { return renderCtx_.Raw(); }
        [[nodiscard]] auto GetContext() noexcept -> GLContext& { return renderCtx_; }
        [[nodiscard]] auto GetContext() const noexcept -> const GLContext& { return renderCtx_; }
        [[nodiscard]] auto HasSpirvSupport() const noexcept -> bool { return ext_.HasSpirvSupport(); }

        // -- Capability --
        auto GetBackendTypeImpl() const -> BackendType { return BackendType::OpenGL43; }
        auto GetCapabilitiesImpl() const -> const GpuCapabilityProfile& { return capabilities_; }
        auto GetQueueTimelinesImpl() const -> QueueTimelines { return {}; }

        // -- Swapchain (OpenGLSwapchain.cpp) --
        auto CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle>;
        void DestroySwapchainImpl(SwapchainHandle h);
        auto ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void>;
        auto AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle fence) -> RhiResult<uint32_t>;
        auto GetSwapchainTextureImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle;
        auto GetSwapchainTextureViewImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureViewHandle;
        auto GetSwapchainImageCountImpl(SwapchainHandle h) -> uint32_t;
        void PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores);

        // -- Sync (OpenGLDevice.cpp) --
        auto CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle>;
        void DestroyFenceImpl(FenceHandle h);
        void WaitFenceImpl(FenceHandle h, uint64_t timeout);
        void ResetFenceImpl(FenceHandle h);
        auto GetFenceStatusImpl(FenceHandle h) -> bool;

        auto CreateSemaphoreImpl(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle>;
        void DestroySemaphoreImpl(SemaphoreHandle h);
        void SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value);
        void WaitSemaphoreImpl(SemaphoreHandle h, uint64_t value, uint64_t timeout);
        auto GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t;

        void WaitIdleImpl();
        void SubmitImpl(QueueType queue, const SubmitDesc& desc);

        // -- Resources (OpenGLResources.cpp) --
        auto CreateBufferImpl(const BufferDesc& desc) -> RhiResult<BufferHandle>;
        void DestroyBufferImpl(BufferHandle h);
        auto MapBufferImpl(BufferHandle h) -> RhiResult<void*>;
        void UnmapBufferImpl(BufferHandle h);
        void FlushMappedRangeImpl(BufferHandle h, uint64_t offset, uint64_t size);
        void InvalidateMappedRangeImpl(BufferHandle h, uint64_t offset, uint64_t size);
        auto GetBufferDeviceAddressImpl(BufferHandle h) -> uint64_t;

        auto CreateTextureImpl(const TextureDesc& desc) -> RhiResult<TextureHandle>;
        auto CreateTextureViewImpl(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle>;
        auto GetTextureViewTextureImpl(TextureViewHandle h) -> TextureHandle;
        void DestroyTextureViewImpl(TextureViewHandle h);
        void DestroyTextureImpl(TextureHandle h);

        auto CreateSamplerImpl(const SamplerDesc& desc) -> RhiResult<SamplerHandle>;
        void DestroySamplerImpl(SamplerHandle h);

        // -- Memory aliasing (OpenGLResources.cpp) --
        auto CreateMemoryHeapImpl(const MemoryHeapDesc& desc) -> RhiResult<DeviceMemoryHandle>;
        void DestroyMemoryHeapImpl(DeviceMemoryHandle h);
        void AliasBufferMemoryImpl(BufferHandle buf, DeviceMemoryHandle heap, uint64_t offset);
        void AliasTextureMemoryImpl(TextureHandle tex, DeviceMemoryHandle heap, uint64_t offset);
        auto GetBufferMemoryRequirementsImpl(BufferHandle h) -> MemoryRequirements;
        auto GetTextureMemoryRequirementsImpl(TextureHandle h) -> MemoryRequirements;

        // -- Sparse binding (not available on T4) --
        auto GetSparsePageSizeImpl() const -> SparsePageSize;
        void SubmitSparseBindsImpl(
            QueueType queue, const SparseBindDesc& binds, std::span<const SemaphoreSubmitInfo> wait,
            std::span<const SemaphoreSubmitInfo> signal
        );

        // -- Shader (OpenGLResources.cpp) --
        auto CreateShaderModuleImpl(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle>;
        void DestroyShaderModuleImpl(ShaderModuleHandle h);

        // -- Descriptors (OpenGLDescriptors.cpp) --
        auto CreateDescriptorLayoutImpl(const DescriptorLayoutDesc& desc) -> RhiResult<DescriptorLayoutHandle>;
        void DestroyDescriptorLayoutImpl(DescriptorLayoutHandle h);
        auto CreatePipelineLayoutImpl(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle>;
        void DestroyPipelineLayoutImpl(PipelineLayoutHandle h);
        auto CreateDescriptorSetImpl(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle>;
        void UpdateDescriptorSetImpl(DescriptorSetHandle h, std::span<const DescriptorWrite> writes);
        void DestroyDescriptorSetImpl(DescriptorSetHandle h);

        // -- Pipelines (OpenGLPipelines.cpp) --
        auto CreateGraphicsPipelineImpl(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        auto CreateComputePipelineImpl(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle>;
        auto CreateRayTracingPipelineImpl(const RayTracingPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        void DestroyPipelineImpl(PipelineHandle h);

        auto CreatePipelineCacheImpl(std::span<const uint8_t> initialData) -> RhiResult<PipelineCacheHandle>;
        auto GetPipelineCacheDataImpl(PipelineCacheHandle h) -> std::vector<uint8_t>;
        void DestroyPipelineCacheImpl(PipelineCacheHandle h);

        auto CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc& desc) -> RhiResult<PipelineLibraryPartHandle>;
        auto LinkGraphicsPipelineImpl(const LinkedPipelineDesc& desc) -> RhiResult<PipelineHandle>;

        // -- Command pools §19 (OpenGLQuery.cpp) --
        auto CreateCommandPoolImpl(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle>;
        void DestroyCommandPoolImpl(CommandPoolHandle h);
        void ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags);
        auto AllocateFromPoolImpl(CommandPoolHandle pool, bool secondary) -> RhiResult<CommandListAcquisition>;
        void FreeFromPoolImpl(CommandPoolHandle pool, const CommandListAcquisition& acq);

        // -- Query (OpenGLQuery.cpp) --
        auto CreateQueryPoolImpl(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle>;
        void DestroyQueryPoolImpl(QueryPoolHandle h);
        auto GetQueryResultsImpl(QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results)
            -> RhiResult<void>;
        auto GetTimestampPeriodImpl() -> double;

        // -- Acceleration structure (not available on T4) --
        auto GetBLASBuildSizesImpl(const BLASDesc& desc) -> AccelStructBuildSizes;
        auto GetTLASBuildSizesImpl(const TLASDesc& desc) -> AccelStructBuildSizes;
        auto CreateBLASImpl(const BLASDesc& desc) -> RhiResult<AccelStructHandle>;
        auto CreateTLASImpl(const TLASDesc& desc) -> RhiResult<AccelStructHandle>;
        void DestroyAccelStructImpl(AccelStructHandle h);

        // -- Memory stats --
        auto GetMemoryStatsImpl() const -> MemoryStats;
        auto GetMemoryHeapBudgetsImpl(std::span<MemoryHeapBudget> out) const -> uint32_t;

        // -- Surface capabilities --
        auto GetSurfaceCapabilitiesImpl(const NativeWindowHandle& window) const -> RenderSurfaceCapabilities;

        // -- HandlePool accessors (for OpenGLCommandBuffer cross-file access) --
        auto GetBufferPool() -> HandlePool<GLBufferData, BufferTag, kMaxBuffers>& { return buffers_; }
        auto GetTexturePool() -> HandlePool<GLTextureData, TextureTag, kMaxTextures>& { return textures_; }
        auto GetTextureViewPool() -> HandlePool<GLTextureViewData, TextureViewTag, kMaxTextureViews>& {
            return textureViews_;
        }
        auto GetSamplerPool() -> HandlePool<GLSamplerData, SamplerTag, kMaxSamplers>& { return samplers_; }
        auto GetPipelinePool() -> HandlePool<GLPipelineData, PipelineTag, kMaxPipelines>& { return pipelines_; }
        auto GetPipelineLayoutPool() -> HandlePool<GLPipelineLayoutData, PipelineLayoutTag, kMaxPipelineLayouts>& {
            return pipelineLayouts_;
        }
        auto GetDescriptorLayoutPool()
            -> HandlePool<GLDescriptorLayoutData, DescriptorLayoutTag, kMaxDescriptorLayouts>& {
            return descriptorLayouts_;
        }
        auto GetDescriptorSetPool() -> HandlePool<GLDescriptorSetData, DescriptorSetTag, kMaxDescriptorSets>& {
            return descriptorSets_;
        }
        auto GetQueryPoolPool() -> HandlePool<GLQueryPoolData, QueryPoolTag, kMaxQueryPools>& { return queryPools_; }
        auto GetCommandBufferPool() -> HandlePool<GLCommandBufferData, CommandBufferTag, kMaxCommandBuffers>& {
            return commandBuffers_;
        }
        auto GetCommandPoolPool() -> HandlePool<GLCommandPoolData, CommandPoolTag, kMaxCommandPools>& {
            return commandPools_;
        }
        auto GetShaderModulePool() -> HandlePool<GLShaderModuleData, ShaderModuleTag, kMaxShaderModules>& {
            return shaderModules_;
        }

        // -- Push constant UBO --
        auto GetPushConstantUBO() const noexcept -> GLuint { return pushConstantUBO_; }

        // -- Extension context --
        [[nodiscard]] auto GetExtContext() const noexcept -> const GLExtContext& { return ext_; }

       private:
        // -- GL context (glad2 MX) --
        GladGLContext* gl_ = nullptr;  // Raw pointer, owned if ownsContext_==true. GLContext wraps this.
        GLContext renderCtx_;          // Primary render context with state cache
        bool ownsContext_ = false;
        platform::IWindowBackend* windowBackend_ = nullptr;
        void* nativeToken_ = nullptr;

        // -- Capabilities --
        GpuCapabilityProfile capabilities_;
        bool hasDSA_ = false;  // GL_ARB_direct_state_access / GL 4.5
        int glMajor_ = 4, glMinor_ = 3;
        GLExtContext ext_;

        // -- Push constant emulation UBO (binding 0, 128B) --
        GLuint pushConstantUBO_ = 0;

        // -- Tracking --
        uint64_t totalAllocatedBytes_ = 0;
        uint32_t totalAllocationCount_ = 0;

        // -- Resource pools (same capacities as other backends) --
        HandlePool<GLBufferData, BufferTag, kMaxBuffers> buffers_;
        HandlePool<GLTextureData, TextureTag, kMaxTextures> textures_;
        HandlePool<GLTextureViewData, TextureViewTag, kMaxTextureViews> textureViews_;
        HandlePool<GLSamplerData, SamplerTag, kMaxSamplers> samplers_;
        HandlePool<GLShaderModuleData, ShaderModuleTag, kMaxShaderModules> shaderModules_;
        HandlePool<GLFenceData, FenceTag, kMaxFences> fences_;
        HandlePool<GLSemaphoreData, SemaphoreTag, kMaxSemaphores> semaphores_;
        HandlePool<GLPipelineData, PipelineTag, kMaxPipelines> pipelines_;
        HandlePool<GLPipelineLayoutData, PipelineLayoutTag, kMaxPipelineLayouts> pipelineLayouts_;
        HandlePool<GLDescriptorLayoutData, DescriptorLayoutTag, kMaxDescriptorLayouts> descriptorLayouts_;
        HandlePool<GLDescriptorSetData, DescriptorSetTag, kMaxDescriptorSets> descriptorSets_;
        HandlePool<GLPipelineCacheData, PipelineCacheTag, kMaxPipelineCaches> pipelineCaches_;
        HandlePool<GLQueryPoolData, QueryPoolTag, kMaxQueryPools> queryPools_;
        HandlePool<GLSwapchainData, SwapchainTag, kMaxSwapchains> swapchains_;
        HandlePool<GLCommandBufferData, CommandBufferTag, kMaxCommandBuffers> commandBuffers_;
        HandlePool<GLCommandPoolData, CommandPoolTag, kMaxCommandPools> commandPools_;

        // -- FBO cache for MRT rendering passes --
        std::unordered_map<GLFBOCacheKey, GLuint, GLFBOCacheKeyHash> fboCache_;

        // -- Utility FBO for transfer operations (CmdCopy, CmdClear, CmdBlit) --
        GLuint utilityFBO_ = 0;

        // -- Init helpers --
        void PopulateCapabilities();
        void PopulateFormatSupport();
        void ProbeVendorMemoryExtensions();

        // -- FBO management --
        void DestroyAllCachedFBOs();

       public:
        auto GetOrCreateMRTFramebuffer(const GLFBOCacheKey& key) -> GLuint;
        auto GetUtilityFBO() noexcept -> GLuint { return utilityFBO_; }
    };

}  // namespace miki::rhi
