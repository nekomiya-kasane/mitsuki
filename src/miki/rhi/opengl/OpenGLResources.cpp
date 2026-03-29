/** @file OpenGLResources.cpp
 *  @brief OpenGL 4.3+ (Tier 4) backend — Buffer, Texture, TextureView, Sampler, ShaderModule, Memory aliasing
 * (fallback: separate allocation).
 *
 *  Uses DSA (glCreateBuffers/glNamedBufferStorage) when GL 4.5+ or ARB_direct_state_access. Falls back to bind-to-edit
 * for GL 4.3/4.4. All buffers use glBufferStorage (immutable) for maximum driver optimization.
 */

#include "miki/rhi/backend/OpenGLDevice.h"

#include <cassert>
#include <cstring>

namespace miki::rhi {

    // =========================================================================
    // Format conversion: miki::rhi::Format -> GL internal format / type / components
    // =========================================================================

    namespace {
        struct GLFormatInfo {
            GLenum internalFormat;
            GLenum format;
            GLenum type;
            uint32_t bytesPerPixel;
        };
        auto ToGLFormat(Format fmt) -> GLFormatInfo {
            // clang-format off
            switch (fmt) {
                case Format::R8_UNORM:     return {.internalFormat = GL_R8, .format = GL_RED, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 1};
                case Format::R8_SNORM:     return {.internalFormat = GL_R8_SNORM, .format = GL_RED, .type = GL_BYTE, .bytesPerPixel = 1};
                case Format::R8_UINT:      return {.internalFormat = GL_R8UI, .format = GL_RED_INTEGER, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 1};
                case Format::R8_SINT:      return {.internalFormat = GL_R8I, .format = GL_RED_INTEGER, .type = GL_BYTE, .bytesPerPixel = 1};
                case Format::RG8_UNORM:    return {.internalFormat = GL_RG8, .format = GL_RG, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 2};
                case Format::RG8_SNORM:    return {.internalFormat = GL_RG8_SNORM, .format = GL_RG, .type = GL_BYTE, .bytesPerPixel = 2};
                case Format::RG8_UINT:     return {.internalFormat = GL_RG8UI, .format = GL_RG_INTEGER, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 2};
                case Format::RG8_SINT:     return {.internalFormat = GL_RG8I, .format = GL_RG_INTEGER, .type = GL_BYTE, .bytesPerPixel = 2};
                case Format::RGBA8_UNORM:  return {.internalFormat = GL_RGBA8, .format = GL_RGBA, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 4};
                case Format::RGBA8_SNORM:  return {.internalFormat = GL_RGBA8_SNORM, .format = GL_RGBA, .type = GL_BYTE, .bytesPerPixel = 4};
                case Format::RGBA8_UINT:   return {.internalFormat = GL_RGBA8UI, .format = GL_RGBA_INTEGER, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 4};
                case Format::RGBA8_SINT:   return {.internalFormat = GL_RGBA8I, .format = GL_RGBA_INTEGER, .type = GL_BYTE, .bytesPerPixel = 4};
                case Format::RGBA8_SRGB:   return {.internalFormat = GL_SRGB8_ALPHA8, .format = GL_RGBA, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 4};
                case Format::BGRA8_UNORM:  return {.internalFormat = GL_RGBA8, .format = GL_BGRA, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 4};
                case Format::BGRA8_SRGB:   return {.internalFormat = GL_SRGB8_ALPHA8, .format = GL_BGRA, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 4};
                case Format::R16_UNORM:    return {.internalFormat = GL_R16, .format = GL_RED, .type = GL_UNSIGNED_SHORT, .bytesPerPixel = 2};
                case Format::R16_SNORM:    return {.internalFormat = GL_R16_SNORM, .format = GL_RED, .type = GL_SHORT, .bytesPerPixel = 2};
                case Format::R16_UINT:     return {.internalFormat = GL_R16UI, .format = GL_RED_INTEGER, .type = GL_UNSIGNED_SHORT, .bytesPerPixel = 2};
                case Format::R16_SINT:     return {.internalFormat = GL_R16I, .format = GL_RED_INTEGER, .type = GL_SHORT, .bytesPerPixel = 2};
                case Format::R16_FLOAT:    return {.internalFormat = GL_R16F, .format = GL_RED, .type = GL_HALF_FLOAT, .bytesPerPixel = 2};
                case Format::RG16_UNORM:   return {.internalFormat = GL_RG16, .format = GL_RG, .type = GL_UNSIGNED_SHORT, .bytesPerPixel = 4};
                case Format::RG16_SNORM:   return {.internalFormat = GL_RG16_SNORM, .format = GL_RG, .type = GL_SHORT, .bytesPerPixel = 4};
                case Format::RG16_UINT:    return {.internalFormat = GL_RG16UI, .format = GL_RG_INTEGER, .type = GL_UNSIGNED_SHORT, .bytesPerPixel = 4};
                case Format::RG16_SINT:    return {.internalFormat = GL_RG16I, .format = GL_RG_INTEGER, .type = GL_SHORT, .bytesPerPixel = 4};
                case Format::RG16_FLOAT:   return {.internalFormat = GL_RG16F, .format = GL_RG, .type = GL_HALF_FLOAT, .bytesPerPixel = 4};
                case Format::RGBA16_UNORM: return {.internalFormat = GL_RGBA16, .format = GL_RGBA, .type = GL_UNSIGNED_SHORT, .bytesPerPixel = 8};
                case Format::RGBA16_SNORM: return {.internalFormat = GL_RGBA16_SNORM, .format = GL_RGBA, .type = GL_SHORT, .bytesPerPixel = 8};
                case Format::RGBA16_UINT:  return {.internalFormat = GL_RGBA16UI, .format = GL_RGBA_INTEGER, .type = GL_UNSIGNED_SHORT, .bytesPerPixel = 8};
                case Format::RGBA16_SINT:  return {.internalFormat = GL_RGBA16I, .format = GL_RGBA_INTEGER, .type = GL_SHORT, .bytesPerPixel = 8};
                case Format::RGBA16_FLOAT: return {.internalFormat = GL_RGBA16F, .format = GL_RGBA, .type = GL_HALF_FLOAT, .bytesPerPixel = 8};
                case Format::R32_UINT:     return {.internalFormat = GL_R32UI, .format = GL_RED_INTEGER, .type = GL_UNSIGNED_INT, .bytesPerPixel = 4};
                case Format::R32_SINT:     return {.internalFormat = GL_R32I, .format = GL_RED_INTEGER, .type = GL_INT, .bytesPerPixel = 4};
                case Format::R32_FLOAT:    return {.internalFormat = GL_R32F, .format = GL_RED, .type = GL_FLOAT, .bytesPerPixel = 4};
                case Format::RG32_UINT:    return {.internalFormat = GL_RG32UI, .format = GL_RG_INTEGER, .type = GL_UNSIGNED_INT, .bytesPerPixel = 8};
                case Format::RG32_SINT:    return {.internalFormat = GL_RG32I, .format = GL_RG_INTEGER, .type = GL_INT, .bytesPerPixel = 8};
                case Format::RG32_FLOAT:   return {.internalFormat = GL_RG32F, .format = GL_RG, .type = GL_FLOAT, .bytesPerPixel = 8};
                case Format::RGB32_UINT:   return {.internalFormat = GL_RGB32UI, .format = GL_RGB_INTEGER, .type = GL_UNSIGNED_INT, .bytesPerPixel = 12};
                case Format::RGB32_SINT:   return {.internalFormat = GL_RGB32I, .format = GL_RGB_INTEGER, .type = GL_INT, .bytesPerPixel = 12};
                case Format::RGB32_FLOAT:  return {.internalFormat = GL_RGB32F, .format = GL_RGB, .type = GL_FLOAT, .bytesPerPixel = 12};
                case Format::RGBA32_UINT:  return {.internalFormat = GL_RGBA32UI, .format = GL_RGBA_INTEGER, .type = GL_UNSIGNED_INT, .bytesPerPixel = 16};
                case Format::RGBA32_SINT:  return {.internalFormat = GL_RGBA32I, .format = GL_RGBA_INTEGER, .type = GL_INT, .bytesPerPixel = 16};
                case Format::RGBA32_FLOAT: return {.internalFormat = GL_RGBA32F, .format = GL_RGBA, .type = GL_FLOAT, .bytesPerPixel = 16};
                case Format::RGB10A2_UNORM: return {.internalFormat = GL_RGB10_A2, .format = GL_RGBA, .type = GL_UNSIGNED_INT_2_10_10_10_REV, .bytesPerPixel = 4};
                case Format::RG11B10_FLOAT: return {.internalFormat = GL_R11F_G11F_B10F, .format = GL_RGB, .type = GL_UNSIGNED_INT_10F_11F_11F_REV, .bytesPerPixel = 4};
                case Format::D16_UNORM:    return {.internalFormat = GL_DEPTH_COMPONENT16, .format = GL_DEPTH_COMPONENT, .type = GL_UNSIGNED_SHORT, .bytesPerPixel = 2};
                case Format::D32_FLOAT:    return {.internalFormat = GL_DEPTH_COMPONENT32F, .format = GL_DEPTH_COMPONENT, .type = GL_FLOAT, .bytesPerPixel = 4};
                case Format::D24_UNORM_S8_UINT: return {.internalFormat = GL_DEPTH24_STENCIL8, .format = GL_DEPTH_STENCIL, .type = GL_UNSIGNED_INT_24_8, .bytesPerPixel = 4};
                case Format::D32_FLOAT_S8_UINT: return {.internalFormat = GL_DEPTH32F_STENCIL8, .format = GL_DEPTH_STENCIL, .type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV, .bytesPerPixel = 8};
                default:                   return {.internalFormat = GL_RGBA8, .format = GL_RGBA, .type = GL_UNSIGNED_BYTE, .bytesPerPixel = 4};
            }
            // clang-format on
        }

        auto ToGLTextureTarget(TextureDimension dim, uint32_t arrayLayers) -> GLenum {
            switch (dim) {
                case TextureDimension::Tex1D: return GL_TEXTURE_1D;
                case TextureDimension::Tex2D: return GL_TEXTURE_2D;
                case TextureDimension::Tex3D: return GL_TEXTURE_3D;
                case TextureDimension::TexCube: return GL_TEXTURE_CUBE_MAP;
                case TextureDimension::Tex2DArray: return GL_TEXTURE_2D_ARRAY;
                case TextureDimension::TexCubeArray: return GL_TEXTURE_CUBE_MAP_ARRAY;
            }
            return GL_TEXTURE_2D;
        }

        [[maybe_unused]] auto IsDepthFormat(GLenum fmt) -> bool {
            return fmt == GL_DEPTH_COMPONENT16 || fmt == GL_DEPTH_COMPONENT32F || fmt == GL_DEPTH24_STENCIL8
                   || fmt == GL_DEPTH32F_STENCIL8;
        }

    }  // namespace

    // =========================================================================
    // Buffer
    // =========================================================================

    auto OpenGLDevice::CreateBufferImpl(const BufferDesc& desc) -> RhiResult<BufferHandle> {
        GLuint buffer = 0;
        GLbitfield storageFlags = 0;

        auto has
            = [&](BufferUsage bit) { return (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(bit)) != 0; };

        // Determine storage flags
        if (desc.memory == MemoryLocation::CpuToGpu) {
            storageFlags = GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        } else if (desc.memory == MemoryLocation::GpuToCpu) {
            storageFlags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        } else {
            storageFlags = GL_DYNAMIC_STORAGE_BIT;
        }

        if (hasDSA_) {
            gl_->CreateBuffers(1, &buffer);
            gl_->NamedBufferStorage(buffer, static_cast<GLsizeiptr>(desc.size), nullptr, storageFlags);
        } else {
            gl_->GenBuffers(1, &buffer);
            gl_->BindBuffer(GL_ARRAY_BUFFER, buffer);
            gl_->BufferStorage(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.size), nullptr, storageFlags);
            gl_->BindBuffer(GL_ARRAY_BUFFER, 0);
        }

        auto [handle, data] = buffers_.Allocate();
        if (!data) {
            gl_->DeleteBuffers(1, &buffer);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->buffer = buffer;
        data->size = desc.size;
        data->usage = desc.usage;
        data->glUsage = storageFlags;

        // Persistent map for upload/readback buffers
        if (desc.memory == MemoryLocation::CpuToGpu) {
            if (hasDSA_) {
                data->mappedPtr = gl_->MapNamedBufferRange(
                    buffer, 0, static_cast<GLsizeiptr>(desc.size),
                    GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
                );
            } else {
                gl_->BindBuffer(GL_ARRAY_BUFFER, buffer);
                data->mappedPtr = gl_->MapBufferRange(
                    GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(desc.size),
                    GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
                );
                gl_->BindBuffer(GL_ARRAY_BUFFER, 0);
            }
        } else if (desc.memory == MemoryLocation::GpuToCpu) {
            if (hasDSA_) {
                data->mappedPtr = gl_->MapNamedBufferRange(
                    buffer, 0, static_cast<GLsizeiptr>(desc.size),
                    GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
                );
            } else {
                gl_->BindBuffer(GL_ARRAY_BUFFER, buffer);
                data->mappedPtr = gl_->MapBufferRange(
                    GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(desc.size),
                    GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
                );
                gl_->BindBuffer(GL_ARRAY_BUFFER, 0);
            }
        }

        if (desc.debugName && gl_->KHR_debug) {
            gl_->ObjectLabel(GL_BUFFER, buffer, -1, desc.debugName);
        }

        totalAllocatedBytes_ += desc.size;
        ++totalAllocationCount_;
        return handle;
    }

    void OpenGLDevice::DestroyBufferImpl(BufferHandle h) {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->mappedPtr) {
            if (hasDSA_) {
                gl_->UnmapNamedBuffer(data->buffer);
            } else {
                gl_->BindBuffer(GL_ARRAY_BUFFER, data->buffer);
                gl_->UnmapBuffer(GL_ARRAY_BUFFER);
                gl_->BindBuffer(GL_ARRAY_BUFFER, 0);
            }
        }
        totalAllocatedBytes_ -= data->size;
        --totalAllocationCount_;
        gl_->DeleteBuffers(1, &data->buffer);
        buffers_.Free(h);
    }

    auto OpenGLDevice::MapBufferImpl(BufferHandle h) -> RhiResult<void*> {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }
        if (data->mappedPtr) {
            return data->mappedPtr;
        }
        if (hasDSA_) {
            data->mappedPtr = gl_->MapNamedBufferRange(
                data->buffer, 0, static_cast<GLsizeiptr>(data->size), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT
            );
        } else {
            gl_->BindBuffer(GL_ARRAY_BUFFER, data->buffer);
            data->mappedPtr = gl_->MapBufferRange(
                GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(data->size), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT
            );
            gl_->BindBuffer(GL_ARRAY_BUFFER, 0);
        }
        if (!data->mappedPtr) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }
        return data->mappedPtr;
    }

    void OpenGLDevice::UnmapBufferImpl(BufferHandle h) {
        auto* data = buffers_.Lookup(h);
        if (!data || !data->mappedPtr) {
            return;
        }
        // Don't unmap persistent buffers
        if (data->glUsage & GL_MAP_PERSISTENT_BIT) {
            return;
        }
        if (hasDSA_) {
            gl_->UnmapNamedBuffer(data->buffer);
        } else {
            gl_->BindBuffer(GL_ARRAY_BUFFER, data->buffer);
            gl_->UnmapBuffer(GL_ARRAY_BUFFER);
            gl_->BindBuffer(GL_ARRAY_BUFFER, 0);
        }
        data->mappedPtr = nullptr;
    }

    auto OpenGLDevice::GetBufferDeviceAddressImpl(BufferHandle) -> uint64_t {
        return 0;  // GL has no BDA
    }

    // =========================================================================
    // Texture
    // =========================================================================

    auto OpenGLDevice::CreateTextureImpl(const TextureDesc& desc) -> RhiResult<TextureHandle> {
        GLFormatInfo fmtInfo = ToGLFormat(desc.format);
        GLenum target = ToGLTextureTarget(desc.dimension, desc.arrayLayers);

        GLuint texture = 0;

        if (hasDSA_) {
            gl_->CreateTextures(target, 1, &texture);
            switch (target) {
                case GL_TEXTURE_1D:
                    gl_->TextureStorage1D(texture, desc.mipLevels, fmtInfo.internalFormat, desc.width);
                    break;
                case GL_TEXTURE_2D:
                    gl_->TextureStorage2D(texture, desc.mipLevels, fmtInfo.internalFormat, desc.width, desc.height);
                    break;
                case GL_TEXTURE_3D:
                    gl_->TextureStorage3D(
                        texture, desc.mipLevels, fmtInfo.internalFormat, desc.width, desc.height, desc.depth
                    );
                    break;
                case GL_TEXTURE_CUBE_MAP:
                    gl_->TextureStorage2D(texture, desc.mipLevels, fmtInfo.internalFormat, desc.width, desc.height);
                    break;
                case GL_TEXTURE_2D_ARRAY:
                    gl_->TextureStorage3D(
                        texture, desc.mipLevels, fmtInfo.internalFormat, desc.width, desc.height, desc.arrayLayers
                    );
                    break;
                case GL_TEXTURE_CUBE_MAP_ARRAY:
                    gl_->TextureStorage3D(
                        texture, desc.mipLevels, fmtInfo.internalFormat, desc.width, desc.height, desc.arrayLayers
                    );
                    break;
                default: break;
            }
        } else {
            gl_->GenTextures(1, &texture);
            gl_->BindTexture(target, texture);
            switch (target) {
                case GL_TEXTURE_1D:
                    gl_->TexStorage1D(target, desc.mipLevels, fmtInfo.internalFormat, desc.width);
                    break;
                case GL_TEXTURE_2D:
                case GL_TEXTURE_CUBE_MAP:
                    gl_->TexStorage2D(target, desc.mipLevels, fmtInfo.internalFormat, desc.width, desc.height);
                    break;
                case GL_TEXTURE_3D:
                    gl_->TexStorage3D(
                        target, desc.mipLevels, fmtInfo.internalFormat, desc.width, desc.height, desc.depth
                    );
                    break;
                case GL_TEXTURE_2D_ARRAY:
                case GL_TEXTURE_CUBE_MAP_ARRAY:
                    gl_->TexStorage3D(
                        target, desc.mipLevels, fmtInfo.internalFormat, desc.width, desc.height, desc.arrayLayers
                    );
                    break;
                default: break;
            }
            gl_->BindTexture(target, 0);
        }

        auto [handle, data] = textures_.Allocate();
        if (!data) {
            gl_->DeleteTextures(1, &texture);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->texture = texture;
        data->target = target;
        data->internalFormat = fmtInfo.internalFormat;
        data->width = desc.width;
        data->height = desc.height;
        data->depth = desc.depth;
        data->mipLevels = desc.mipLevels;
        data->arrayLayers = desc.arrayLayers;
        data->ownsTexture = true;

        if (desc.debugName && gl_->KHR_debug) {
            gl_->ObjectLabel(GL_TEXTURE, texture, -1, desc.debugName);
        }

        return handle;
    }

    void OpenGLDevice::DestroyTextureImpl(TextureHandle h) {
        auto* data = textures_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->ownsTexture && data->texture) {
            gl_->DeleteTextures(1, &data->texture);
        }
        textures_.Free(h);
    }

    // =========================================================================
    // TextureView (glTextureView, GL 4.3 core)
    // =========================================================================

    auto OpenGLDevice::CreateTextureViewImpl(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle> {
        auto* texData = textures_.Lookup(desc.texture);
        if (!texData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        GLFormatInfo fmtInfo = ToGLFormat(desc.format);
        GLenum viewTarget = ToGLTextureTarget(desc.viewDimension, desc.arrayLayerCount);

        GLuint viewTex = 0;
        gl_->GenTextures(1, &viewTex);
        gl_->TextureView(
            viewTex, viewTarget, texData->texture, fmtInfo.internalFormat, desc.baseMipLevel, desc.mipLevelCount,
            desc.baseArrayLayer, desc.arrayLayerCount
        );

        auto [handle, data] = textureViews_.Allocate();
        if (!data) {
            gl_->DeleteTextures(1, &viewTex);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->viewTexture = viewTex;
        data->parentTexture = desc.texture;
        data->target = viewTarget;
        data->ownsView = true;
        return handle;
    }

    void OpenGLDevice::DestroyTextureViewImpl(TextureViewHandle h) {
        auto* data = textureViews_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->ownsView && data->viewTexture) {
            gl_->DeleteTextures(1, &data->viewTexture);
            data->viewTexture = 0;
        }
        textureViews_.Free(h);
    }

    // =========================================================================
    // Sampler
    // =========================================================================

    namespace {
        auto ToGLFilter(Filter f) -> GLenum {
            return (f == Filter::Linear) ? GL_LINEAR : GL_NEAREST;
        }

        auto ToGLMinFilter(Filter min, Filter mip) -> GLenum {
            if (min == Filter::Nearest && mip == Filter::Nearest) {
                return GL_NEAREST_MIPMAP_NEAREST;
            }
            if (min == Filter::Nearest && mip == Filter::Linear) {
                return GL_NEAREST_MIPMAP_LINEAR;
            }
            if (min == Filter::Linear && mip == Filter::Nearest) {
                return GL_LINEAR_MIPMAP_NEAREST;
            }
            return GL_LINEAR_MIPMAP_LINEAR;
        }

        auto ToGLWrap(AddressMode m) -> GLenum {
            switch (m) {
                case AddressMode::Repeat: return GL_REPEAT;
                case AddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
                case AddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
                case AddressMode::ClampToBorder: return GL_CLAMP_TO_BORDER;
            }
            return GL_REPEAT;
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
                case CompareOp::None: return GL_NEVER;
            }
            return GL_NEVER;
        }
    }  // namespace

    auto OpenGLDevice::CreateSamplerImpl(const SamplerDesc& desc) -> RhiResult<SamplerHandle> {
        GLuint sampler = 0;
        gl_->GenSamplers(1, &sampler);

        gl_->SamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, ToGLFilter(desc.magFilter));
        gl_->SamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, ToGLMinFilter(desc.minFilter, desc.mipFilter));
        gl_->SamplerParameteri(sampler, GL_TEXTURE_WRAP_S, ToGLWrap(desc.addressU));
        gl_->SamplerParameteri(sampler, GL_TEXTURE_WRAP_T, ToGLWrap(desc.addressV));
        gl_->SamplerParameteri(sampler, GL_TEXTURE_WRAP_R, ToGLWrap(desc.addressW));
        gl_->SamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, desc.mipLodBias);
        gl_->SamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, desc.minLod);
        gl_->SamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, desc.maxLod);

        if (desc.maxAnisotropy > 0.0f) {
            constexpr GLenum GL_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FE;
            gl_->SamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, desc.maxAnisotropy);
        }

        if (desc.compareOp != CompareOp::None) {
            gl_->SamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            gl_->SamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, ToGLCompareFunc(desc.compareOp));
        }

        if (desc.borderColor == BorderColor::OpaqueWhite) {
            float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
            gl_->SamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, white);
        } else if (desc.borderColor == BorderColor::OpaqueBlack) {
            float black[] = {0.0f, 0.0f, 0.0f, 1.0f};
            gl_->SamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, black);
        }

        auto [handle, data] = samplers_.Allocate();
        if (!data) {
            gl_->DeleteSamplers(1, &sampler);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->sampler = sampler;
        return handle;
    }

    void OpenGLDevice::DestroySamplerImpl(SamplerHandle h) {
        auto* data = samplers_.Lookup(h);
        if (!data) {
            return;
        }
        gl_->DeleteSamplers(1, &data->sampler);
        samplers_.Free(h);
    }

    // =========================================================================
    // ShaderModule (GLSL 4.30 source text)
    // =========================================================================

    auto OpenGLDevice::CreateShaderModuleImpl(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle> {
        auto [handle, data] = shaderModules_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->source.assign(reinterpret_cast<const char*>(desc.code.data()), desc.code.size());
        data->stage = desc.stage;
        data->compiledShader = 0;  // Compiled lazily at pipeline creation
        return handle;
    }

    void OpenGLDevice::DestroyShaderModuleImpl(ShaderModuleHandle h) {
        auto* data = shaderModules_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->compiledShader) {
            gl_->DeleteShader(data->compiledShader);
        }
        shaderModules_.Free(h);
    }

    // =========================================================================
    // Memory aliasing (T4 fallback: separate allocation — no true aliasing)
    // =========================================================================

    auto OpenGLDevice::CreateMemoryHeapImpl(const MemoryHeapDesc&) -> RhiResult<DeviceMemoryHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);  // No aliasing on GL
    }

    void OpenGLDevice::DestroyMemoryHeapImpl(DeviceMemoryHandle) {}

    void OpenGLDevice::AliasBufferMemoryImpl(BufferHandle, DeviceMemoryHandle, uint64_t) {
        // No-op: GL doesn't support memory aliasing
    }

    void OpenGLDevice::AliasTextureMemoryImpl(TextureHandle, DeviceMemoryHandle, uint64_t) {
        // No-op: GL doesn't support memory aliasing
    }

    auto OpenGLDevice::GetBufferMemoryRequirementsImpl(BufferHandle h) -> MemoryRequirements {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return {};
        }
        return {data->size, 256, UINT32_MAX};
    }

    auto OpenGLDevice::GetTextureMemoryRequirementsImpl(TextureHandle h) -> MemoryRequirements {
        auto* data = textures_.Lookup(h);
        if (!data) {
            return {};
        }
        GLFormatInfo fmtInfo = ToGLFormat(Format::RGBA8_UNORM);  // Approximate
        uint64_t size = static_cast<uint64_t>(data->width) * data->height * fmtInfo.bytesPerPixel;
        return {size, 256, UINT32_MAX};
    }

}  // namespace miki::rhi
