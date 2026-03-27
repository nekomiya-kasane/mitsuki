/** @brief Resource creation descriptors for the miki RHI.
 *
 * Defines TextureDesc, BufferDesc, SamplerDesc, RenderingInfo, and
 * supporting enums (TextureUsage, BufferUsage, MemoryType, Filter,
 * AddressMode, LoadOp, StoreOp, etc.).
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

#include "miki/core/Types.h"
#include "miki/rhi/Format.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

// ===========================================================================
// Bitmask operator macro — eliminates per-enum boilerplate
// ===========================================================================

/** @brief Define bitwise operators (|, &, ^, ~, |=, &=, ^=) and HasFlag for a scoped enum. */
#define MIKI_DEFINE_BITMASK_OPS(EnumType)                                                  \
    [[nodiscard]] constexpr auto operator|(EnumType a, EnumType b) noexcept -> EnumType {  \
        return static_cast<EnumType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); \
    }                                                                                      \
    [[nodiscard]] constexpr auto operator&(EnumType a, EnumType b) noexcept -> EnumType {  \
        return static_cast<EnumType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); \
    }                                                                                      \
    [[nodiscard]] constexpr auto operator^(EnumType a, EnumType b) noexcept -> EnumType {  \
        return static_cast<EnumType>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b)); \
    }                                                                                      \
    [[nodiscard]] constexpr auto operator~(EnumType a) noexcept -> EnumType {              \
        return static_cast<EnumType>(~static_cast<uint32_t>(a));                           \
    }                                                                                      \
    constexpr auto operator|=(EnumType& a, EnumType b) noexcept -> EnumType& {             \
        return a = a | b;                                                                  \
    }                                                                                      \
    constexpr auto operator&=(EnumType& a, EnumType b) noexcept -> EnumType& {             \
        return a = a & b;                                                                  \
    }                                                                                      \
    constexpr auto operator^=(EnumType& a, EnumType b) noexcept -> EnumType& {             \
        return a = a ^ b;                                                                  \
    }                                                                                      \
    [[nodiscard]] constexpr auto HasFlag(EnumType a, EnumType b) noexcept -> bool {        \
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;                 \
    }

    // ===========================================================================
    // Usage / memory enums
    // ===========================================================================

    /** @brief Texture usage flags (bitmask). */
    enum class TextureUsage : uint32_t {
        Sampled = 1 << 0,
        Storage = 1 << 1,
        ColorAttachment = 1 << 2,
        DepthStencilAttachment = 1 << 3,
        TransferSrc = 1 << 4,
        TransferDst = 1 << 5,
    };
    MIKI_DEFINE_BITMASK_OPS(TextureUsage)

    /** @brief Buffer usage flags (bitmask). */
    enum class BufferUsage : uint32_t {
        Vertex = 1 << 0,
        Index = 1 << 1,
        Uniform = 1 << 2,
        Storage = 1 << 3,
        Indirect = 1 << 4,
        TransferSrc = 1 << 5,
        TransferDst = 1 << 6,
        ShaderDeviceAddress
            = 1 << 7,  ///< Buffer may be accessed via BDA (Vulkan: VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    };
    MIKI_DEFINE_BITMASK_OPS(BufferUsage)

    /** @brief Memory allocation strategy. */
    enum class MemoryType : uint8_t {
        GpuOnly,
        CpuToGpu,
        GpuToCpu,
        DeviceLocalHostVisible,  ///< ReBAR / SAM: device-local + host-visible. CPU can write directly to VRAM.
    };

    /** @brief Texture sampling filter mode. */
    enum class Filter : uint8_t {
        Nearest,
        Linear,
    };

    /** @brief Mip-map interpolation mode. */
    enum class MipMapMode : uint8_t {
        Nearest,
        Linear,
    };

    /** @brief Sampler reduction mode (Vulkan 1.2 core / EXT_sampler_filter_minmax). */
    enum class SamplerReductionMode : uint8_t {
        WeightedAverage,  ///< Default — standard linear/nearest filtering
        Min,              ///< Return minimum of sampled texels (VK_SAMPLER_REDUCTION_MODE_MIN)
        Max,              ///< Return maximum of sampled texels (VK_SAMPLER_REDUCTION_MODE_MAX)
    };

    /** @brief Texture address / wrap mode. */
    enum class AddressMode : uint8_t {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
    };

    /** @brief Index buffer element type. */
    enum class IndexType : uint8_t {
        Uint16,
        Uint32,
    };

    /** @brief Attachment load operation. */
    enum class LoadOp : uint8_t {
        Load,
        Clear,
        DontCare,
    };

    /** @brief Attachment store operation. */
    enum class StoreOp : uint8_t {
        Store,
        DontCare,
    };

    // ===========================================================================
    // Clear value
    // ===========================================================================

    /** @brief Clear color (RGBA float). */
    struct ClearColor {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    /** @brief Clear depth/stencil. */
    struct ClearDepthStencil {
        float depth = 1.0f;
        uint32_t stencil = 0;
    };

    /** @brief Clear value variant (color or depth/stencil). */
    using ClearValue = std::variant<ClearColor, ClearDepthStencil>;

    // ===========================================================================
    // Resource descriptors
    // ===========================================================================

    /** @brief Texture creation descriptor. */
    struct TextureDesc {
        uint32_t width = 1;                           ///< Texture width in pixels
        uint32_t height = 1;                          ///< Texture height in pixels
        uint32_t depth = 1;                           ///< Texture depth (for 3D textures)
        uint32_t mipLevels = 1;                       ///< Number of mip levels
        uint32_t arrayLayers = 1;                     ///< Number of array layers
        Format format = Format::Undefined;            ///< Pixel format
        TextureUsage usage = TextureUsage::Sampled;   ///< Usage flags
        MemoryType memoryType = MemoryType::GpuOnly;  ///< Memory allocation type
        uint32_t samples = 1;                         ///< Sample count for multisampling
        bool isCubemap
            = false;  ///< True = create as cubemap (arrayLayers must be 6). Replaces arrayLayers==6 heuristic (D6).
    };

    /** @brief View type for texture views. */
    enum class TextureViewType : uint8_t {
        Tex2D,
        Tex2DArray,
        TexCube,
    };

    /** @brief Texture view creation descriptor — aliased view into an existing texture.
     *
     * The returned TextureHandle borrows the source image (does not own it).
     * DestroyTexture on the view only destroys the VkImageView, not the image/memory.
     * Used for per-mip storage views (prefilter specular) and view type override (2D_ARRAY for compute writes to
     * cubemap).
     */
    struct TextureViewDesc {
        TextureHandle sourceTexture = {};  ///< Source texture to create view from
        TextureViewType viewType = TextureViewType::Tex2D;
        Format format = Format::Undefined;  ///< Undefined = inherit from source
        uint32_t baseMipLevel = 0;
        uint32_t mipCount = 1;
        uint32_t baseLayer = 0;
        uint32_t layerCount = 1;
    };

    /** @brief Buffer creation descriptor. */
    struct BufferDesc {
        uint64_t size = 0;
        BufferUsage usage = BufferUsage::Vertex;
        MemoryType memoryType = MemoryType::GpuOnly;
    };

    /** @brief Sampler creation descriptor. */
    struct SamplerDesc {
        Filter magFilter = Filter::Linear;
        Filter minFilter = Filter::Linear;
        MipMapMode mipMapMode = MipMapMode::Linear;
        AddressMode addressModeU = AddressMode::Repeat;
        AddressMode addressModeV = AddressMode::Repeat;
        AddressMode addressModeW = AddressMode::Repeat;
        SamplerReductionMode reductionMode = SamplerReductionMode::WeightedAverage;
        float maxAnisotropy = 1.0f;
        float minLod = 0.0f;
        float maxLod = 1000.0f;
    };

    // ===========================================================================
    // Pipeline descriptors (minimal for initial interface)
    // ===========================================================================

    /** @brief Shader stage for pipeline creation. */
    enum class ShaderStageFlag : uint32_t {
        Vertex = 1 << 0,
        Fragment = 1 << 1,
        Compute = 1 << 2,
        Task = 1 << 3,
        Mesh = 1 << 4,
    };
    MIKI_DEFINE_BITMASK_OPS(ShaderStageFlag)

    /** @brief Shader module descriptor (bytecode reference). */
    struct ShaderModuleDesc {
        const void* code = nullptr;
        uint64_t codeSize = 0;
        ShaderStageFlag stage = ShaderStageFlag::Vertex;
    };

    // ===========================================================================
    // Pipeline configuration types (used by GraphicsPipelineDesc)
    // ===========================================================================

    /** @brief Face culling mode. */
    enum class CullMode : uint8_t {
        None,
        Front,
        Back,
        FrontAndBack,
    };

    /** @brief Polygon rasterization mode. */
    enum class PolygonMode : uint8_t {
        Fill,
        Line,
        Point,
    };

    /** @brief Vertex attribute format for vertex layout description. */
    enum class VertexAttributeFormat : uint8_t {
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        Int2,
        Int3,
        Int4,
        UNorm8x4,
    };

    /** @brief Single vertex attribute binding. */
    struct VertexAttribute {
        uint32_t location = 0;
        uint32_t binding = 0;
        VertexAttributeFormat format = VertexAttributeFormat::Float3;
        uint32_t offset = 0;
    };

    /** @brief Vertex input rate (per-vertex or per-instance). */
    enum class VertexInputRate : uint8_t {
        Vertex,
        Instance,
    };

    /** @brief Vertex buffer binding description. */
    struct VertexBinding {
        uint32_t binding = 0;
        uint32_t stride = 0;
        VertexInputRate inputRate = VertexInputRate::Vertex;
    };

    /** @brief Complete vertex layout description. */
    struct VertexLayout {
        std::span<const VertexAttribute> attributes = {};
        std::span<const VertexBinding> bindings = {};
    };

    /** @brief Depth comparison function. */
    enum class CompareOp : uint8_t {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always,
    };

    /** @brief Stencil operation applied on stencil test pass/fail. */
    enum class StencilOp : uint8_t {
        Keep,       ///< Keep current stencil value
        Zero,       ///< Set stencil to 0
        Replace,    ///< Set stencil to reference value
        IncrClamp,  ///< Increment and clamp to max
        DecrClamp,  ///< Decrement and clamp to 0
        Invert,     ///< Bitwise invert
        IncrWrap,   ///< Increment and wrap to 0
        DecrWrap,   ///< Decrement and wrap to max
    };

    /** @brief Per-face stencil operation state. */
    struct StencilOpState {
        StencilOp failOp = StencilOp::Keep;       ///< Stencil test fails
        StencilOp passOp = StencilOp::Keep;       ///< Both stencil and depth pass
        StencilOp depthFailOp = StencilOp::Keep;  ///< Stencil passes, depth fails
        CompareOp compareOp = CompareOp::Always;
        uint32_t compareMask = 0xFF;
        uint32_t writeMask = 0xFF;
        uint32_t reference = 0;
    };

    /** @brief Stencil test state for graphics pipeline. */
    struct StencilState {
        bool enabled = false;
        StencilOpState front = {};
        StencilOpState back = {};
    };

    /** @brief Blend factor for color/alpha blending. */
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
    };

    /** @brief Blend operation. */
    enum class BlendOp : uint8_t {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    /** @brief Color attachment blend state. */
    struct BlendState {
        bool enabled = false;
        BlendFactor srcColor = BlendFactor::One;
        BlendFactor dstColor = BlendFactor::Zero;
        BlendOp colorOp = BlendOp::Add;
        BlendFactor srcAlpha = BlendFactor::One;
        BlendFactor dstAlpha = BlendFactor::Zero;
        BlendOp alphaOp = BlendOp::Add;
    };

    // ===========================================================================
    // Pipeline descriptors
    // ===========================================================================

    /** @brief Graphics pipeline creation descriptor — full PSO state.
     *
     * Contains all state needed to create a backend-native graphics PSO:
     * shader stages, vertex input, rasterization, depth/stencil, blending,
     * render target formats, and pipeline layout.
     *
     * This is the primary interface for graphics pipeline creation.
     * `IPipelineFactory::CreateGeometryPass()` is a convenience wrapper
     * that constructs this descriptor and forwards to `IDevice::CreateGraphicsPipeline()`.
     */
    struct GraphicsPipelineDesc {
        ShaderModuleDesc vertexShader = {};    ///< Vertex shader (Tier2/3/4 geometry path)
        ShaderModuleDesc fragmentShader = {};  ///< Fragment/pixel shader (all tiers)
        ShaderModuleDesc taskShader = {};      ///< Task/amplification shader (Tier1 mesh shader path, Phase 6a)
        ShaderModuleDesc meshShader = {};      ///< Mesh shader (Tier1, Phase 6a). When set, vertexShader is ignored.
        VertexLayout vertexLayout = {};        ///< Vertex input (ignored when meshShader is set — BDA vertex fetch)
        bool depthTest = true;
        bool depthWrite = true;
        CompareOp depthCompareOp = CompareOp::Less;
        CullMode cullMode = CullMode::Back;
        PolygonMode polygonMode = PolygonMode::Fill;
        BlendState colorBlend = {};
        StencilState stencilState = {};
        static constexpr uint32_t kMaxColorAttachments = 8;
        std::array<Format, kMaxColorAttachments> colorFormats = {};
        uint32_t colorFormatCount = 0;
        Format depthFormat = Format::D32_FLOAT;
        Format stencilFormat
            = Format::Undefined;  ///< Set to D24_UNORM_S8_UINT or D32_FLOAT_S8_UINT to enable stencil attachment.
        PipelineLayoutHandle pipelineLayout = {};  ///< Optional — if valid, use this layout; otherwise create empty.

        [[nodiscard]] constexpr auto IsMeshShaderPipeline() const noexcept -> bool {
            return meshShader.code != nullptr && meshShader.codeSize > 0;
        }
    };

    /** @brief Compute pipeline creation descriptor. */
    struct ComputePipelineDesc {
        ShaderModuleDesc computeShader = {};
        PipelineLayoutHandle pipelineLayout = {};  ///< Required — must be valid.
    };

    // ===========================================================================
    // Rendering (dynamic rendering)
    // ===========================================================================

    /** @brief Viewport for rasterization. */
    struct Viewport {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    /** @brief 2D rectangle with signed offset and unsigned extent. */
    struct Rect2D {
        miki::core::int2 offset = {};
        miki::core::uint2 extent = {};
    };

    /** @brief Single rendering attachment (color or depth). */
    struct RenderingAttachment {
        TextureHandle texture = {};
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        ClearValue clearValue = ClearColor{};
    };

    /** @brief Dynamic rendering info (no VkRenderPass). */
    struct RenderingInfo {
        std::span<const RenderingAttachment> colorAttachments = {};
        std::optional<RenderingAttachment> depthAttachment = std::nullopt;
        Rect2D renderArea = {};
    };

    // ===========================================================================
    // Barrier / copy descriptors
    // ===========================================================================

    /** @brief Texture memory barrier. */
    struct TextureBarrier {
        TextureHandle texture = {};
        AccessFlags srcAccess = AccessFlags::None;
        AccessFlags dstAccess = AccessFlags::None;
        TextureLayout oldLayout = TextureLayout::Undefined;
        TextureLayout newLayout = TextureLayout::Undefined;
        uint32_t srcQueueFamilyIndex = UINT32_MAX;  ///< UINT32_MAX = ignored (no ownership transfer)
        uint32_t dstQueueFamilyIndex = UINT32_MAX;  ///< UINT32_MAX = ignored (no ownership transfer)
    };

    /** @brief Buffer memory barrier. */
    struct BufferBarrier {
        BufferHandle buffer = {};
        AccessFlags srcAccess = AccessFlags::None;
        AccessFlags dstAccess = AccessFlags::None;
        uint64_t offset = 0;
        uint64_t size = ~uint64_t{0};
        uint32_t srcQueueFamilyIndex = UINT32_MAX;  ///< UINT32_MAX = ignored (no ownership transfer)
        uint32_t dstQueueFamilyIndex = UINT32_MAX;  ///< UINT32_MAX = ignored (no ownership transfer)
    };

    /** @brief Global memory barrier (no specific resource). */
    struct MemoryBarrier {
        AccessFlags srcAccess = AccessFlags::None;
        AccessFlags dstAccess = AccessFlags::None;
    };

    /** @brief Pipeline barrier info aggregating texture, buffer, and memory barriers. */
    struct PipelineBarrierInfo {
        PipelineStage srcStage = PipelineStage::TopOfPipe;
        PipelineStage dstStage = PipelineStage::BottomOfPipe;
        std::span<const TextureBarrier> textureBarriers = {};
        std::span<const BufferBarrier> bufferBarriers = {};
        std::span<const MemoryBarrier> memoryBarriers = {};
    };

    /** @brief Copy region for buffer-to-texture transfer.
     *
     * Follows Vulkan semantics: bufferRowLength and bufferImageHeight are
     * in **texels** (pixels), not bytes. 0 means tightly packed (= texWidth
     * or texHeight respectively). Backends convert to native units internally.
     */
    struct BufferTextureCopyInfo {
        uint64_t bufferOffset = 0;
        uint32_t bufferRowLength = 0;    ///< Texels per row in the buffer. 0 = tightly packed.
        uint32_t bufferImageHeight = 0;  ///< Rows per image in the buffer. 0 = tightly packed.
        uint32_t texOffsetX = 0;
        uint32_t texOffsetY = 0;
        uint32_t texOffsetZ = 0;
        uint32_t texWidth = 0;
        uint32_t texHeight = 0;
        uint32_t texDepth = 1;
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
    };

    /** @brief Copy region for buffer-to-buffer transfer. */
    struct BufferCopyInfo {
        uint64_t srcOffset = 0;
        uint64_t dstOffset = 0;
        uint64_t size = 0;
    };

    // ===========================================================================
    // Descriptor system (T2.3.1)
    // ===========================================================================

    /** @brief Descriptor resource type. */
    enum class DescriptorType : uint8_t {
        UniformBuffer,
        StorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        CombinedImageSampler,
    };

    /** @brief Per-binding flags for descriptor set layout bindings.
     *
     *  Vulkan 1.2+ (core): maps to VkDescriptorBindingFlags.
     *  D3D12 / WebGPU / OpenGL: ignored (inherent in their binding models).
     */
    enum class DescriptorBindingFlags : uint32_t {
        None                    = 0,
        UpdateAfterBind         = 1 << 0,  ///< Vk: VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
        PartiallyBound          = 1 << 1,  ///< Vk: VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
        VariableDescriptorCount = 1 << 2,  ///< Vk: VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT (last binding only)
    };
    MIKI_DEFINE_BITMASK_OPS(DescriptorBindingFlags)

    /** @brief Single binding slot within a descriptor set layout. */
    struct DescriptorSetLayoutBinding {
        uint32_t binding = 0;
        DescriptorType type = DescriptorType::UniformBuffer;
        uint32_t count = 1;
        ShaderStageFlag stageFlags = ShaderStageFlag::Vertex;
        DescriptorBindingFlags flags = DescriptorBindingFlags::None;
    };

    /** @brief Pipeline bind point for descriptor operations. */
    enum class PipelineBindPoint : uint8_t {
        Graphics,
        Compute
    };

    /** @brief Layout-level flags for descriptor set layout creation.
     *
     *  Vulkan: maps to VkDescriptorSetLayoutCreateFlags.
     *  D3D12 / WebGPU / OpenGL: ignored (inherent in their binding models).
     */
    enum class DescriptorSetLayoutFlags : uint32_t {
        None             = 0,
        PushDescriptor   = 1 << 0,  ///< Vk: VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT
        UpdateAfterBind  = 1 << 1,  ///< Vk: VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
        DescriptorBuffer = 1 << 2,  ///< Vk: VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT
    };
    MIKI_DEFINE_BITMASK_OPS(DescriptorSetLayoutFlags)

    /** @brief Descriptor set layout creation descriptor. */
    struct DescriptorSetLayoutDesc {
        std::span<const DescriptorSetLayoutBinding> bindings = {};
        DescriptorSetLayoutFlags flags = DescriptorSetLayoutFlags::None;
    };

    /** @brief Push constant range within a pipeline layout. */
    struct PushConstantRange {
        ShaderStageFlag stageFlags = ShaderStageFlag::Vertex;
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    /** @brief Pipeline layout creation descriptor. */
    struct PipelineLayoutDesc {
        std::span<const DescriptorSetLayoutHandle> setLayouts = {};
        std::span<const PushConstantRange> pushConstants = {};
    };

    /** @brief A single descriptor write operation. */
    struct DescriptorWrite {
        uint32_t binding = 0;
        uint32_t arrayElement = 0;
        DescriptorType type = DescriptorType::UniformBuffer;

        BufferHandle buffer = {};
        uint64_t bufferOffset = 0;
        uint64_t bufferRange = 0;

        TextureHandle texture = {};
        SamplerHandle sampler = {};
    };

    // ===========================================================================
    // Submit synchronization (T2.2.3 semaphore fix)
    // ===========================================================================

    /** @brief Optional synchronization info for IDevice::Submit().
     *
     * Vulkan: waitSemaphores → VkSubmitInfo::pWaitSemaphores (cast uint64_t → VkSemaphore)
     *         signalSemaphores → VkSubmitInfo::pSignalSemaphores
     *         signalFence → vkQueueSubmit fence param (cast uint64_t → VkFence)
     * D3D12/GL/WebGPU/Mock: ignored (these backends use implicit sync or fences).
     *
     * Default-constructed SubmitSyncInfo has empty spans + 0 fence → Submit() behaves as before.
     */
    struct SubmitSyncInfo {
        std::span<const uint64_t> waitSemaphores = {};
        std::span<const uint64_t> signalSemaphores = {};
        uint64_t signalFence = 0;  ///< Opaque fence handle (Vulkan: VkFence)
    };

    // ===========================================================================
    // Timeline semaphore synchronization (T12 TransferQueue)
    // ===========================================================================

    /** @brief A wait operation on a timeline semaphore.
     *
     * The queue will wait until the semaphore reaches at least `value` before
     * executing subsequent commands. `stageMask` specifies which pipeline stages
     * are blocked by the wait.
     */
    struct TimelineSemaphoreWait {
        SemaphoreHandle semaphore = {};
        uint64_t value = 0;
        PipelineStage stageMask = PipelineStage::AllCommands;
    };

    /** @brief A signal operation on a timeline semaphore.
     *
     * After all commands in this submission complete the given stages, the
     * semaphore is advanced to `value`.
     */
    struct TimelineSemaphoreSignal {
        SemaphoreHandle semaphore = {};
        uint64_t value = 0;
        PipelineStage stageMask = PipelineStage::AllCommands;
    };

    /** @brief Tier1 submit synchronization — timeline-first, no VkFence.
     *
     * Used by VulkanRenderSurfaceTier1 and FrameManager Tier1 paths.
     * Binary semaphores are retained ONLY for swapchain acquire/present
     * (Vulkan spec mandates binary for vkAcquireNextImageKHR / vkQueuePresentKHR).
     *
     * Vulkan Tier1: maps to VkSubmitInfo2 with timeline + binary VkSemaphoreSubmitInfo.
     * D3D12: not used (D3D12 ID3D12Fence is inherently timeline).
     */
    struct SubmitSyncInfo2 {
        std::span<const uint64_t> waitBinarySemaphores = {};
        std::span<const uint64_t> signalBinarySemaphores = {};

        std::span<const TimelineSemaphoreSignal> timelineSignals = {};
        std::span<const TimelineSemaphoreWait> timelineWaits = {};
    };

    /** @brief Extended submit info with timeline semaphore + queue targeting.
     *
     * Supersedes SubmitSyncInfo for new code. Supports:
     * - Timeline semaphore waits/signals (Vulkan 1.2+, D3D12 fences)
     * - Queue type selection (Graphics / Compute / Transfer)
     * - Legacy binary semaphores + fence for Compat tier
     *
     * Vulkan: maps to VkSubmitInfo2 with VkSemaphoreSubmitInfo timeline entries.
     * D3D12:  maps to ID3D12CommandQueue::Signal/Wait on ID3D12Fence.
     * Other backends: timeline fields ignored.
     */
    struct SubmitInfo2 {
        QueueType queue = QueueType::Graphics;

        std::span<const TimelineSemaphoreWait> timelineWaits = {};
        std::span<const TimelineSemaphoreSignal> timelineSignals = {};

        std::span<const uint64_t> waitSemaphores = {};    ///< Binary semaphore waits (swapchain interop)
        std::span<const uint64_t> signalSemaphores = {};  ///< Binary semaphore signals (swapchain interop)
        uint64_t signalFence = 0;                         ///< Compat tier fence (Tier1: always 0)
    };

}  // namespace miki::rhi
