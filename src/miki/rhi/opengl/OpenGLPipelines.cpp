/** @file OpenGLPipelines.cpp
 *  @brief OpenGL 4.3+ (Tier 4) backend — Graphics Pipeline (glCreateProgram + VAO),
 *         Compute Pipeline, PipelineCache (no native support), PipelineLibrary (no split).
 *
 *  Graphics pipeline = linked GL program + cached fixed-function state + VAO.
 *  Compute pipeline = linked GL program (compute shader only).
 *  Ray tracing pipeline = not supported on T4.
 */

#include "miki/rhi/backend/OpenGLDevice.h"

#include "miki/debug/StructuredLogger.h"

#include <cassert>

// GL_ARB_gl_spirv binary format constant (not in glad2 generated headers)
#ifndef GL_SHADER_BINARY_FORMAT_SPIR_V
#    define GL_SHADER_BINARY_FORMAT_SPIR_V 0x9551
#endif

namespace miki::rhi {

    namespace {
        [[maybe_unused]] auto ToGLShaderType(ShaderStage stage) -> GLenum {
            switch (stage) {
                case ShaderStage::Vertex: return GL_VERTEX_SHADER;
                case ShaderStage::Fragment: return GL_FRAGMENT_SHADER;
                case ShaderStage::Compute: return GL_COMPUTE_SHADER;
                default: return GL_VERTEX_SHADER;
            }
        }

        auto ToGLTopology(PrimitiveTopology topo) -> GLenum {
            switch (topo) {
                case PrimitiveTopology::PointList: return GL_POINTS;
                case PrimitiveTopology::LineList: return GL_LINES;
                case PrimitiveTopology::LineStrip: return GL_LINE_STRIP;
                case PrimitiveTopology::TriangleList: return GL_TRIANGLES;
                case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
                case PrimitiveTopology::TriangleFan: return GL_TRIANGLE_FAN;
                case PrimitiveTopology::PatchList: return GL_PATCHES;
            }
            return GL_TRIANGLES;
        }

        auto ToGLCullMode(CullMode mode) -> GLenum {
            switch (mode) {
                case CullMode::None: return GL_NONE;
                case CullMode::Front: return GL_FRONT;
                case CullMode::Back: return GL_BACK;
                case CullMode::FrontAndBack: return GL_FRONT_AND_BACK;
            }
            return GL_BACK;
        }

        auto ToGLPolygonMode(PolygonMode mode) -> GLenum {
            switch (mode) {
                case PolygonMode::Fill: return GL_FILL;
                case PolygonMode::Line: return GL_LINE;
                case PolygonMode::Point: return GL_POINT;
            }
            return GL_FILL;
        }

        auto ToGLCompareFunc(CompareOp op) -> GLenum {
            switch (op) {
                case CompareOp::Never: return GL_NEVER;
                case CompareOp::Less: return GL_LESS;
                case CompareOp::Equal: return GL_EQUAL;
                case CompareOp::LessOrEqual: return GL_LEQUAL;
                case CompareOp::Greater: return GL_GREATER;
                case CompareOp::NotEqual: return GL_NOTEQUAL;
                case CompareOp::GreaterOrEqual: return GL_GEQUAL;
                case CompareOp::Always: return GL_ALWAYS;
                case CompareOp::None: return GL_ALWAYS;
            }
            return GL_ALWAYS;
        }

        auto ToGLStencilOp(StencilOp op) -> GLenum {
            switch (op) {
                case StencilOp::Keep: return GL_KEEP;
                case StencilOp::Zero: return GL_ZERO;
                case StencilOp::Replace: return GL_REPLACE;
                case StencilOp::IncrementAndClamp: return GL_INCR;
                case StencilOp::DecrementAndClamp: return GL_DECR;
                case StencilOp::Invert: return GL_INVERT;
                case StencilOp::IncrementAndWrap: return GL_INCR_WRAP;
                case StencilOp::DecrementAndWrap: return GL_DECR_WRAP;
            }
            return GL_KEEP;
        }

        auto ToGLBlendFactor(BlendFactor f) -> GLenum {
            switch (f) {
                case BlendFactor::Zero: return GL_ZERO;
                case BlendFactor::One: return GL_ONE;
                case BlendFactor::SrcColor: return GL_SRC_COLOR;
                case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
                case BlendFactor::DstColor: return GL_DST_COLOR;
                case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
                case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
                case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
                case BlendFactor::DstAlpha: return GL_DST_ALPHA;
                case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
                case BlendFactor::ConstantColor: return GL_CONSTANT_COLOR;
                case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
                case BlendFactor::SrcAlphaSaturate: return GL_SRC_ALPHA_SATURATE;
            }
            return GL_ZERO;
        }

        auto ToGLBlendOp(BlendOp op) -> GLenum {
            switch (op) {
                case BlendOp::Add: return GL_FUNC_ADD;
                case BlendOp::Subtract: return GL_FUNC_SUBTRACT;
                case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
                case BlendOp::Min: return GL_MIN;
                case BlendOp::Max: return GL_MAX;
            }
            return GL_FUNC_ADD;
        }

        struct GLVertexFormatInfo {
            GLenum type;
            GLint components;
            bool normalized;
        };

        auto ToGLVertexFormat(Format fmt) -> GLVertexFormatInfo {
            switch (fmt) {
                case Format::R32_FLOAT: return {.type = GL_FLOAT, .components = 1, .normalized = false};
                case Format::RG32_FLOAT: return {.type = GL_FLOAT, .components = 2, .normalized = false};
                case Format::RGB32_FLOAT: return {.type = GL_FLOAT, .components = 3, .normalized = false};
                case Format::RGBA32_FLOAT: return {.type = GL_FLOAT, .components = 4, .normalized = false};
                case Format::R32_UINT: return {.type = GL_UNSIGNED_INT, .components = 1, .normalized = false};
                case Format::RG32_UINT: return {.type = GL_UNSIGNED_INT, .components = 2, .normalized = false};
                case Format::RGB32_UINT: return {.type = GL_UNSIGNED_INT, .components = 3, .normalized = false};
                case Format::RGBA32_UINT: return {.type = GL_UNSIGNED_INT, .components = 4, .normalized = false};
                case Format::R32_SINT: return {.type = GL_INT, .components = 1, .normalized = false};
                case Format::RG32_SINT: return {.type = GL_INT, .components = 2, .normalized = false};
                case Format::RGB32_SINT: return {.type = GL_INT, .components = 3, .normalized = false};
                case Format::RGBA32_SINT: return {.type = GL_INT, .components = 4, .normalized = false};
                case Format::R16_FLOAT: return {.type = GL_HALF_FLOAT, .components = 1, .normalized = false};
                case Format::RG16_FLOAT: return {.type = GL_HALF_FLOAT, .components = 2, .normalized = false};
                case Format::RGBA16_FLOAT: return {.type = GL_HALF_FLOAT, .components = 4, .normalized = false};
                case Format::R8_UNORM: return {.type = GL_UNSIGNED_BYTE, .components = 1, .normalized = true};
                case Format::RG8_UNORM: return {.type = GL_UNSIGNED_BYTE, .components = 2, .normalized = true};
                case Format::RGBA8_UNORM: return {.type = GL_UNSIGNED_BYTE, .components = 4, .normalized = true};
                case Format::R16_UNORM: return {.type = GL_UNSIGNED_SHORT, .components = 1, .normalized = true};
                case Format::RG16_UNORM: return {.type = GL_UNSIGNED_SHORT, .components = 2, .normalized = true};
                case Format::RGBA16_UNORM: return {.type = GL_UNSIGNED_SHORT, .components = 4, .normalized = true};
                default: return {.type = GL_FLOAT, .components = 4, .normalized = false};
            }
        }

        auto CompileShaderGLSL(GladGLContext* gl, GLuint& cachedShader, const std::string& source, GLenum type)
            -> GLuint {
            if (cachedShader != 0) {
                return cachedShader;
            }
            GLuint shader = gl->CreateShader(type);
            const char* src = source.c_str();
            gl->ShaderSource(shader, 1, &src, nullptr);
            gl->CompileShader(shader);

            GLint success = 0;
            gl->GetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                char log[1024]{};
                gl->GetShaderInfoLog(shader, sizeof(log), nullptr, log);
                MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "[OpenGL] GLSL compile error: {}", log);
                gl->DeleteShader(shader);
                return 0;
            }
            cachedShader = shader;
            return shader;
        }

        auto CompileShaderSPIRV(
            GladGLContext* gl, const GLExtContext& ext, GLuint& cachedShader, const std::vector<uint8_t>& spirvData,
            const std::string& entryPoint, GLenum type
        ) -> GLuint {
            if (cachedShader != 0) {
                return cachedShader;
            }
            GLuint shader = gl->CreateShader(type);
            gl->ShaderBinary(
                1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V, spirvData.data(), static_cast<GLsizei>(spirvData.size())
            );
            ext.SpecializeShader(shader, entryPoint.c_str(), 0, nullptr, nullptr);

            GLint success = 0;
            gl->GetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                char log[1024]{};
                gl->GetShaderInfoLog(shader, sizeof(log), nullptr, log);
                MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "[OpenGL] SPIR-V specialization error: {}", log);
                gl->DeleteShader(shader);
                return 0;
            }
            cachedShader = shader;
            return shader;
        }
    }  // namespace

    // =========================================================================
    // Graphics Pipeline
    // =========================================================================

    auto OpenGLDevice::CreateGraphicsPipelineImpl(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        GLuint program = gl_->CreateProgram();

        // Compile and attach shaders (SPIR-V or GLSL path)
        auto attachShader = [&](ShaderModuleHandle h, GLenum type) -> bool {
            if (!h.IsValid()) {
                return true;
            }
            auto* mod = shaderModules_.Lookup(h);
            if (!mod) {
                return false;
            }
            GLuint shader = 0;
            if (mod->isSPIRV) {
                shader = CompileShaderSPIRV(gl_, ext_, mod->compiledShader, mod->spirvData, mod->entryPoint, type);
            } else {
                shader = CompileShaderGLSL(gl_, mod->compiledShader, mod->source, type);
            }
            if (!shader) {
                return false;
            }
            gl_->AttachShader(program, shader);
            return true;
        };

        if (!attachShader(desc.vertexShader, GL_VERTEX_SHADER)
            || !attachShader(desc.fragmentShader, GL_FRAGMENT_SHADER)) {
            gl_->DeleteProgram(program);
            return std::unexpected(RhiError::ShaderCompilationFailed);
        }

        gl_->LinkProgram(program);
        GLint linked = 0;
        gl_->GetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024]{};
            gl_->GetProgramInfoLog(program, sizeof(log), nullptr, log);
            MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "[OpenGL] Program link error: {}", log);
            gl_->DeleteProgram(program);
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        // Create VAO for vertex input state
        GLuint vao = 0;
        gl_->GenVertexArrays(1, &vao);
        gl_->BindVertexArray(vao);

        // Set up vertex attributes (format only — buffers bound at draw time)
        for (auto& attr : desc.vertexInput.attributes) {
            GLVertexFormatInfo vfmt = ToGLVertexFormat(attr.format);
            gl_->EnableVertexAttribArray(attr.location);
            if (vfmt.type == GL_INT || vfmt.type == GL_UNSIGNED_INT) {
                gl_->VertexAttribIFormat(attr.location, vfmt.components, vfmt.type, attr.offset);
            } else {
                gl_->VertexAttribFormat(
                    attr.location, vfmt.components, vfmt.type, vfmt.normalized ? GL_TRUE : GL_FALSE, attr.offset
                );
            }
            gl_->VertexAttribBinding(attr.location, attr.binding);
        }

        // Set per-binding divisor (instancing) — once per binding, not per attribute
        for (auto& bind : desc.vertexInput.bindings) {
            if (bind.inputRate == VertexInputRate::PerInstance) {
                gl_->VertexBindingDivisor(bind.binding, 1);
            }
        }

        gl_->BindVertexArray(0);

        // Store pipeline
        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            gl_->DeleteProgram(program);
            gl_->DeleteVertexArrays(1, &vao);
            return std::unexpected(RhiError::TooManyObjects);
        }

        data->program = program;
        data->isCompute = false;
        data->vao = vao;
        data->primitiveTopology = ToGLTopology(desc.topology);
        data->cullMode = ToGLCullMode(desc.cullMode);
        data->frontFace = (desc.frontFace == FrontFace::CounterClockwise) ? GL_CCW : GL_CW;
        data->polygonMode = ToGLPolygonMode(desc.polygonMode);
        data->depthTestEnable = desc.depthTestEnable;
        data->depthWriteEnable = desc.depthWriteEnable;
        data->depthFunc = ToGLCompareFunc(desc.depthCompareOp);
        data->stencilTestEnable = desc.stencilTestEnable;
        data->depthClampEnable = desc.depthClampEnable;
        data->colorAttachmentCount = static_cast<uint32_t>(desc.colorBlends.size());

        // Stencil state
        data->stencilFront
            = {.failOp = ToGLStencilOp(desc.stencilFront.failOp),
               .depthFailOp = ToGLStencilOp(desc.stencilFront.depthFailOp),
               .passOp = ToGLStencilOp(desc.stencilFront.passOp),
               .compareOp = ToGLCompareFunc(desc.stencilFront.compareOp),
               .compareMask = desc.stencilFront.compareMask,
               .writeMask = desc.stencilFront.writeMask};
        data->stencilBack
            = {.failOp = ToGLStencilOp(desc.stencilBack.failOp),
               .depthFailOp = ToGLStencilOp(desc.stencilBack.depthFailOp),
               .passOp = ToGLStencilOp(desc.stencilBack.passOp),
               .compareOp = ToGLCompareFunc(desc.stencilBack.compareOp),
               .compareMask = desc.stencilBack.compareMask,
               .writeMask = desc.stencilBack.writeMask};

        // Blend state
        for (size_t i = 0; i < desc.colorBlends.size() && i < 8; ++i) {
            auto& cb = desc.colorBlends[i];
            data->blendStates[i].enable = cb.blendEnable;
            data->blendStates[i].srcColor = ToGLBlendFactor(cb.srcColor);
            data->blendStates[i].dstColor = ToGLBlendFactor(cb.dstColor);
            data->blendStates[i].colorOp = ToGLBlendOp(cb.colorOp);
            data->blendStates[i].srcAlpha = ToGLBlendFactor(cb.srcAlpha);
            data->blendStates[i].dstAlpha = ToGLBlendFactor(cb.dstAlpha);
            data->blendStates[i].alphaOp = ToGLBlendOp(cb.alphaOp);
            data->blendStates[i].writeMask = static_cast<GLuint>(cb.writeMask);
        }

        // Cache vertex binding info for draw-time buffer binding
        for (auto& bind : desc.vertexInput.bindings) {
            data->vertexBindings.push_back({bind.binding, bind.stride, bind.inputRate == VertexInputRate::PerInstance});
        }
        for (auto& attr : desc.vertexInput.attributes) {
            GLVertexFormatInfo vfmt = ToGLVertexFormat(attr.format);
            data->vertexAttribs.push_back(
                {attr.location, attr.binding, vfmt.type, vfmt.components, attr.offset, vfmt.normalized}
            );
        }

        return handle;
    }

    // =========================================================================
    // Compute Pipeline
    // =========================================================================

    auto OpenGLDevice::CreateComputePipelineImpl(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        auto* mod = shaderModules_.Lookup(desc.computeShader);
        if (!mod) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        GLuint shader = 0;
        if (mod->isSPIRV) {
            shader = CompileShaderSPIRV(
                gl_, ext_, mod->compiledShader, mod->spirvData, mod->entryPoint, GL_COMPUTE_SHADER
            );
        } else {
            shader = CompileShaderGLSL(gl_, mod->compiledShader, mod->source, GL_COMPUTE_SHADER);
        }
        if (!shader) {
            return std::unexpected(RhiError::ShaderCompilationFailed);
        }

        GLuint program = gl_->CreateProgram();
        gl_->AttachShader(program, shader);
        gl_->LinkProgram(program);

        GLint linked = 0;
        gl_->GetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024]{};
            gl_->GetProgramInfoLog(program, sizeof(log), nullptr, log);
            MIKI_LOG_ERROR(::miki::debug::LogCategory::Rhi, "[OpenGL] Compute program link error: {}", log);
            gl_->DeleteProgram(program);
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            gl_->DeleteProgram(program);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->program = program;
        data->isCompute = true;
        return handle;
    }

    // =========================================================================
    // Ray Tracing Pipeline (not available on T4)
    // =========================================================================

    auto OpenGLDevice::CreateRayTracingPipelineImpl(const RayTracingPipelineDesc&) -> RhiResult<PipelineHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    void OpenGLDevice::DestroyPipelineImpl(PipelineHandle h) {
        auto* data = pipelines_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->program) {
            gl_->DeleteProgram(data->program);
        }
        if (data->vao) {
            gl_->DeleteVertexArrays(1, &data->vao);
        }
        pipelines_.Free(h);
    }

    // =========================================================================
    // Pipeline cache (GL has no native cache — store blob for compatibility)
    // =========================================================================

    auto OpenGLDevice::CreatePipelineCacheImpl(std::span<const uint8_t> initialData) -> RhiResult<PipelineCacheHandle> {
        auto [handle, data] = pipelineCaches_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->blob.assign(initialData.begin(), initialData.end());
        return handle;
    }

    auto OpenGLDevice::GetPipelineCacheDataImpl(PipelineCacheHandle h) -> std::vector<uint8_t> {
        auto* data = pipelineCaches_.Lookup(h);
        if (!data) {
            return {};
        }
        return data->blob;
    }

    void OpenGLDevice::DestroyPipelineCacheImpl(PipelineCacheHandle h) {
        auto* data = pipelineCaches_.Lookup(h);
        if (!data) {
            return;
        }
        pipelineCaches_.Free(h);
    }

    // =========================================================================
    // Pipeline library (no split compilation on GL — fallback)
    // =========================================================================

    auto OpenGLDevice::CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc&)
        -> RhiResult<PipelineLibraryPartHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    auto OpenGLDevice::LinkGraphicsPipelineImpl(const LinkedPipelineDesc&) -> RhiResult<PipelineHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    void OpenGLDevice::DestroyPipelineLibraryPartImpl(PipelineLibraryPartHandle) {}

}  // namespace miki::rhi
