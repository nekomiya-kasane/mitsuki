/** @file RhiEnums.h
 *  @brief All RHI enumeration types.
 *
 *  Bitmask enums use uint32_t to allow sufficient bits for OR-combination.
 *  Non-bitmask enums use uint8_t for compact storage.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>

namespace miki::rhi {

    // =========================================================================
    // Queue types
    // =========================================================================

    enum class QueueType : uint8_t {
        Graphics,  ///< Graphics + compute + transfer
        Compute,   ///< Compute + transfer (async compute)
        Transfer,  ///< Transfer only (DMA / copy engine)
    };

    // =========================================================================
    // Shader stages (bitmask)
    // =========================================================================

    enum class ShaderStage : uint32_t {
        Vertex = 1 << 0,
        Fragment = 1 << 1,
        Compute = 1 << 2,
        Task = 1 << 3,
        Mesh = 1 << 4,
        RayGen = 1 << 5,
        AnyHit = 1 << 6,
        ClosestHit = 1 << 7,
        Miss = 1 << 8,
        Intersection = 1 << 9,
        Callable = 1 << 10,
        AllGraphics = Vertex | Fragment,
        All = 0x7FF,
    };

    [[nodiscard]] constexpr auto operator|(ShaderStage a, ShaderStage b) noexcept -> ShaderStage {
        return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    [[nodiscard]] constexpr auto operator&(ShaderStage a, ShaderStage b) noexcept -> ShaderStage {
        return static_cast<ShaderStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // =========================================================================
    // Pipeline stages (bitmask)
    // =========================================================================

    enum class PipelineStage : uint32_t {
        TopOfPipe = 1 << 0,
        DrawIndirect = 1 << 1,
        VertexInput = 1 << 2,
        VertexShader = 1 << 3,
        TaskShader = 1 << 4,
        MeshShader = 1 << 5,
        FragmentShader = 1 << 6,
        EarlyFragmentTests = 1 << 7,
        LateFragmentTests = 1 << 8,
        ColorAttachmentOutput = 1 << 9,
        ComputeShader = 1 << 10,
        Transfer = 1 << 11,
        BottomOfPipe = 1 << 12,
        Host = 1 << 13,
        AllGraphics = 1 << 14,
        AllCommands = 1 << 15,
        AccelStructBuild = 1 << 16,
        RayTracingShader = 1 << 17,
        ShadingRateImage = 1 << 18,
    };

    [[nodiscard]] constexpr auto operator|(PipelineStage a, PipelineStage b) noexcept -> PipelineStage {
        return static_cast<PipelineStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    [[nodiscard]] constexpr auto operator&(PipelineStage a, PipelineStage b) noexcept -> PipelineStage {
        return static_cast<PipelineStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // =========================================================================
    // Access flags (bitmask)
    // =========================================================================

    enum class AccessFlags : uint32_t {
        None = 0,
        IndirectCommandRead = 1 << 0,
        IndexRead = 1 << 1,
        VertexAttributeRead = 1 << 2,
        UniformRead = 1 << 3,
        InputAttachmentRead = 1 << 4,
        ShaderRead = 1 << 5,
        ShaderWrite = 1 << 6,
        ColorAttachmentRead = 1 << 7,
        ColorAttachmentWrite = 1 << 8,
        DepthStencilRead = 1 << 9,
        DepthStencilWrite = 1 << 10,
        TransferRead = 1 << 11,
        TransferWrite = 1 << 12,
        HostRead = 1 << 13,
        HostWrite = 1 << 14,
        MemoryRead = 1 << 15,
        MemoryWrite = 1 << 16,
        AccelStructRead = 1 << 17,
        AccelStructWrite = 1 << 18,
        ShadingRateImageRead = 1 << 19,
    };

    [[nodiscard]] constexpr auto operator|(AccessFlags a, AccessFlags b) noexcept -> AccessFlags {
        return static_cast<AccessFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    [[nodiscard]] constexpr auto operator&(AccessFlags a, AccessFlags b) noexcept -> AccessFlags {
        return static_cast<AccessFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // =========================================================================
    // Texture layout (for barriers)
    // =========================================================================

    enum class TextureLayout : uint8_t {
        Undefined,
        General,
        ColorAttachment,
        DepthStencilAttachment,
        DepthStencilReadOnly,
        ShaderReadOnly,
        TransferSrc,
        TransferDst,
        Present,
        ShadingRate,
    };

    // =========================================================================
    // Buffer usage (bitmask)
    // =========================================================================

    enum class BufferUsage : uint32_t {
        Vertex = 1 << 0,
        Index = 1 << 1,
        Uniform = 1 << 2,
        Storage = 1 << 3,
        Indirect = 1 << 4,
        TransferSrc = 1 << 5,
        TransferDst = 1 << 6,
        AccelStructInput = 1 << 7,
        AccelStructStorage = 1 << 8,
        ShaderDeviceAddress = 1 << 9,
        SparseBinding = 1 << 10,
    };

    [[nodiscard]] constexpr auto operator|(BufferUsage a, BufferUsage b) noexcept -> BufferUsage {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    [[nodiscard]] constexpr auto operator&(BufferUsage a, BufferUsage b) noexcept -> BufferUsage {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // =========================================================================
    // Texture usage (bitmask)
    // =========================================================================

    enum class TextureUsage : uint32_t {
        Sampled = 1 << 0,
        Storage = 1 << 1,
        ColorAttachment = 1 << 2,
        DepthStencil = 1 << 3,
        TransferSrc = 1 << 4,
        TransferDst = 1 << 5,
        InputAttachment = 1 << 6,
        ShadingRate = 1 << 7,
        SparseBinding = 1 << 8,
    };

    [[nodiscard]] constexpr auto operator|(TextureUsage a, TextureUsage b) noexcept -> TextureUsage {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    [[nodiscard]] constexpr auto operator&(TextureUsage a, TextureUsage b) noexcept -> TextureUsage {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // =========================================================================
    // Memory location
    // =========================================================================

    enum class MemoryLocation : uint8_t {
        GpuOnly,   ///< DEVICE_LOCAL
        CpuToGpu,  ///< HOST_VISIBLE | HOST_COHERENT (staging, uniform ring)
        GpuToCpu,  ///< HOST_VISIBLE | HOST_CACHED (readback)
        Auto,      ///< Backend decides (ReBAR if available for small buffers)
    };

    // =========================================================================
    // Texture dimension
    // =========================================================================

    enum class TextureDimension : uint8_t {
        Tex1D,
        Tex2D,
        Tex3D,
        TexCube,
        Tex2DArray,
        TexCubeArray,
    };

    // =========================================================================
    // Texture aspect
    // =========================================================================

    enum class TextureAspect : uint8_t {
        Color,
        Depth,
        Stencil,
        DepthStencil,
    };

    // =========================================================================
    // Sampler enums
    // =========================================================================

    enum class Filter : uint8_t {
        Nearest,
        Linear,
    };

    enum class AddressMode : uint8_t {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
    };

    enum class BorderColor : uint8_t {
        TransparentBlack,
        OpaqueBlack,
        OpaqueWhite,
    };

    // =========================================================================
    // Comparison and stencil operations
    // =========================================================================

    enum class CompareOp : uint8_t {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
        None,  ///< No compare (for samplers)
    };

    enum class StencilOp : uint8_t {
        Keep,
        Zero,
        Replace,
        IncrementAndClamp,
        DecrementAndClamp,
        Invert,
        IncrementAndWrap,
        DecrementAndWrap,
    };

    struct StencilOpState {
        StencilOp failOp = StencilOp::Keep;
        StencilOp passOp = StencilOp::Keep;
        StencilOp depthFailOp = StencilOp::Keep;
        CompareOp compareOp = CompareOp::Always;
        uint32_t compareMask = 0xFF;
        uint32_t writeMask = 0xFF;
        uint32_t reference = 0;
    };

    // =========================================================================
    // Blend enums
    // =========================================================================

    enum class BlendFactor : uint8_t {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantColor,
        OneMinusConstantColor,
        SrcAlphaSaturate,
    };

    enum class BlendOp : uint8_t {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    enum class ColorWriteMask : uint8_t {
        None = 0,
        R = 1 << 0,
        G = 1 << 1,
        B = 1 << 2,
        A = 1 << 3,
        All = R | G | B | A,
    };

    [[nodiscard]] constexpr auto operator|(ColorWriteMask a, ColorWriteMask b) noexcept -> ColorWriteMask {
        return static_cast<ColorWriteMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    // =========================================================================
    // Rasterizer enums
    // =========================================================================

    enum class PrimitiveTopology : uint8_t {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        PatchList,
    };

    enum class PolygonMode : uint8_t {
        Fill,
        Line,
        Point,
    };

    enum class CullMode : uint8_t {
        None,
        Front,
        Back,
        FrontAndBack,
    };

    enum class FrontFace : uint8_t {
        CounterClockwise,
        Clockwise,
    };

    // =========================================================================
    // Index type
    // =========================================================================

    enum class IndexType : uint8_t {
        Uint16,
        Uint32,
    };

    // =========================================================================
    // Vertex input rate
    // =========================================================================

    enum class VertexInputRate : uint8_t {
        PerVertex,
        PerInstance,
    };

    // =========================================================================
    // Attachment operations
    // =========================================================================

    enum class AttachmentLoadOp : uint8_t {
        Load,
        Clear,
        DontCare,
    };

    enum class AttachmentStoreOp : uint8_t {
        Store,
        DontCare,
    };

    // =========================================================================
    // Present mode
    // =========================================================================

    enum class PresentMode : uint8_t {
        Immediate,
        Mailbox,
        Fifo,
        FifoRelaxed,
    };

    // =========================================================================
    // Semaphore type
    // =========================================================================

    enum class SemaphoreType : uint8_t {
        Binary,
        Timeline,
    };

    // =========================================================================
    // Query type
    // =========================================================================

    enum class QueryType : uint8_t {
        Timestamp,
        Occlusion,
        PipelineStatistics,
    };

    // =========================================================================
    // Variable Rate Shading (T1 only)
    // =========================================================================

    enum class ShadingRate : uint8_t {
        Rate1x1 = 0,
        Rate1x2 = 1,
        Rate2x1 = 2,
        Rate2x2 = 3,
        Rate2x4 = 4,
        Rate4x2 = 5,
        Rate4x4 = 6,
    };

    enum class ShadingRateCombinerOp : uint8_t {
        Keep,
        Replace,
        Min,
        Max,
        Mul,
    };

    // =========================================================================
    // Descriptor model (per-tier)
    // =========================================================================

    enum class DescriptorModel : uint8_t {
        DescriptorHeap,    ///< Vulkan 1.4 VK_EXT_descriptor_heap / D3D12
        DescriptorBuffer,  ///< Vulkan VK_EXT_descriptor_buffer
        DescriptorSet,     ///< Vulkan traditional descriptor sets
        BindGroup,         ///< WebGPU bind groups
        DirectBind,        ///< OpenGL direct bind (glBindBufferRange etc.)
    };

    // =========================================================================
    // Acceleration structure enums (T1 only)
    // =========================================================================

    enum class AccelStructGeometryType : uint8_t {
        Triangles,
        AABBs,
    };

    enum class AccelStructGeometryFlags : uint8_t {
        None = 0,
        Opaque = 1 << 0,
        NoDuplicateAnyHit = 1 << 1,
    };

    [[nodiscard]] constexpr auto operator|(AccelStructGeometryFlags a, AccelStructGeometryFlags b) noexcept
        -> AccelStructGeometryFlags {
        return static_cast<AccelStructGeometryFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    enum class AccelStructBuildFlags : uint8_t {
        None = 0,
        PreferFastTrace = 1 << 0,
        PreferFastBuild = 1 << 1,
        AllowUpdate = 1 << 2,
    };

    [[nodiscard]] constexpr auto operator|(AccelStructBuildFlags a, AccelStructBuildFlags b) noexcept
        -> AccelStructBuildFlags {
        return static_cast<AccelStructBuildFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    enum class AccelStructInstanceFlags : uint8_t {
        None = 0,
        TriangleFacingCullDisable = 1 << 0,
        TriangleFrontCounterClockwise = 1 << 1,
        ForceOpaque = 1 << 2,
        ForceNoOpaque = 1 << 3,
    };

    [[nodiscard]] constexpr auto operator|(AccelStructInstanceFlags a, AccelStructInstanceFlags b) noexcept
        -> AccelStructInstanceFlags {
        return static_cast<AccelStructInstanceFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    // =========================================================================
    // Pipeline library (split compilation)
    // =========================================================================

    enum class PipelineLibraryPart : uint8_t {
        VertexInput,
        PreRasterization,
        FragmentShader,
        FragmentOutput,
    };

    // =========================================================================
    // Format feature flags (for format support query)
    // =========================================================================

    enum class FormatFeatureFlags : uint32_t {
        None = 0,
        Sampled = 1 << 0,
        Storage = 1 << 1,
        ColorAttachment = 1 << 2,
        DepthStencil = 1 << 3,
        BlendSrc = 1 << 4,
        Filter = 1 << 5,
        All = Sampled | Storage | ColorAttachment | DepthStencil | BlendSrc | Filter,
    };

    [[nodiscard]] constexpr auto operator|(FormatFeatureFlags a, FormatFeatureFlags b) noexcept -> FormatFeatureFlags {
        return static_cast<FormatFeatureFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    [[nodiscard]] constexpr auto operator&(FormatFeatureFlags a, FormatFeatureFlags b) noexcept -> FormatFeatureFlags {
        return static_cast<FormatFeatureFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // =========================================================================
    // Compression format (future: CmdDecompressBuffer)
    // =========================================================================

    enum class CompressionFormat : uint8_t {
        GDeflate,
    };

    // =========================================================================
    // Ray tracing shader group type
    // =========================================================================

    enum class RayTracingShaderGroupType : uint8_t {
        General,
        TrianglesHitGroup,
        ProceduralHitGroup,
    };

}  // namespace miki::rhi
