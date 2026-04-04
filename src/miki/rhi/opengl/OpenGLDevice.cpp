/** @file OpenGLDevice.cpp
 *  @brief OpenGL 4.3+ (Tier 4) backend — glad2 MX context init, capability population,
 *         sync primitives (glFenceSync), submit (immediate), memory stats.
 */

#include "miki/rhi/backend/OpenGLDevice.h"
#include "miki/rhi/backend/OpenGLCommandBuffer.h"

#include "miki/debug/StructuredLogger.h"
#include "miki/platform/WindowManager.h"

#include <cstring>
#include <format>

namespace miki::rhi {

    // =========================================================================
    // GL debug message callback (KHR_debug)
    // =========================================================================

    namespace {
        constexpr auto GLSourceToString(GLenum source) -> std::string_view {
            switch (source) {
                case GL_DEBUG_SOURCE_API: return "API";
                case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "WindowSystem";
                case GL_DEBUG_SOURCE_SHADER_COMPILER: return "ShaderCompiler";
                case GL_DEBUG_SOURCE_THIRD_PARTY: return "ThirdParty";
                case GL_DEBUG_SOURCE_APPLICATION: return "Application";
                case GL_DEBUG_SOURCE_OTHER: return "Other";
                default: return "Unknown";
            }
        }

        constexpr auto GLTypeToString(GLenum type) -> std::string_view {
            switch (type) {
                case GL_DEBUG_TYPE_ERROR: return "Error";
                case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "Deprecated";
                case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "UndefinedBehavior";
                case GL_DEBUG_TYPE_PORTABILITY: return "Portability";
                case GL_DEBUG_TYPE_PERFORMANCE: return "Performance";
                case GL_DEBUG_TYPE_MARKER: return "Marker";
                case GL_DEBUG_TYPE_PUSH_GROUP: return "PushGroup";
                case GL_DEBUG_TYPE_POP_GROUP: return "PopGroup";
                case GL_DEBUG_TYPE_OTHER: return "Other";
                default: return "Unknown";
            }
        }

        void GLAD_API_PTR GLDebugCallback(
            GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/, const GLchar* message,
            const void* /*userParam*/
        ) {
            using enum ::miki::debug::LogCategory;
            auto msg = std::format(
                "[OpenGL] [{}:{}] (id={}) {}", GLSourceToString(source), GLTypeToString(type), id, message
            );

            switch (severity) {
                case GL_DEBUG_SEVERITY_HIGH: MIKI_LOG_ERROR(Rhi, "{}", msg); break;
                case GL_DEBUG_SEVERITY_MEDIUM: MIKI_LOG_WARN(Rhi, "{}", msg); break;
                case GL_DEBUG_SEVERITY_LOW: MIKI_LOG_INFO(Rhi, "{}", msg); break;
                case GL_DEBUG_SEVERITY_NOTIFICATION:
                default: MIKI_LOG_TRACE(Rhi, "{}", msg); break;
            }
            MIKI_LOG_FLUSH();
        }
    }  // namespace

    // =========================================================================
    // GLExtContext — probe and load extension functions not in glad2
    // =========================================================================

    void GLExtContext::Init(GladGLContext* gl, GLADloadfunc loader) {
        if (!loader) {
            return;
        }

        // GL 4.6 has GL_ARB_gl_spirv in core — check version first
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) — GL API returns const GLubyte*
        const char* versionStr = reinterpret_cast<const char*>(gl->GetString(GL_VERSION));
        int glMajor = 0, glMinor = 0;
        if (versionStr) {
            char* end = nullptr;
            constexpr int kBase10 = 10;
            glMajor = static_cast<int>(std::strtol(versionStr, &end, kBase10));
            if (end && *end == '.') {
                glMinor = static_cast<int>(std::strtol(end + 1, nullptr, kBase10));
            }
        }
        constexpr int kGL46Minor = 6;
        bool isGL46 = (glMajor > 4 || (glMajor == 4 && glMinor >= kGL46Minor));

        bool foundDepthBounds = false;
        bool foundArbGlSpirv = isGL46;

        GLint numExtensions = 0;
        gl->GetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
        for (GLint i = 0; i < numExtensions; ++i) {
            const char* ext = reinterpret_cast<const char*>(gl->GetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            if (!ext) {
                continue;
            }
            if (!foundDepthBounds && std::strcmp(ext, "GL_EXT_depth_bounds_test") == 0) {
                pfnDepthBoundsEXT_ = reinterpret_cast<PFNGLDEPTHBOUNDSEXTPROC>(loader("glDepthBoundsEXT"));
                hasDepthBoundsTest_ = (pfnDepthBoundsEXT_ != nullptr);
                foundDepthBounds = true;
            }
            if (!foundArbGlSpirv && std::strcmp(ext, "GL_ARB_gl_spirv") == 0) {
                foundArbGlSpirv = true;
            }
            if (foundDepthBounds && foundArbGlSpirv) {
                break;
            }
        }

        // GL_ARB_gl_spirv requires both glShaderBinary (GL 4.1 core) and glSpecializeShader
        if (foundArbGlSpirv) {
            pfnSpecializeShader_ = reinterpret_cast<PFNGLSPECIALIZESHADERPROC>(loader("glSpecializeShader"));
            if (!pfnSpecializeShader_) {
                pfnSpecializeShader_ = reinterpret_cast<PFNGLSPECIALIZESHADERPROC>(loader("glSpecializeShaderARB"));
            }
            hasSpirvSupport_ = (pfnSpecializeShader_ != nullptr) && (gl->ShaderBinary != nullptr);
        }
    }

    // =========================================================================
    // Init / Destroy
    // =========================================================================

    auto OpenGLDevice::Init(const OpenGLDeviceDesc& desc) -> RhiResult<void> {
        gl_ = new GladGLContext{};
        ownsContext_ = true;

        windowBackend_ = desc.windowBackend;
        nativeToken_ = desc.nativeToken;

        // glad2 MX: load GL function pointers via gladLoaderLoadGLContext (uses platform loader)
        // The GL context must already be current on this thread before calling.
        int version = gladLoaderLoadGLContext(gl_);
        if (version == 0) {
            delete gl_;
            gl_ = nullptr;
            return std::unexpected(RhiError::DeviceLost);
        }
        glMajor_ = GLAD_VERSION_MAJOR(version);
        glMinor_ = GLAD_VERSION_MINOR(version);

        if (glMajor_ < 4 || (glMajor_ == 4 && glMinor_ < 3)) {
            delete gl_;
            gl_ = nullptr;
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        hasDSA_ = (glMajor_ > 4 || (glMajor_ == 4 && glMinor_ >= 5)) || gl_->ARB_direct_state_access;
        renderCtx_.Init(gl_);

        // Probe extensions not covered by glad2 (GL_ARB_gl_spirv, GL_EXT_depth_bounds_test)
        GLADloadfunc procLoader = nullptr;
        if (windowBackend_) {
            procLoader = reinterpret_cast<GLADloadfunc>(windowBackend_->GetGLProcLoader());
        }
        ext_.Init(gl_, procLoader);

        // Enable debug output if validation requested
        if (desc.enableValidation && gl_->KHR_debug) {
            gl_->Enable(GL_DEBUG_OUTPUT);
            gl_->Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            gl_->DebugMessageCallback(GLDebugCallback, nullptr);
            // Accept all messages — filtering is done by StructuredLogger category level
            gl_->DebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        }

        // Adaptation: §20b Feature::PushConstants → Strategy::UboEmulation
        // OpenGL has no native push constants. Emulated via 128B UBO at binding 0.
        // User UBO bindings shifted +1 to avoid collision.
        if (hasDSA_) {
            gl_->CreateBuffers(1, &pushConstantUBO_);
            gl_->NamedBufferStorage(pushConstantUBO_, 128, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
        } else {
            gl_->GenBuffers(1, &pushConstantUBO_);
            gl_->BindBuffer(GL_UNIFORM_BUFFER, pushConstantUBO_);
            gl_->BufferStorage(GL_UNIFORM_BUFFER, 128, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
            gl_->BindBuffer(GL_UNIFORM_BUFFER, 0);
        }
        gl_->BindBufferBase(GL_UNIFORM_BUFFER, 0, pushConstantUBO_);

        // Create utility FBO for transfer operations (CmdCopy, CmdClear, CmdBlit)
        gl_->GenFramebuffers(1, &utilityFBO_);

        PopulateCapabilities();

        MIKI_LOG_INFO(
            ::miki::debug::LogCategory::Rhi, "[OpenGL] Device initialized: {} (GL {}.{}, SPIR-V: {})",
            capabilities_.deviceName, glMajor_, glMinor_, ext_.HasSpirvSupport() ? "yes" : "no"
        );

        return {};
    }

    OpenGLDevice::OpenGLDevice() = default;

    OpenGLDevice::~OpenGLDevice() {
        if (gl_) {
            WaitIdleImpl();

            DestroyAllCachedFBOs();

            if (utilityFBO_) {
                gl_->DeleteFramebuffers(1, &utilityFBO_);
                utilityFBO_ = 0;
            }

            if (pushConstantUBO_) {
                gl_->DeleteBuffers(1, &pushConstantUBO_);
                pushConstantUBO_ = 0;
            }

            if (ownsContext_) {
                delete gl_;
            }
            gl_ = nullptr;
        }
    }

    // =========================================================================
    // FBO cache for MRT (multi-color-attachment) rendering passes
    // =========================================================================

    auto OpenGLDevice::GetOrCreateMRTFramebuffer(const GLFBOCacheKey& key) -> GLuint {
        auto it = fboCache_.find(key);
        if (it != fboCache_.end()) {
            return it->second;
        }

        GLuint fbo = 0;
        gl_->GenFramebuffers(1, &fbo);
        gl_->BindFramebuffer(GL_FRAMEBUFFER, fbo);

        std::array<GLenum, GLFBOCacheKey::kMaxCachedAttachments> drawBuffers{};
        for (uint32_t i = 0; i < key.colorCount; ++i) {
            auto attachPoint = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
            gl_->FramebufferTexture(GL_FRAMEBUFFER, attachPoint, key.colorTextures[i], 0);
            drawBuffers[i] = attachPoint;
        }
        gl_->DrawBuffers(static_cast<GLsizei>(key.colorCount), drawBuffers.data());

        if (key.depthTexture) {
            gl_->FramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, key.depthTexture, 0);
        }
        if (key.stencilTexture) {
            gl_->FramebufferTexture(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, key.stencilTexture, 0);
        }

        gl_->BindFramebuffer(GL_FRAMEBUFFER, 0);
        fboCache_.emplace(key, fbo);
        return fbo;
    }

    void OpenGLDevice::DestroyAllCachedFBOs() {
        for (auto& [key, fbo] : fboCache_) {
            if (fbo) {
                gl_->DeleteFramebuffers(1, &fbo);
            }
        }
        fboCache_.clear();
    }

    // =========================================================================
    // Capability population
    // =========================================================================

    void OpenGLDevice::PopulateCapabilities() {
        capabilities_.tier = CapabilityTier::Tier4_OpenGL;
        capabilities_.backendType = BackendType::OpenGL43;

        const char* renderer = reinterpret_cast<const char*>(gl_->GetString(GL_RENDERER));
        capabilities_.deviceName = renderer ? renderer : "Unknown OpenGL Device";

        const char* vendor = reinterpret_cast<const char*>(gl_->GetString(GL_VENDOR));
        (void)vendor;

        // OpenGL has single queue, no async compute, no ray tracing
        capabilities_.hasTimelineSemaphore = false;
        capabilities_.hasAsyncCompute = false;
        capabilities_.hasAsyncTransfer = false;
        capabilities_.hasMultiDrawIndirect = true;        // GL 4.3 core
        capabilities_.hasMultiDrawIndirectCount = false;  // Requires GL_ARB_indirect_parameters
        capabilities_.hasBindless = false;                // GL_ARB_bindless_texture — not guaranteed
        capabilities_.hasSubgroupOps = false;
        capabilities_.hasMeshShader = false;
        capabilities_.hasTaskShader = false;
        capabilities_.hasRayQuery = false;
        capabilities_.hasRayTracingPipeline = false;
        capabilities_.hasAccelerationStructure = false;
        capabilities_.hasSpirvShaders = ext_.HasSpirvSupport();
        capabilities_.hasVariableRateShading = false;
        capabilities_.hasSparseBinding = false;
        capabilities_.hasWorkGraphs = false;
        capabilities_.hasCooperativeMatrix = false;
        capabilities_.hasHardwareDecompression = false;
        capabilities_.hasResizableBAR = false;

        // Descriptor model
        capabilities_.descriptorModel = DescriptorModel::DirectBind;

        // Limits from GL
        GLint maxTexSize = 0;
        gl_->GetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
        capabilities_.maxTextureSize2D = static_cast<uint32_t>(maxTexSize);

        GLint maxCubeSize = 0;
        gl_->GetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCubeSize);
        capabilities_.maxTextureSizeCube = static_cast<uint32_t>(maxCubeSize);

        GLint maxColorAttach = 0;
        gl_->GetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttach);
        capabilities_.maxColorAttachments = static_cast<uint32_t>(maxColorAttach);

        GLint maxViewports = 0;
        gl_->GetIntegerv(GL_MAX_VIEWPORTS, &maxViewports);
        capabilities_.maxViewports = static_cast<uint32_t>(maxViewports);

        capabilities_.maxPushConstantSize = 128;  // Emulated via UBO
        capabilities_.maxBoundDescriptorSets = 4;
        capabilities_.maxDrawIndirectCount = UINT32_MAX;

        GLint maxSSBOSize = 0;
        gl_->GetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxSSBOSize);
        capabilities_.maxStorageBufferSize = static_cast<uint64_t>(maxSSBOSize);

        GLint maxFBWidth = 0, maxFBHeight = 0;
        gl_->GetIntegerv(GL_MAX_FRAMEBUFFER_WIDTH, &maxFBWidth);
        gl_->GetIntegerv(GL_MAX_FRAMEBUFFER_HEIGHT, &maxFBHeight);
        capabilities_.maxFramebufferWidth = static_cast<uint32_t>(maxFBWidth);
        capabilities_.maxFramebufferHeight = static_cast<uint32_t>(maxFBHeight);

        GLint maxWorkGroupCount[3]{};
        gl_->GetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxWorkGroupCount[0]);
        gl_->GetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxWorkGroupCount[1]);
        gl_->GetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxWorkGroupCount[2]);
        capabilities_.maxComputeWorkGroupCount = {
            static_cast<uint32_t>(maxWorkGroupCount[0]),
            static_cast<uint32_t>(maxWorkGroupCount[1]),
            static_cast<uint32_t>(maxWorkGroupCount[2]),
        };

        GLint maxWorkGroupSize[3]{};
        gl_->GetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxWorkGroupSize[0]);
        gl_->GetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &maxWorkGroupSize[1]);
        gl_->GetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &maxWorkGroupSize[2]);
        capabilities_.maxComputeWorkGroupSize = {
            static_cast<uint32_t>(maxWorkGroupSize[0]),
            static_cast<uint32_t>(maxWorkGroupSize[1]),
            static_cast<uint32_t>(maxWorkGroupSize[2]),
        };

        capabilities_.subgroupSize = 0;   // Not exposed on GL
        capabilities_.hasFloat64 = true;  // GL 4.0 core

        // Memory — GL doesn't expose memory budgets; best-effort via extensions
        capabilities_.hasMemoryBudgetQuery = false;
        capabilities_.deviceLocalMemoryBytes = 0;
        capabilities_.hostVisibleMemoryBytes = 0;

        // Check NV/ATI memory info extensions (probe may generate expected GL_INVALID_ENUM)
        ProbeVendorMemoryExtensions();

        // Probe non-glad2 extensions
        auto loader = windowBackend_ ? reinterpret_cast<GLADloadfunc>(windowBackend_->GetGLProcLoader()) : nullptr;
        ext_.Init(gl_, loader);

        // Always-on features
        capabilities_.enabledFeatures.Add(DeviceFeature::Present);
        capabilities_.enabledFeatures.Add(DeviceFeature::DynamicRendering);

        // Runtime format support probe
        PopulateFormatSupport();
    }

    void OpenGLDevice::PopulateFormatSupport() {
        // GL internal format indexed by miki::rhi::Format enum value
        static constexpr GLenum kFormatMap[] = {
            0,                      // Undefined
            GL_R8,                  // R8_UNORM
            GL_R8_SNORM,            // R8_SNORM
            GL_R8UI,                // R8_UINT
            GL_R8I,                 // R8_SINT
            GL_RG8,                 // RG8_UNORM
            GL_RG8_SNORM,           // RG8_SNORM
            GL_RG8UI,               // RG8_UINT
            GL_RG8I,                // RG8_SINT
            GL_RGBA8,               // RGBA8_UNORM
            GL_RGBA8_SNORM,         // RGBA8_SNORM
            GL_RGBA8UI,             // RGBA8_UINT
            GL_RGBA8I,              // RGBA8_SINT
            GL_SRGB8_ALPHA8,        // RGBA8_SRGB
            GL_RGBA8,               // BGRA8_UNORM (GL uses RGBA8 + GL_BGRA swizzle)
            GL_SRGB8_ALPHA8,        // BGRA8_SRGB
            GL_R16,                 // R16_UNORM
            GL_R16_SNORM,           // R16_SNORM
            GL_R16UI,               // R16_UINT
            GL_R16I,                // R16_SINT
            GL_R16F,                // R16_FLOAT
            GL_RG16,                // RG16_UNORM
            GL_RG16_SNORM,          // RG16_SNORM
            GL_RG16UI,              // RG16_UINT
            GL_RG16I,               // RG16_SINT
            GL_RG16F,               // RG16_FLOAT
            GL_RGBA16,              // RGBA16_UNORM
            GL_RGBA16_SNORM,        // RGBA16_SNORM
            GL_RGBA16UI,            // RGBA16_UINT
            GL_RGBA16I,             // RGBA16_SINT
            GL_RGBA16F,             // RGBA16_FLOAT
            GL_R32UI,               // R32_UINT
            GL_R32I,                // R32_SINT
            GL_R32F,                // R32_FLOAT
            GL_RG32UI,              // RG32_UINT
            GL_RG32I,               // RG32_SINT
            GL_RG32F,               // RG32_FLOAT
            GL_RGB32UI,             // RGB32_UINT
            GL_RGB32I,              // RGB32_SINT
            GL_RGB32F,              // RGB32_FLOAT
            GL_RGBA32UI,            // RGBA32_UINT
            GL_RGBA32I,             // RGBA32_SINT
            GL_RGBA32F,             // RGBA32_FLOAT
            GL_RGB10_A2,            // RGB10A2_UNORM
            GL_R11F_G11F_B10F,      // RG11B10_FLOAT
            GL_DEPTH_COMPONENT16,   // D16_UNORM
            GL_DEPTH_COMPONENT32F,  // D32_FLOAT
            GL_DEPTH24_STENCIL8,    // D24_UNORM_S8_UINT
            GL_DEPTH32F_STENCIL8,   // D32_FLOAT_S8_UINT
            0x83F1,                 // BC1_UNORM
            0x8C4D,                 // BC1_SRGB
            0x83F2,                 // BC2_UNORM
            0x8C4E,                 // BC2_SRGB
            0x83F3,                 // BC3_UNORM
            0x8C4F,                 // BC3_SRGB
            0x8DBB,                 // BC4_UNORM
            0x8DBC,                 // BC4_SNORM
            0x8DBD,                 // BC5_UNORM
            0x8DBE,                 // BC5_SNORM
            0x8E8F,                 // BC6H_UFLOAT
            0x8E8E,                 // BC6H_SFLOAT
            0x8E8C,                 // BC7_UNORM
            0x8E8D,                 // BC7_SRGB
            0,                      // ASTC_4x4_UNORM (not core GL 4.3)
            0,                      // ASTC_4x4_SRGB
        };
        static_assert(sizeof(kFormatMap) / sizeof(kFormatMap[0]) == GpuCapabilityProfile::kFormatCount);

        for (uint32_t i = 1; i < GpuCapabilityProfile::kFormatCount; ++i) {
            if (kFormatMap[i] == 0) {
                capabilities_.formatSupport[i] = FormatFeatureFlags::None;
                continue;
            }

            GLenum glFmt = kFormatMap[i];
            auto info = FormatInfo(static_cast<Format>(i));

            // Check if format is supported at all (GL 4.3 core: glGetInternalformativ)
            GLint supported = 0;
            gl_->GetInternalformativ(GL_TEXTURE_2D, glFmt, GL_INTERNALFORMAT_SUPPORTED, 1, &supported);
            if (gl_->GetError() != GL_NO_ERROR || supported != GL_TRUE) {
                capabilities_.formatSupport[i] = FormatFeatureFlags::None;
                continue;
            }

            FormatFeatureFlags flags = FormatFeatureFlags::Sampled;  // If supported, always samplable

            // Renderable (color attachment or depth/stencil)
            GLint renderable = 0;
            gl_->GetInternalformativ(GL_TEXTURE_2D, glFmt, GL_FRAMEBUFFER_RENDERABLE, 1, &renderable);
            if (gl_->GetError() == GL_NO_ERROR && renderable == static_cast<GLint>(GL_FULL_SUPPORT)) {
                if (info.isDepth || info.isStencil) {
                    flags = flags | FormatFeatureFlags::DepthStencil;
                } else if (!info.isCompressed) {
                    flags = flags | FormatFeatureFlags::ColorAttachment;
                }
            }

            // Blendable
            if (!info.isDepth && !info.isStencil && !info.isCompressed) {
                GLint blendable = 0;
                gl_->GetInternalformativ(GL_TEXTURE_2D, glFmt, GL_FRAMEBUFFER_BLEND, 1, &blendable);
                if (gl_->GetError() == GL_NO_ERROR && blendable == static_cast<GLint>(GL_FULL_SUPPORT)) {
                    flags = flags | FormatFeatureFlags::BlendSrc;
                }
            }

            // Image load/store (storage)
            if (!info.isCompressed && !info.isDepth) {
                GLint imageLoad = 0;
                gl_->GetInternalformativ(GL_TEXTURE_2D, glFmt, GL_SHADER_IMAGE_LOAD, 1, &imageLoad);
                if (gl_->GetError() == GL_NO_ERROR && imageLoad == static_cast<GLint>(GL_FULL_SUPPORT)) {
                    flags = flags | FormatFeatureFlags::Storage;
                }
            }

            // Linear filtering
            GLint filterable = 0;
            gl_->GetInternalformativ(GL_TEXTURE_2D, glFmt, GL_FILTER, 1, &filterable);
            if (gl_->GetError() == GL_NO_ERROR && filterable == static_cast<GLint>(GL_FULL_SUPPORT)) {
                flags = flags | FormatFeatureFlags::Filter;
            }

            capabilities_.formatSupport[i] = flags;
        }
        // Clear any accumulated GL errors from probing
        while (gl_->GetError() != GL_NO_ERROR) {}
    }

    void OpenGLDevice::ProbeVendorMemoryExtensions() {
        // RAII guard to suppress expected GL_INVALID_ENUM on non-NVIDIA/AMD GPUs
        struct DebugOutputGuard {
            GladGLContext* gl;
            bool wasEnabled;
            explicit DebugOutputGuard(GladGLContext* ctx) : gl(ctx), wasEnabled(ctx->KHR_debug) {
                if (wasEnabled) {
                    gl->Disable(GL_DEBUG_OUTPUT);
                }
            }
            ~DebugOutputGuard() {
                if (wasEnabled) {
                    gl->Enable(GL_DEBUG_OUTPUT);
                }
            }
        } guard(gl_);

        // GL_NVX_gpu_memory_info (NVIDIA-only)
        constexpr GLenum GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX = 0x9048;
        GLint totalMemKB = 0;
        gl_->GetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemKB);
        if (gl_->GetError() == GL_NO_ERROR && totalMemKB > 0) {
            capabilities_.deviceLocalMemoryBytes = static_cast<uint64_t>(totalMemKB) * 1024;
            capabilities_.hasMemoryBudgetQuery = true;
            return;
        }

        // GL_ATI_meminfo (AMD-only)
        // VBO_FREE_MEMORY_ATI returns [total, largest block, total aux, largest aux block]
        constexpr GLenum GL_VBO_FREE_MEMORY_ATI = 0x87FB;
        GLint atiMemInfo[4]{};
        gl_->GetIntegerv(GL_VBO_FREE_MEMORY_ATI, atiMemInfo);
        if (gl_->GetError() == GL_NO_ERROR && atiMemInfo[0] > 0) {
            capabilities_.deviceLocalMemoryBytes = static_cast<uint64_t>(atiMemInfo[0]) * 1024;
            capabilities_.hasMemoryBudgetQuery = true;
        }
    }

    // =========================================================================
    // Sync primitives — glFenceSync based
    // =========================================================================

    auto OpenGLDevice::CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle> {
        auto [handle, data] = fences_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->sync = nullptr;
        data->signaled = signaled;
        return handle;
    }

    void OpenGLDevice::DestroyFenceImpl(FenceHandle h) {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->sync) {
            gl_->DeleteSync(data->sync);
        }
        fences_.Free(h);
    }

    void OpenGLDevice::WaitFenceImpl(FenceHandle h, uint64_t timeout) {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->signaled) {
            return;
        }
        if (data->sync) {
            gl_->ClientWaitSync(data->sync, GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
            data->signaled = true;
        }
    }

    void OpenGLDevice::ResetFenceImpl(FenceHandle h) {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->sync) {
            gl_->DeleteSync(data->sync);
            data->sync = nullptr;
        }
        data->signaled = false;
    }

    auto OpenGLDevice::GetFenceStatusImpl(FenceHandle h) -> bool {
        auto* data = fences_.Lookup(h);
        if (!data) {
            return false;
        }
        if (data->signaled) {
            return true;
        }
        if (data->sync) {
            GLint status = GL_UNSIGNALED;
            GLsizei length = 0;
            gl_->GetSynciv(data->sync, GL_SYNC_STATUS, sizeof(status), &length, &status);
            if (status == GL_SIGNALED) {
                data->signaled = true;
                return true;
            }
        }
        return false;
    }

    auto OpenGLDevice::CreateSemaphoreImpl(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle> {
        auto [handle, data] = semaphores_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->sync = nullptr;
        data->value = desc.initialValue;
        data->type = desc.type;
        return handle;
    }

    void OpenGLDevice::DestroySemaphoreImpl(SemaphoreHandle h) {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->sync) {
            gl_->DeleteSync(data->sync);
        }
        semaphores_.Free(h);
    }

    void OpenGLDevice::SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value) {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->sync) {
            gl_->DeleteSync(data->sync);
        }
        data->sync = gl_->FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        data->value = value;
    }

    void OpenGLDevice::WaitSemaphoreImpl(SemaphoreHandle h, uint64_t /*value*/, uint64_t timeout) {
        auto* data = semaphores_.Lookup(h);
        if (!data || !data->sync) {
            return;
        }
        gl_->ClientWaitSync(data->sync, GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
    }

    auto OpenGLDevice::GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t {
        auto* data = semaphores_.Lookup(h);
        if (!data) {
            return 0;
        }
        return data->value;
    }

    void OpenGLDevice::WaitIdleImpl() {
        if (gl_) {
            gl_->Finish();
        }
    }

    // =========================================================================
    // Submit — OpenGL is immediate mode, commands already executed
    // =========================================================================

    void OpenGLDevice::SubmitImpl(QueueType /*queue*/, const SubmitDesc& desc) {
        // OpenGL commands are immediate — no deferred submission.
        // Signal fence after commands are flushed.
        if (desc.signalFence.IsValid()) {
            auto* fenceData = fences_.Lookup(desc.signalFence);
            if (fenceData) {
                if (fenceData->sync) {
                    gl_->DeleteSync(fenceData->sync);
                }
                fenceData->sync = gl_->FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                fenceData->signaled = false;
            }
        }

        // Signal semaphores
        for (auto& s : desc.signalSemaphores) {
            auto* semData = semaphores_.Lookup(s.semaphore);
            if (semData) {
                if (semData->sync) {
                    gl_->DeleteSync(semData->sync);
                }
                semData->sync = gl_->FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                semData->value = s.value;
            }
        }

        gl_->Flush();
    }

    // =========================================================================
    // Memory stats
    // =========================================================================

    auto OpenGLDevice::GetMemoryStatsImpl() const -> MemoryStats {
        MemoryStats result{};
        result.totalAllocationCount = totalAllocationCount_;
        result.totalAllocatedBytes = totalAllocatedBytes_;
        result.totalUsedBytes = totalAllocatedBytes_;
        result.heapCount = capabilities_.hasMemoryBudgetQuery ? 1 : 0;
        return result;
    }

    auto OpenGLDevice::GetMemoryHeapBudgetsImpl(std::span<MemoryHeapBudget> out) const -> uint32_t {
        if (out.empty() || !capabilities_.hasMemoryBudgetQuery) {
            return 0;
        }

        // GL_NVX_gpu_memory_info
        constexpr GLenum GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX = 0x9049;
        constexpr GLenum GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX = 0x9048;

        GLint totalKB = 0, availKB = 0;
        gl_->GetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalKB);
        gl_->GetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availKB);

        if (gl_->GetError() != GL_NO_ERROR) {
            return 0;
        }

        out[0].heapIndex = 0;
        out[0].budgetBytes = static_cast<uint64_t>(totalKB) * 1024;
        out[0].usageBytes = static_cast<uint64_t>(totalKB - availKB) * 1024;
        out[0].isDeviceLocal = true;
        return 1;
    }

    // =========================================================================
    // Adaptation: §20b Feature::SparseBinding → Strategy::Unsupported
    // =========================================================================
    auto OpenGLDevice::GetSparsePageSizeImpl() const -> SparsePageSize {
        assert(false && "Sparse binding not available on OpenGL");
        return {};
    }

    void OpenGLDevice::SubmitSparseBindsImpl(
        QueueType, const SparseBindDesc&, std::span<const SemaphoreSubmitInfo>, std::span<const SemaphoreSubmitInfo>
    ) {
        assert(false && "Sparse binding not available on OpenGL");
    }

    // =========================================================================
    // Adaptation: §20b Feature::RayTracing → Strategy::Unsupported
    // =========================================================================

    auto OpenGLDevice::GetBLASBuildSizesImpl(const BLASDesc&) -> AccelStructBuildSizes {
        assert(false && "Acceleration structures not available on OpenGL");
        return {};
    }
    auto OpenGLDevice::GetTLASBuildSizesImpl(const TLASDesc&) -> AccelStructBuildSizes {
        assert(false && "Acceleration structures not available on OpenGL");
        return {};
    }

    auto OpenGLDevice::CreateBLASImpl(const BLASDesc&) -> RhiResult<AccelStructHandle> {
        assert(false && "Acceleration structures not available on OpenGL");
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    auto OpenGLDevice::CreateTLASImpl(const TLASDesc&) -> RhiResult<AccelStructHandle> {
        assert(false && "Acceleration structures not available on OpenGL");
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    void OpenGLDevice::DestroyAccelStructImpl(AccelStructHandle) {
        assert(false && "Acceleration structures not available on OpenGL");
    }

}  // namespace miki::rhi
