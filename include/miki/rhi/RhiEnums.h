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
#include <type_traits>

#include "miki/core/TypeTraits.h"

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
        Amplification = Task,
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

    MIKI_BITMASK_OPS(ShaderStage)

    // =========================================================================
    // Pipeline stages (bitmask)
    // =========================================================================

    enum class PipelineStage : uint32_t {
        None = 0,                            // VK_PIPELINE_STAGE_2_NONE
        TopOfPipe = 0x00000001,              // VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
        DrawIndirect = 0x00000002,           // VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT
        VertexInput = 0x00000004,            // VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT
        VertexShader = 0x00000008,           // VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
        FragmentShader = 0x00000080,         // VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
        EarlyFragmentTests = 0x00000100,     // VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
        LateFragmentTests = 0x00000200,      // VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
        ColorAttachmentOutput = 0x00000400,  // VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
        ComputeShader = 0x00000800,          // VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        Transfer = 0x00001000,               // VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT
        BottomOfPipe = 0x00002000,           // VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
        Host = 0x00004000,                   // VK_PIPELINE_STAGE_2_HOST_BIT
        AllGraphics = 0x00008000,            // VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
        AllCommands = 0x00010000,            // VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        TaskShader = 0x00080000,             // VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
        MeshShader = 0x00100000,             // VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
        RayTracingShader = 0x00200000,       // VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
        ShadingRateImage = 0x00400000,       // VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR
        AccelStructBuild = 0x02000000,       // VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    };

    MIKI_BITMASK_OPS(PipelineStage)

    // =========================================================================
    // Access flags (bitmask)
    // =========================================================================

    enum class AccessFlags : uint32_t {
        None = 0,                           // VK_ACCESS_2_NONE
        IndirectCommandRead = 0x00000001,   // VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
        IndexRead = 0x00000002,             // VK_ACCESS_2_INDEX_READ_BIT
        VertexAttributeRead = 0x00000004,   // VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT
        UniformRead = 0x00000008,           // VK_ACCESS_2_UNIFORM_READ_BIT
        InputAttachmentRead = 0x00000010,   // VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT
        ShaderRead = 0x00000020,            // VK_ACCESS_2_SHADER_READ_BIT
        ShaderWrite = 0x00000040,           // VK_ACCESS_2_SHADER_WRITE_BIT
        ColorAttachmentRead = 0x00000080,   // VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
        ColorAttachmentWrite = 0x00000100,  // VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
        DepthStencilRead = 0x00000200,      // VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
        DepthStencilWrite = 0x00000400,     // VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        TransferRead = 0x00000800,          // VK_ACCESS_2_TRANSFER_READ_BIT
        TransferWrite = 0x00001000,         // VK_ACCESS_2_TRANSFER_WRITE_BIT
        HostRead = 0x00002000,              // VK_ACCESS_2_HOST_READ_BIT
        HostWrite = 0x00004000,             // VK_ACCESS_2_HOST_WRITE_BIT
        MemoryRead = 0x00008000,            // VK_ACCESS_2_MEMORY_READ_BIT
        MemoryWrite = 0x00010000,           // VK_ACCESS_2_MEMORY_WRITE_BIT
        AccelStructRead = 0x00200000,       // VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
        AccelStructWrite = 0x00400000,      // VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
        ShadingRateImageRead = 0x00800000,  // VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR
    };

    MIKI_BITMASK_OPS(AccessFlags)

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

    MIKI_BITMASK_OPS(BufferUsage)

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

    MIKI_BITMASK_OPS(TextureUsage)

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

    MIKI_BITMASK_OPS(ColorWriteMask)

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

    MIKI_BITMASK_OPS(AccelStructGeometryFlags)

    enum class AccelStructBuildFlags : uint8_t {
        None = 0,
        PreferFastTrace = 1 << 0,
        PreferFastBuild = 1 << 1,
        AllowUpdate = 1 << 2,
    };

    MIKI_BITMASK_OPS(AccelStructBuildFlags)

    enum class AccelStructInstanceFlags : uint8_t {
        None = 0,
        TriangleFacingCullDisable = 1 << 0,
        TriangleFrontCounterClockwise = 1 << 1,
        ForceOpaque = 1 << 2,
        ForceNoOpaque = 1 << 3,
    };

    MIKI_BITMASK_OPS(AccelStructInstanceFlags)

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

    MIKI_BITMASK_OPS(FormatFeatureFlags)

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
