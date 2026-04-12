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
        Graphics,      ///< Graphics + compute + transfer (main queue)
        Compute,       ///< Dedicated compute queue (frame-sync, HIGH priority)
        AsyncCompute,  ///< Async compute queue (cross-frame, NORMAL priority; Level A only, else alias Compute)
        Transfer,      ///< Transfer only (DMA / copy engine)
    };

    // =========================================================================
    // Shader stages (bitmask)
    //
    // NOTE: Geometry Shader and Tessellation (Hull/Domain) are intentionally excluded.
    //   - GS is deprecated by all major IHVs; Mesh Shader is the replacement for vertex amplification.
    //   - Tessellation is unsupported on WebGPU and discouraged on mobile; compute-driven
    //     tessellation (via Compute -> Vertex/Mesh) is the modern alternative.
    //   - Keeping these out avoids backend complexity for Tier3/4 where no fallback exists.
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
        None = 0,
        TopOfPipe = 1u << 0,               // pseudo-stage before all commands
        DrawIndirect = 1u << 1,            // indirect command parameter consumption
        VertexInput = 1u << 2,             // vertex/index buffer consumption
        VertexShader = 1u << 3,            // vertex shader execution
        TessControlShader = 1u << 4,       // tessellation control shader — unused, all backends
        TessEvalShader = 1u << 5,          // tessellation evaluation shader — unused, all backends
        GeometryShader = 1u << 6,          // geometry shader — unused, all backends
        FragmentShader = 1u << 7,          // fragment shader execution
        EarlyFragmentTests = 1u << 8,      // early depth/stencil tests
        LateFragmentTests = 1u << 9,       // late depth/stencil tests
        ColorAttachmentOutput = 1u << 10,  // color attachment read/write
        ComputeShader = 1u << 11,          // compute shader execution
        Transfer = 1u << 12,               // all transfer operations (copy/blit/resolve/clear)
        BottomOfPipe = 1u << 13,           // pseudo-stage after all commands
        Host = 1u << 14,                   // host read/write of device memory
        AllGraphics = 1u << 15,            // all graphics pipeline stages
        AllCommands = 1u << 16,            // all commands on the queue
        CommandPreprocess = 1u << 17,      // device-generated command preprocess — unused, all backends
        ConditionalRendering = 1u << 18,   // conditional rendering predicate — unused, all backends
        TaskShader = 1u << 19,             // task (amplification) shader
        MeshShader = 1u << 20,             // mesh shader
        RayTracingShader = 1u << 21,       // ray tracing shader execution
        ShadingRateImage = 1u << 22,       // fragment shading rate attachment read
        FragmentDensity = 1u << 23,        // fragment density map processing — unused, all backends
        TransformFeedback = 1u << 24,      // transform feedback / stream-out — unused, all backends
        AccelStructBuild = 1u << 25,       // acceleration structure build
    };

    MIKI_BITMASK_OPS(PipelineStage)

    // =========================================================================
    // Access flags (bitmask)
    // =========================================================================

    enum class AccessFlags : uint32_t {
        None = 0,
        IndirectCommandRead = 1u << 0,      // read indirect draw/dispatch parameters
        IndexRead = 1u << 1,                // read index buffer
        VertexAttributeRead = 1u << 2,      // read vertex buffer attributes
        UniformRead = 1u << 3,              // read uniform/constant buffer
        InputAttachmentRead = 1u << 4,      // read input attachment in fragment shader
        ShaderRead = 1u << 5,               // generic shader resource read
        ShaderWrite = 1u << 6,              // generic shader resource write (UAV/SSBO)
        ColorAttachmentRead = 1u << 7,      // read color attachment (e.g. blending)
        ColorAttachmentWrite = 1u << 8,     // write color attachment
        DepthStencilRead = 1u << 9,         // read depth/stencil attachment
        DepthStencilWrite = 1u << 10,       // write depth/stencil attachment
        TransferRead = 1u << 11,            // read source of transfer operation
        TransferWrite = 1u << 12,           // write destination of transfer operation
        HostRead = 1u << 13,                // host-side memory read
        HostWrite = 1u << 14,               // host-side memory write
        MemoryRead = 1u << 15,              // catch-all memory read
        MemoryWrite = 1u << 16,             // catch-all memory write
        CommandPreprocessRead = 1u << 17,   // device-generated command read — unused, all backends
        CommandPreprocessWrite = 1u << 18,  // device-generated command write — unused, all backends
        // bit 19: unassigned in Vulkan spec
        ConditionalRenderingRead = 1u << 20,  // conditional rendering predicate read — unused, all backends
        AccelStructRead = 1u << 21,           // read acceleration structure (ray traversal/build input)
        AccelStructWrite = 1u << 22,          // write acceleration structure (build output)
        ShadingRateImageRead = 1u << 23,      // read fragment shading rate attachment
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
