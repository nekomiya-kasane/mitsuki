/** @brief Core RHI types for the miki renderer.
 *
 * Defines typed handles, pipeline/access/layout enums, external contexts,
 * and device configuration. No API-specific includes — all backend pointers
 * are void* to avoid leaking Vulkan/D3D12/GL/WebGPU headers.
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <compare>
#include <cstdint>
#include <variant>

#include "miki/rhi/DeviceFeature.h"
#include "miki/rhi/Format.h"

namespace miki::rhi {

    // ===========================================================================
    // Handle<Tag> — typed, generational GPU resource handle
    // ===========================================================================

    /** @brief Typed GPU resource handle with generation tracking.
     *
     * Layout (64 bits): [generation:16 | index:32 | type:16]
     *   - generation: incremented on reuse to detect stale handles
     *   - index: slot in the backend resource pool
     *   - type: tag discriminator (for debug; not enforced at runtime)
     *
     * Null handle: _value == 0 (generation=0, index=0, type=0).
     * sizeof(Handle<Tag>) == 8.
     *
     * @tparam Tag Empty struct tag for compile-time type safety.
     */
    template <typename Tag>
    struct Handle {
        uint64_t _value = 0;

        /** @brief Check if this handle refers to a valid resource. */
        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return _value != 0; }

        /** @brief Extract the 32-bit pool index. */
        [[nodiscard]] constexpr auto Index() const noexcept -> uint32_t {
            return static_cast<uint32_t>((_value >> 16) & 0xFFFFFFFF);
        }

        /** @brief Extract the 16-bit generation counter. */
        [[nodiscard]] constexpr auto Generation() const noexcept -> uint16_t {
            return static_cast<uint16_t>((_value >> 48) & 0xFFFF);
        }

        /** @brief Extract the 16-bit type discriminator. */
        [[nodiscard]] constexpr auto Type() const noexcept -> uint16_t {
            return static_cast<uint16_t>(_value & 0xFFFF);
        }

        /** @brief Construct a handle from its components.
         *  @param iGeneration Generation counter.
         *  @param iIndex Pool index.
         *  @param iType Type discriminator.
         */
        [[nodiscard]] static constexpr auto Create(uint16_t iGeneration, uint32_t iIndex, uint16_t iType) noexcept
            -> Handle {
            return Handle{
                (static_cast<uint64_t>(iGeneration) << 48) | (static_cast<uint64_t>(iIndex) << 16)
                | static_cast<uint64_t>(iType)
            };
        }

        constexpr auto operator==(const Handle&) const noexcept -> bool = default;
        constexpr auto operator!=(const Handle&) const noexcept -> bool = default;
        constexpr auto operator<=>(const Handle&) const noexcept = default;
    };

    // --- Handle tag types ---
    struct TextureTag {};
    struct BufferTag {};
    struct PipelineTag {};
    struct SamplerTag {};
    struct ShaderTag {};
    struct RenderPassTag {};
    struct FramebufferTag {};
    struct DescriptorSetLayoutTag {};
    struct PipelineLayoutTag {};
    struct DescriptorSetTag {};
    struct QueryPoolTag {};
    struct SemaphoreTag {};

    // --- Concrete handle aliases ---
    using TextureHandle = Handle<TextureTag>;
    using BufferHandle = Handle<BufferTag>;
    using PipelineHandle = Handle<PipelineTag>;
    using SamplerHandle = Handle<SamplerTag>;
    using ShaderHandle = Handle<ShaderTag>;
    using RenderPassHandle = Handle<RenderPassTag>;
    using FramebufferHandle = Handle<FramebufferTag>;
    using DescriptorSetLayoutHandle = Handle<DescriptorSetLayoutTag>;
    using PipelineLayoutHandle = Handle<PipelineLayoutTag>;
    using DescriptorSetHandle = Handle<DescriptorSetTag>;
    using QueryPoolHandle = Handle<QueryPoolTag>;
    using SemaphoreHandle = Handle<SemaphoreTag>;

    static_assert(sizeof(TextureHandle) == 8);
    static_assert(sizeof(BufferHandle) == 8);
    static_assert(sizeof(PipelineHandle) == 8);
    static_assert(sizeof(DescriptorSetLayoutHandle) == 8);
    static_assert(sizeof(PipelineLayoutHandle) == 8);
    static_assert(sizeof(DescriptorSetHandle) == 8);
    static_assert(sizeof(QueryPoolHandle) == 8);
    static_assert(sizeof(SemaphoreHandle) == 8);

    // ===========================================================================
    // Queue type
    // ===========================================================================

    /** @brief Identifies a logical queue family on the device.
     *
     * Graphics: general-purpose (draw + compute + transfer).
     * Compute:  async compute (compute + transfer, no draw).
     * Transfer: dedicated DMA engine (transfer only, lowest latency for copies).
     *
     * Vulkan: maps to dedicated queue families when available, falls back to graphics.
     * D3D12:  maps to D3D12_COMMAND_LIST_TYPE_{DIRECT, COMPUTE, COPY}.
     * Other backends: all map to the single queue.
     */
    enum class QueueType : uint8_t {
        Graphics = 0,
        Compute = 1,
        Transfer = 2,
    };

    // ===========================================================================
    // Pipeline / synchronization enums
    // ===========================================================================

    /** @brief Pipeline stage flags for synchronization barriers. */
    enum class PipelineStage : uint32_t {
        TopOfPipe = 0,
        DrawIndirect = 1 << 0,
        VertexInput = 1 << 1,
        VertexShader = 1 << 2,
        FragmentShader = 1 << 3,
        EarlyFragTests = 1 << 4,
        LateFragTests = 1 << 5,
        ColorAttachOut = 1 << 6,
        ComputeShader = 1 << 7,
        Transfer = 1 << 8,
        TaskShader = 1 << 9,
        MeshShader = 1 << 10,
        AllGraphics = 1 << 11,
        AllCommands = 1 << 12,
        BottomOfPipe = 1 << 13,
    };

    /** @brief Bitwise OR for PipelineStage flags. */
    [[nodiscard]] constexpr auto operator|(PipelineStage a, PipelineStage b) noexcept -> PipelineStage {
        return static_cast<PipelineStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    /** @brief Bitwise AND for PipelineStage flags. */
    [[nodiscard]] constexpr auto operator&(PipelineStage a, PipelineStage b) noexcept -> PipelineStage {
        return static_cast<PipelineStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    /** @brief Bitwise OR-assign for PipelineStage flags. */
    constexpr auto operator|=(PipelineStage& a, PipelineStage b) noexcept -> PipelineStage& {
        a = a | b;
        return a;
    }

    /** @brief Memory access flags for synchronization barriers. */
    enum class AccessFlags : uint32_t {
        None = 0,
        ShaderRead = 1 << 0,
        ShaderWrite = 1 << 1,
        TransferRead = 1 << 2,
        TransferWrite = 1 << 3,
        ColorAttachmentRead = 1 << 4,
        ColorAttachmentWrite = 1 << 5,
        DepthStencilRead = 1 << 6,
        DepthStencilWrite = 1 << 7,
        IndirectCommandRead = 1 << 8,
        IndexRead = 1 << 9,
        VertexAttributeRead = 1 << 10,
        UniformRead = 1 << 11,
        HostRead = 1 << 12,
        HostWrite = 1 << 13,
    };

    /** @brief Texture/image layout for synchronization transitions. */
    enum class TextureLayout : uint32_t {
        Undefined = 0,
        General,
        ColorAttachment,
        DepthStencilAttachment,
        DepthStencilReadOnly,
        ShaderReadOnly,
        TransferSrc,
        TransferDst,
        PresentSrc,
    };

    // ===========================================================================
    // Extent2D
    // ===========================================================================

    /** @brief 2D extent (width x height) for surfaces, textures, viewports. */
    struct Extent2D {
        uint32_t width = 0;
        uint32_t height = 0;

        constexpr auto operator==(const Extent2D&) const noexcept -> bool = default;
    };

    // ===========================================================================
    // Backend type
    // ===========================================================================

    /** @brief Identifies which GPU backend to use. */
    enum class BackendType : uint8_t {
        Vulkan,
        D3D12,
        OpenGL,
        WebGPU,
        Mock,
    };

    // ===========================================================================
    // External contexts (Step 2) — opaque backend injection points
    // ===========================================================================

    /** @brief Vulkan external context for IDevice::CreateFromExisting.
     *
     * All pointers are void* to avoid #include <vulkan/vulkan.h> in public headers.
     * Backend casts internally.
     */
    struct VulkanExternalContext {
        void* instance = nullptr;
        void* physicalDevice = nullptr;
        void* device = nullptr;
        uint32_t graphicsQueueFamily = 0;
        uint32_t graphicsQueueIndex = 0;
        uint32_t computeQueueFamily = 0;
        uint32_t computeQueueIndex = 0;
        uint32_t transferQueueFamily = 0;
        uint32_t transferQueueIndex = 0;
        void* surface = nullptr;
    };

    /** @brief D3D12 external context for IDevice::CreateFromExisting. */
    struct D3D12ExternalContext {
        void* device = nullptr;
        void* factory = nullptr;
        void* commandQueue = nullptr;
    };

    /** @brief OpenGL external context for IDevice::CreateFromExisting.
     *
     * If getProcAddress is nullptr and a GL context is current on the calling
     * thread, the engine will auto-detect the platform loader
     * (wglGetProcAddress / glXGetProcAddress / eglGetProcAddress).
     */
    struct OpenGlExternalContext {
        void* getProcAddress = nullptr;
    };

    /** @brief WebGPU external context for IDevice::CreateFromExisting. */
    struct WebGpuExternalContext {
        void* device = nullptr;
    };

    /** @brief Variant holding any backend's external context.
     *
     * Used by IDevice::CreateFromExisting to inject pre-created API objects.
     */
    using ExternalContext
        = std::variant<VulkanExternalContext, D3D12ExternalContext, OpenGlExternalContext, WebGpuExternalContext>;

    /** @brief Device creation configuration. */
    struct DeviceConfig {
        BackendType preferredBackend = BackendType::Vulkan;
        bool enableValidation = false;
        bool enableProfiling = false;
        bool forceCompatTier = false;
        DeviceFeatureSet requiredFeatures;  ///< Device creation fails if unsatisfied
        DeviceFeatureSet optionalFeatures;  ///< Best-effort; silently dropped if unavailable
    };

    /** @brief Native window info for IDevice::CreateForWindow.
     *
     * Host owns the window. miki creates the GPU device/context on it.
     * Window ownership stays with the host — miki will NOT destroy the window.
     */
    struct NativeWindowInfo {
        void* windowHandle = nullptr;  ///< Platform window handle (HWND, X11 Window, etc.)
        BackendType backend = BackendType::Vulkan;
    };

    /** @brief Opaque native image handle for ImportSwapchainImage. */
    struct NativeImageHandle {
        void* handle = nullptr;
        BackendType type = BackendType::Vulkan;
        Format format = Format::RGBA8_UNORM;  ///< Image format (needed to create views)
        uint32_t width = 0;                   ///< Image width
        uint32_t height = 0;                  ///< Image height
    };

    // ===========================================================================
    // Indirect command structs
    // ===========================================================================

    /** @brief Indirect command for mesh shader dispatch.
     *
     * Matches VkDrawMeshTasksIndirectCommandEXT / D3D12 DispatchMesh arguments.
     * 12 bytes, trivially copyable, no alignment requirement.
     */
    struct DrawMeshTasksIndirectCommand {
        uint32_t groupCountX = 0;
        uint32_t groupCountY = 0;
        uint32_t groupCountZ = 0;
    };
    static_assert(sizeof(DrawMeshTasksIndirectCommand) == 12);
    static_assert(std::is_trivially_copyable_v<DrawMeshTasksIndirectCommand>);

}  // namespace miki::rhi
