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

#include <string>
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

       private:
        using PFNGLDEPTHBOUNDSEXTPROC = void(GLAD_API_PTR*)(GLdouble, GLdouble);
        PFNGLDEPTHBOUNDSEXTPROC pfnDepthBoundsEXT_ = nullptr;
        bool hasDepthBoundsTest_ = false;
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
        bool ownsTexture = true;  // false for swapchain (external FBO)
    };

    struct GLTextureViewData {
        // OpenGL 4.3+ texture views via glTextureView
        GLuint viewTexture = 0;
        TextureHandle parentTexture;
        GLenum target = GL_TEXTURE_2D;
        bool ownsView = false;  // true if created via glTextureView, false if alias
    };

    struct GLSamplerData {
        GLuint sampler = 0;
    };

    struct GLShaderModuleData {
        std::string source;  // GLSL 4.30 source text
        ShaderStage stage = ShaderStage::Vertex;
        GLuint compiledShader = 0;  // Cached compiled GL shader object
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
        TextureHandle colorTexture;  // Wrapper handle for default FB color
        platform::IWindowBackend* windowBackend = nullptr;
        void* nativeToken = nullptr;
        uint32_t currentImage = 0;
    };

    struct GLCommandBufferData {
        QueueType queueType = QueueType::Graphics;
        bool isSecondary = false;
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
        OpenGLDevice() = default;
        ~OpenGLDevice();

        OpenGLDevice(const OpenGLDevice&) = delete;
        auto operator=(const OpenGLDevice&) -> OpenGLDevice& = delete;
        OpenGLDevice(OpenGLDevice&&) = delete;
        auto operator=(OpenGLDevice&&) -> OpenGLDevice& = delete;

        [[nodiscard]] auto Init(const OpenGLDeviceDesc& desc = {}) -> RhiResult<void>;

        // -- Native accessors --
        [[nodiscard]] auto GetGLContext() const noexcept -> GladGLContext* { return gl_; }

        // -- Capability --
        auto GetBackendTypeImpl() const -> BackendType { return BackendType::OpenGL43; }
        auto GetCapabilitiesImpl() const -> const GpuCapabilityProfile& { return capabilities_; }

        // -- Swapchain (OpenGLSwapchain.cpp) --
        auto CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle>;
        void DestroySwapchainImpl(SwapchainHandle h);
        auto ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void>;
        auto AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle fence) -> RhiResult<uint32_t>;
        auto GetSwapchainTextureImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle;
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
        auto GetBufferDeviceAddressImpl(BufferHandle h) -> uint64_t;

        auto CreateTextureImpl(const TextureDesc& desc) -> RhiResult<TextureHandle>;
        auto CreateTextureViewImpl(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle>;
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

        // -- Command buffers (OpenGLQuery.cpp) --
        auto CreateCommandBufferImpl(const CommandBufferDesc& desc) -> RhiResult<CommandBufferHandle>;
        void DestroyCommandBufferImpl(CommandBufferHandle h);

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
        auto GetShaderModulePool() -> HandlePool<GLShaderModuleData, ShaderModuleTag, kMaxShaderModules>& {
            return shaderModules_;
        }

        // -- Push constant UBO --
        auto GetPushConstantUBO() const noexcept -> GLuint { return pushConstantUBO_; }

        // -- Extension context --
        [[nodiscard]] auto GetExtContext() const noexcept -> const GLExtContext& { return ext_; }

       private:
        // -- GL context (glad2 MX) --
        GladGLContext* gl_ = nullptr;
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

        // -- Init helpers --
        void PopulateCapabilities();
    };

}  // namespace miki::rhi
