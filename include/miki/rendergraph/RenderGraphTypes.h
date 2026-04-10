/** @file RenderGraphTypes.h
 *  @brief Core data types for the miki Render Graph system.
 *
 *  Defines handles, descriptors, resource access flags, barrier mapping,
 *  and all fundamental types used across Builder, Compiler, and Executor.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <span>

#include "miki/core/InlineFunction.h"

#include "miki/core/Hash.h"
#include "miki/core/TypeTraits.h"
#include "miki/rhi/Format.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/adaptation/AdaptationTypes.h"

namespace miki::rg {

    // =========================================================================
    // Forward declarations
    // =========================================================================

    struct RenderPassContext;
    class PassBuilder;

    // =========================================================================
    // Handle types — lightweight typed indices into graph-internal arrays
    // =========================================================================

    /// @brief Opaque pass handle. Encodes a 32-bit pass index.
    struct RGPassHandle {
        uint32_t index = kInvalid;
        static constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();
        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return index != kInvalid; }
        constexpr auto operator==(const RGPassHandle&) const noexcept -> bool = default;
    };

    /// @brief Opaque resource handle with SSA versioning.
    /// Encodes [version:16 | index:16] in a single uint32_t.
    /// - index:   identifies the logical resource (texture or buffer).
    /// - version: incremented on each write, enabling SSA-style tracking.
    struct RGResourceHandle {
        uint32_t packed = kInvalid;
        static constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();

        static constexpr uint32_t kIndexBits = 16;
        static constexpr uint32_t kVersionBits = 16;
        static constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1;
        static constexpr uint32_t kVersionShift = kIndexBits;

        [[nodiscard]] static constexpr auto Create(uint16_t index, uint16_t version) noexcept -> RGResourceHandle {
            return {static_cast<uint32_t>(version) << kVersionShift | static_cast<uint32_t>(index)};
        }

        [[nodiscard]] constexpr auto GetIndex() const noexcept -> uint16_t {
            return static_cast<uint16_t>(packed & kIndexMask);
        }
        [[nodiscard]] constexpr auto GetVersion() const noexcept -> uint16_t {
            return static_cast<uint16_t>(packed >> kVersionShift);
        }
        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return packed != kInvalid; }
        [[nodiscard]] constexpr auto NextVersion() const noexcept -> RGResourceHandle {
            return Create(GetIndex(), static_cast<uint16_t>(GetVersion() + 1));
        }
        constexpr auto operator==(const RGResourceHandle&) const noexcept -> bool = default;
    };

    // =========================================================================
    // Queue type (graph-level, maps to rhi::QueueType at execution)
    // =========================================================================

    enum class RGQueueType : uint8_t {
        Graphics,      ///< Graphics + compute + transfer
        AsyncCompute,  ///< Async compute queue (Tier1 only)
        Transfer,      ///< Dedicated transfer queue (Tier1 only)
    };

    /// @brief Map graph-level RGQueueType to RHI QueueType.
    [[nodiscard]] constexpr auto ToRhiQueueType(RGQueueType q) noexcept -> rhi::QueueType {
        switch (q) {
            case RGQueueType::Graphics: return rhi::QueueType::Graphics;
            case RGQueueType::AsyncCompute: return rhi::QueueType::Compute;
            case RGQueueType::Transfer: return rhi::QueueType::Transfer;
        }
        return rhi::QueueType::Graphics;
    }

    // =========================================================================
    // Pass flags (bitmask)
    // =========================================================================

    enum class RGPassFlags : uint8_t {
        None = 0,
        Graphics = 1 << 0,      ///< Uses rasterization pipeline
        Compute = 1 << 1,       ///< Uses compute pipeline
        AsyncCompute = 1 << 2,  ///< Eligible for async compute queue
        Transfer = 1 << 3,      ///< Transfer-only pass
        Present = 1 << 4,       ///< Present pass (swapchain output)
        SideEffects = 1 << 5,   ///< Never cull (readback, present, etc.)
        NeverCull = 1 << 6,     ///< User-forced non-cullable
    };

    MIKI_BITMASK_OPS(RGPassFlags)

    // =========================================================================
    // Resource access — declarative usage flags for pass resource bindings
    // =========================================================================

    enum class ResourceAccess : uint32_t {
        None = 0,

        // Read accesses
        ShaderReadOnly = 1u << 0,   ///< SRV / sampled texture / readonly SSBO
        DepthReadOnly = 1u << 1,    ///< Depth attachment read-only (depth test, no write)
        IndirectBuffer = 1u << 2,   ///< Indirect draw/dispatch argument buffer
        TransferSrc = 1u << 3,      ///< Copy source
        InputAttachment = 1u << 4,  ///< Subpass input attachment
        AccelStructRead = 1u << 5,  ///< Ray tracing acceleration structure read
        ShadingRateRead = 1u << 6,  ///< VRS image read
        PresentSrc = 1u << 7,       ///< Presentation source (terminal consumer, layout transition)

        // Write accesses
        ShaderWrite = 1u << 8,         ///< UAV / storage image / readwrite SSBO
        ColorAttachWrite = 1u << 9,    ///< Color attachment write
        DepthStencilWrite = 1u << 10,  ///< Depth/stencil attachment write
        TransferDst = 1u << 11,        ///< Copy destination
        AccelStructWrite = 1u << 12,   ///< Acceleration structure build/refit output

        // Convenience masks
        ReadMask = ShaderReadOnly | DepthReadOnly | IndirectBuffer | TransferSrc | InputAttachment | AccelStructRead
                   | ShadingRateRead | PresentSrc,
        WriteMask = ShaderWrite | ColorAttachWrite | DepthStencilWrite | TransferDst | AccelStructWrite,
    };

    MIKI_BITMASK_OPS(ResourceAccess)

    /// @brief Check if a ResourceAccess value contains any write access.
    [[nodiscard]] constexpr auto IsWriteAccess(ResourceAccess access) noexcept -> bool {
        return (static_cast<uint32_t>(access) & static_cast<uint32_t>(ResourceAccess::WriteMask)) != 0;
    }

    /// @brief Check if a ResourceAccess value contains any read access.
    [[nodiscard]] constexpr auto IsReadAccess(ResourceAccess access) noexcept -> bool {
        return (static_cast<uint32_t>(access) & static_cast<uint32_t>(ResourceAccess::ReadMask)) != 0;
    }

    // =========================================================================
    // Compile-time access validation per pass type (B-18)
    // =========================================================================

    /// @brief Accesses valid for transfer-only passes.
    inline constexpr ResourceAccess kTransferPassAccessMask = ResourceAccess::TransferSrc | ResourceAccess::TransferDst;

    /// @brief Accesses forbidden on compute-only passes (rasterization-specific).
    inline constexpr ResourceAccess kGraphicsOnlyAccessMask
        = ResourceAccess::ColorAttachWrite | ResourceAccess::DepthStencilWrite | ResourceAccess::DepthReadOnly
          | ResourceAccess::InputAttachment | ResourceAccess::ShadingRateRead;

    /// @brief Accesses valid for present passes.
    inline constexpr ResourceAccess kPresentPassAccessMask = ResourceAccess::PresentSrc;

    /// @brief Validate that a ResourceAccess is legal for the given pass flags.
    /// Returns true if access is valid, false otherwise.
    [[nodiscard]] constexpr auto IsAccessValidForPassType(ResourceAccess access, RGPassFlags passFlags) noexcept
        -> bool {
        auto bits = static_cast<uint32_t>(access);
        if (bits == 0) {
            return true;
        }

        // Present passes: only PresentSrc allowed
        if ((static_cast<uint8_t>(passFlags) & static_cast<uint8_t>(RGPassFlags::Present)) != 0) {
            return (bits & ~static_cast<uint32_t>(kPresentPassAccessMask)) == 0;
        }

        // Transfer-only passes: only TransferSrc/TransferDst allowed
        bool isTransferOnly = (static_cast<uint8_t>(passFlags) & static_cast<uint8_t>(RGPassFlags::Transfer)) != 0
                              && (static_cast<uint8_t>(passFlags) & static_cast<uint8_t>(RGPassFlags::Graphics)) == 0
                              && (static_cast<uint8_t>(passFlags) & static_cast<uint8_t>(RGPassFlags::Compute)) == 0;
        if (isTransferOnly) {
            return (bits & ~static_cast<uint32_t>(kTransferPassAccessMask)) == 0;
        }

        // Compute-only passes (including async): no graphics-only accesses
        bool isComputeOnly = (static_cast<uint8_t>(passFlags) & static_cast<uint8_t>(RGPassFlags::Compute)) != 0
                             && (static_cast<uint8_t>(passFlags) & static_cast<uint8_t>(RGPassFlags::Graphics)) == 0;
        if (isComputeOnly) {
            return (bits & static_cast<uint32_t>(kGraphicsOnlyAccessMask)) == 0;
        }

        // Graphics passes: all accesses valid
        return true;
    }

    // =========================================================================
    // Barrier mapping — resolve ResourceAccess to RHI PipelineStage + AccessFlags
    // =========================================================================

    struct BarrierMapping {
        rhi::PipelineStage stage = rhi::PipelineStage::None;
        rhi::AccessFlags access = rhi::AccessFlags::None;
        rhi::TextureLayout layout = rhi::TextureLayout::Undefined;
    };

    /// @brief Resolve a ResourceAccess flag to its corresponding RHI barrier components.
    /// This is constexpr and should be evaluated at compile time for static accesses.
    [[nodiscard]] constexpr auto ResolveBarrier(ResourceAccess access) noexcept -> BarrierMapping {
        switch (access) {
            case ResourceAccess::ShaderReadOnly:
                return {
                    .stage = rhi::PipelineStage::FragmentShader | rhi::PipelineStage::ComputeShader,
                    .access = rhi::AccessFlags::ShaderRead,
                    .layout = rhi::TextureLayout::ShaderReadOnly,
                };
            case ResourceAccess::DepthReadOnly:
                return {
                    .stage = rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                    .access = rhi::AccessFlags::DepthStencilRead,
                    .layout = rhi::TextureLayout::DepthStencilReadOnly,
                };
            case ResourceAccess::IndirectBuffer:
                return {
                    .stage = rhi::PipelineStage::DrawIndirect,
                    .access = rhi::AccessFlags::IndirectCommandRead,
                    .layout = rhi::TextureLayout::Undefined,  // buffers have no layout
                };
            case ResourceAccess::TransferSrc:
                return {
                    .stage = rhi::PipelineStage::Transfer,
                    .access = rhi::AccessFlags::TransferRead,
                    .layout = rhi::TextureLayout::TransferSrc,
                };
            case ResourceAccess::InputAttachment:
                return {
                    .stage = rhi::PipelineStage::FragmentShader,
                    .access = rhi::AccessFlags::InputAttachmentRead,
                    .layout = rhi::TextureLayout::ShaderReadOnly,
                };
            case ResourceAccess::AccelStructRead:
                return {
                    .stage = rhi::PipelineStage::RayTracingShader | rhi::PipelineStage::ComputeShader,
                    .access = rhi::AccessFlags::AccelStructRead,
                    .layout = rhi::TextureLayout::Undefined,
                };
            case ResourceAccess::ShadingRateRead:
                return {
                    .stage = rhi::PipelineStage::ShadingRateImage,
                    .access = rhi::AccessFlags::ShadingRateImageRead,
                    .layout = rhi::TextureLayout::ShadingRate,
                };
            case ResourceAccess::ShaderWrite:
                return {
                    .stage = rhi::PipelineStage::ComputeShader | rhi::PipelineStage::FragmentShader,
                    .access = rhi::AccessFlags::ShaderWrite,
                    .layout = rhi::TextureLayout::General,
                };
            case ResourceAccess::ColorAttachWrite:
                return {
                    .stage = rhi::PipelineStage::ColorAttachmentOutput,
                    .access = rhi::AccessFlags::ColorAttachmentWrite,
                    .layout = rhi::TextureLayout::ColorAttachment,
                };
            case ResourceAccess::DepthStencilWrite:
                return {
                    .stage = rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                    .access = rhi::AccessFlags::DepthStencilWrite,
                    .layout = rhi::TextureLayout::DepthStencilAttachment,
                };
            case ResourceAccess::TransferDst:
                return {
                    .stage = rhi::PipelineStage::Transfer,
                    .access = rhi::AccessFlags::TransferWrite,
                    .layout = rhi::TextureLayout::TransferDst,
                };
            case ResourceAccess::AccelStructWrite:
                return {
                    .stage = rhi::PipelineStage::AccelStructBuild,
                    .access = rhi::AccessFlags::AccelStructWrite,
                    .layout = rhi::TextureLayout::Undefined,
                };
            case ResourceAccess::PresentSrc:
                return {
                    .stage = rhi::PipelineStage::BottomOfPipe,
                    .access = rhi::AccessFlags::None,
                    .layout = rhi::TextureLayout::Present,
                };
            default: return {};
        }
    }

    /// @brief Resolve a combined ResourceAccess bitmask to merged barrier components.
    /// Iterates set bits and ORs the results.
    [[nodiscard]] constexpr auto ResolveBarrierCombined(ResourceAccess access) noexcept -> BarrierMapping {
        BarrierMapping result{};
        auto bits = static_cast<uint32_t>(access);
        while (bits != 0) {
            uint32_t lsb = bits & (~bits + 1);  // isolate lowest set bit
            auto mapping = ResolveBarrier(static_cast<ResourceAccess>(lsb));
            result.stage = result.stage | mapping.stage;
            result.access = result.access | mapping.access;
            if (mapping.layout != rhi::TextureLayout::Undefined) {
                result.layout = mapping.layout;  // last non-Undefined wins (single write expected)
            }
            assert(mapping.stage != rhi::PipelineStage::None || lsb == 0);
            bits &= ~lsb;
        }
        return result;
    }

    // =========================================================================
    // Resource descriptors — graph-level resource descriptions
    // =========================================================================

    /// @brief Sentinel values for subresource ranges.
    inline constexpr uint32_t kAllMips = std::numeric_limits<uint32_t>::max();
    inline constexpr uint32_t kAllLayers = std::numeric_limits<uint32_t>::max();

    /// @brief Texture descriptor for graph-declared transient textures.
    struct RGTextureDesc {
        rhi::TextureDimension dimension = rhi::TextureDimension::Tex2D;
        rhi::Format format = rhi::Format::RGBA8_UNORM;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        uint32_t sampleCount = 1;
        const char* debugName = nullptr;

        [[nodiscard]] constexpr auto operator==(const RGTextureDesc&) const noexcept -> bool = default;

        /// @brief Convert to RHI TextureDesc for physical allocation.
        /// Usage flags are inferred by the compiler from all pass accesses.
        [[nodiscard]] auto ToRhiDesc(rhi::TextureUsage inferredUsage) const noexcept -> rhi::TextureDesc {
            return {
                .dimension = dimension,
                .format = format,
                .width = width,
                .height = height,
                .depth = depth,
                .mipLevels = mipLevels,
                .arrayLayers = arrayLayers,
                .sampleCount = sampleCount,
                .usage = inferredUsage,
                .memory = rhi::MemoryLocation::GpuOnly,
                .transient = true,
                .debugName = debugName,
            };
        }
    };

    /// @brief Buffer descriptor for graph-declared transient buffers.
    struct RGBufferDesc {
        uint64_t size = 0;
        const char* debugName = nullptr;

        [[nodiscard]] constexpr auto operator==(const RGBufferDesc&) const noexcept -> bool = default;

        /// @brief Convert to RHI BufferDesc for physical allocation.
        [[nodiscard]] auto ToRhiDesc(rhi::BufferUsage inferredUsage) const noexcept -> rhi::BufferDesc {
            return {
                .size = size,
                .usage = inferredUsage,
                .memory = rhi::MemoryLocation::GpuOnly,
                .transient = true,
                .debugName = debugName,
            };
        }
    };

    // =========================================================================
    // Per-pass resource access record (SSA edge in the DAG)
    // =========================================================================

    /// @brief Records a single resource access within a pass, including subresource range.
    struct RGResourceAccess {
        RGResourceHandle handle = {};  ///< Virtual resource handle (SSA-versioned)
        ResourceAccess access = ResourceAccess::None;
        uint32_t mipLevel = kAllMips;      ///< kAllMips = whole resource
        uint32_t arrayLayer = kAllLayers;  ///< kAllLayers = whole resource
    };

    // =========================================================================
    // Resource kind discriminator
    // =========================================================================

    enum class RGResourceKind : uint8_t {
        Texture,
        Buffer,
    };

    // =========================================================================
    // Resource node — internal storage for a logical resource
    // =========================================================================

    struct RGResourceNode {
        RGResourceKind kind = RGResourceKind::Texture;
        bool imported = false;          ///< true = external resource, false = transient
        bool lifetimeExtended = false;  ///< true = history resource, excluded from aliasing
        uint16_t currentVersion = 0;    ///< Current SSA version counter

        union {
            RGTextureDesc textureDesc;
            RGBufferDesc bufferDesc;
        };

        // Imported resource physical handles (only valid if imported == true)
        rhi::TextureHandle importedTexture = {};
        rhi::BufferHandle importedBuffer = {};

        const char* name = nullptr;

        RGResourceNode() : textureDesc{} {}
    };

    // =========================================================================
    // Dependency edge types
    // =========================================================================

    enum class HazardType : uint8_t {
        RAW,  ///< Read-After-Write (execution + memory barrier)
        WAR,  ///< Write-After-Read (execution-only barrier)
        WAW,  ///< Write-After-Write (execution + memory barrier)
    };

    struct DependencyEdge {
        uint32_t srcPass = RGPassHandle::kInvalid;
        uint32_t dstPass = RGPassHandle::kInvalid;
        uint16_t resourceIndex = 0;
        HazardType hazard = HazardType::RAW;
    };

    // =========================================================================
    // Lambda type aliases — InlineFunction (SBO-only, no heap allocation)
    // =========================================================================

    /// @brief Default inline capacity for render-graph lambdas.
    /// 64 bytes covers typical captures (4-8 pointers/handles).
    inline constexpr size_t kPassFnCapacity = 64;

    using PassSetupFn = core::InlineFunction<void(PassBuilder&), kPassFnCapacity>;
    using PassExecuteFn = core::InlineFunction<void(RenderPassContext&), kPassFnCapacity>;
    using ConditionFn = core::InlineFunction<bool(), kPassFnCapacity>;

    // =========================================================================
    // Attachment info — per-attachment load/store/clear configuration
    // =========================================================================

    /// @brief Per-attachment rendering configuration declared by pass authors.
    /// Stored in RGPassNode for each color and depth/stencil attachment.
    /// The executor uses this to build RenderingAttachment descriptors with
    /// correct load/store ops instead of hardcoded Clear/Store.
    struct RGAttachmentInfo {
        RGResourceHandle handle;
        uint32_t slotIndex = 0;  ///< Color attachment slot (0-7), ignored for depth
        rhi::AttachmentLoadOp loadOp = rhi::AttachmentLoadOp::Clear;
        rhi::AttachmentStoreOp storeOp = rhi::AttachmentStoreOp::Store;
        rhi::ClearValue clearValue = {};
        bool isDepthStencil = false;
    };

    /// @brief Maximum color attachments per pass (matches GPU spec minimum).
    inline constexpr uint32_t kMaxColorAttachments = 8;

    // =========================================================================
    // Pass node — internal storage for a declared pass
    // =========================================================================

    struct RGPassNode {
        const char* name = nullptr;
        RGPassFlags flags = RGPassFlags::None;
        RGQueueType queue = RGQueueType::Graphics;
        int32_t orderHint = 0;        ///< User-specified ordering hint
        bool hasSideEffects = false;  ///< If true, never cull
        bool enabled = true;          ///< Result of condition evaluation

        PassExecuteFn executeFn;
        ConditionFn conditionFn;  ///< nullptr = always enabled

        // Resource accesses — spans into LinearAllocator-owned memory
        std::span<RGResourceAccess> reads;
        std::span<RGResourceAccess> writes;

        // Attachment configuration — populated by PassBuilder::WriteColorAttachment / WriteDepthStencil
        std::vector<RGAttachmentInfo> colorAttachments;
        RGAttachmentInfo depthStencilAttachment = {};
        bool hasDepthStencil = false;
    };

    // =========================================================================
    // Compiled barrier command
    // =========================================================================

    struct BarrierCommand {
        uint16_t resourceIndex = 0;
        ResourceAccess srcAccess = ResourceAccess::None;
        ResourceAccess dstAccess = ResourceAccess::None;
        rhi::TextureLayout srcLayout = rhi::TextureLayout::Undefined;
        rhi::TextureLayout dstLayout = rhi::TextureLayout::Undefined;
        uint32_t mipLevel = kAllMips;
        uint32_t arrayLayer = kAllLayers;
        bool isSplitRelease = false;  ///< true = release half of split barrier
        bool isSplitAcquire = false;  ///< true = acquire half of split barrier
        bool isCrossQueue = false;
        bool isAliasingBarrier = false;  ///< true = aliasing barrier (UNDEFINED->initial, no src dependency)
        RGQueueType srcQueue = RGQueueType::Graphics;
        RGQueueType dstQueue = RGQueueType::Graphics;
    };

    // =========================================================================
    // Cross-queue synchronization point
    // =========================================================================

    struct CrossQueueSyncPoint {
        RGQueueType srcQueue = RGQueueType::Graphics;
        RGQueueType dstQueue = RGQueueType::Graphics;
        uint32_t srcPassIndex = RGPassHandle::kInvalid;
        uint32_t dstPassIndex = RGPassHandle::kInvalid;
        uint64_t timelineValue = 0;  ///< Allocated at execution time
    };

    // =========================================================================
    // Heap group classification for transient resource aliasing (§5.6.2)
    // =========================================================================

    enum class HeapGroupType : uint8_t {
        RtDs,           ///< Render target / depth-stencil textures
        NonRtDs,        ///< Shader-read-only textures (SRV, UAV without RT/DS)
        Buffer,         ///< Transient buffers
        MixedFallback,  ///< D3D12 Tier 1 fallback (all resource types)
    };

    inline constexpr size_t kHeapGroupCount = 4;

    // =========================================================================
    // Aliasing slot — a reusable memory region within a heap group
    // =========================================================================

    struct AliasingSlot {
        uint32_t slotIndex = 0;
        HeapGroupType heapGroup = HeapGroupType::RtDs;
        uint64_t size = 0;           ///< Slot capacity in bytes
        uint64_t alignment = 0;      ///< Maximum alignment of any resource in this slot
        uint64_t heapOffset = 0;     ///< Byte offset within the heap group
        uint32_t lifetimeStart = 0;  ///< Earliest firstPass (topo order position)
        uint32_t lifetimeEnd = 0;    ///< Latest lastPass (topo order position)
    };

    // =========================================================================
    // Aliasing layout — complete Stage 7 output
    // =========================================================================

    struct AliasingLayout {
        std::vector<AliasingSlot> slots;       ///< All heap slots
        std::vector<uint32_t> resourceToSlot;  ///< resource index -> slot index (kNotAliased if not aliased)
        std::array<uint64_t, kHeapGroupCount> heapGroupSizes = {};  ///< Total heap size per HeapGroupType
        std::vector<BarrierCommand> aliasingBarriers;               ///< Aliasing barriers to inject at pass start
        std::vector<uint32_t> aliasingBarrierPassPos;  ///< Topo order position where each aliasing barrier goes

        static constexpr uint32_t kNotAliased = std::numeric_limits<uint32_t>::max();
    };

    // =========================================================================
    // Resource size/alignment estimation for aliasing (compile-time)
    // =========================================================================

    inline constexpr uint64_t kAlignmentTexture = 65536;               ///< 64 KB
    inline constexpr uint64_t kAlignmentMsaa = 4ULL * 1024ULL * 1024;  ///< 4 MB for MSAA render targets
    inline constexpr uint64_t kAlignmentBuffer = 256;                  ///< 256 B

    [[nodiscard]] constexpr auto EstimateTextureSize(const RGTextureDesc& desc) noexcept -> uint64_t {
        uint64_t bpp = rhi::FormatBytesPerPixel(desc.format);
        if (bpp == 0) {
            bpp = 4;
        }
        uint64_t total = 0;
        uint32_t w = desc.width;
        uint32_t h = desc.height;
        for (uint32_t mip = 0; mip < desc.mipLevels; ++mip) {
            total += static_cast<uint64_t>(w > 0 ? w : 1) * static_cast<uint64_t>(h > 0 ? h : 1) * desc.depth * bpp;
            w >>= 1;
            h >>= 1;
        }
        return total * desc.arrayLayers * (desc.sampleCount > 1 ? desc.sampleCount : 1);
    }

    [[nodiscard]] constexpr auto EstimateBufferSize(const RGBufferDesc& desc) noexcept -> uint64_t {
        return desc.size;
    }

    [[nodiscard]] constexpr auto EstimateTextureAlignment(const RGTextureDesc& desc) noexcept -> uint64_t {
        return (desc.sampleCount > 1) ? kAlignmentMsaa : kAlignmentTexture;
    }

    [[nodiscard]] constexpr auto EstimateBufferAlignment([[maybe_unused]] const RGBufferDesc& desc) noexcept
        -> uint64_t {
        return kAlignmentBuffer;
    }

    [[nodiscard]] constexpr auto ClassifyHeapGroup(RGResourceKind kind, ResourceAccess combinedAccess) noexcept
        -> HeapGroupType {
        if (kind == RGResourceKind::Buffer) {
            return HeapGroupType::Buffer;
        }
        bool isRtDs = (combinedAccess & (ResourceAccess::ColorAttachWrite | ResourceAccess::DepthStencilWrite))
                      != ResourceAccess::None;
        return isRtDs ? HeapGroupType::RtDs : HeapGroupType::NonRtDs;
    }

    // =========================================================================
    // Subpass dependency — replaces inter-pass barriers within a merged group (§5.7.2)
    // =========================================================================

    struct SubpassDependency {
        uint32_t srcSubpass = 0;  ///< Index within merged group (0-based)
        uint32_t dstSubpass = 0;
        ResourceAccess srcAccess = ResourceAccess::None;
        ResourceAccess dstAccess = ResourceAccess::None;
        rhi::TextureLayout srcLayout = rhi::TextureLayout::Undefined;
        rhi::TextureLayout dstLayout = rhi::TextureLayout::Undefined;
        bool byRegion = true;  ///< VK_DEPENDENCY_BY_REGION_BIT (tile-local dependency)
    };

    // =========================================================================
    // Merged render pass group — consecutive passes sharing tile memory (§5.7)
    // =========================================================================

    struct MergedRenderPassGroup {
        std::vector<uint32_t> subpassIndices;  ///< Indices into CompiledRenderGraph::passes[]
        std::vector<SubpassDependency> dependencies;
        std::vector<uint16_t> sharedAttachments;  ///< Resource indices used across subpasses
        uint32_t renderAreaWidth = 0;
        uint32_t renderAreaHeight = 0;
    };

    // =========================================================================
    // Adaptation pass — auxiliary pass injected by Stage 9 (§5.1 Stage 9)
    // =========================================================================

    struct AdaptationPassInfo {
        uint32_t originalPassIndex = 0;     ///< The pass this adaptation supports
        uint32_t insertBeforePosition = 0;  ///< Topo order position to insert before
        RGQueueType queue = RGQueueType::Graphics;
        rhi::adaptation::Feature feature{};    ///< Which feature triggered the adaptation
        rhi::adaptation::Strategy strategy{};  ///< How the feature is adapted
        const char* description = nullptr;     ///< Human-readable, from adaptation Rule
    };

    // =========================================================================
    // Command batch — submission unit for a single queue (§5.1 Stage 10)
    // =========================================================================

    struct CommandBatch {
        RGQueueType queue = RGQueueType::Graphics;
        std::vector<uint32_t> passIndices;  ///< Indices into CompiledRenderGraph::passes[]
        bool signalTimeline = false;        ///< true = allocate + signal timeline value after this batch

        struct WaitEntry {
            RGQueueType srcQueue = RGQueueType::Graphics;
            uint64_t timelineValue = 0;  ///< Cross-queue timeline value to wait on
        };
        std::vector<WaitEntry> waits;  ///< Cross-queue dependencies this batch must wait for
    };

    // =========================================================================
    // Compiled render graph — immutable output of the compiler
    // =========================================================================

    struct CompiledPassInfo {
        uint32_t passIndex = 0;  ///< Index into original pass array
        RGQueueType queue = RGQueueType::Graphics;
        std::vector<BarrierCommand> acquireBarriers;  ///< Barriers before pass execution
        std::vector<BarrierCommand> releaseBarriers;  ///< Barriers after pass execution
    };

    struct CompiledRenderGraph {
        std::vector<CompiledPassInfo> passes;  ///< Topologically sorted, active passes only
        std::vector<CrossQueueSyncPoint> syncPoints;
        std::vector<DependencyEdge> edges;

        struct ResourceLifetime {
            uint16_t resourceIndex = 0;
            uint32_t firstPass = 0;  ///< Position in topological order
            uint32_t lastPass = 0;   ///< Position in topological order
        };
        std::vector<ResourceLifetime> lifetimes;

        AliasingLayout aliasing;  ///< Stage 7 output

        std::vector<MergedRenderPassGroup> mergedGroups;   ///< Stage 8 output
        std::vector<AdaptationPassInfo> adaptationPasses;  ///< Stage 9 output

        /// @brief Stage 10 output: submission batches grouped by queue with sync metadata
        std::vector<CommandBatch> batches;

        struct StructuralHash {
            uint64_t passCount = 0;
            uint64_t resourceCount = 0;
            uint64_t edgeHash = 0;
            uint64_t conditionHash = 0;
            uint64_t descHash = 0;
            [[nodiscard]] auto operator==(const StructuralHash&) const noexcept -> bool = default;
        };
        StructuralHash hash = {};
    };

    // =========================================================================
    // Scheduler strategy for barrier-aware reordering
    // =========================================================================

    enum class SchedulerStrategy : uint8_t {
        MinBarriers,  ///< Minimize barrier count (best for discrete GPUs)
        MinMemory,    ///< Minimize peak transient memory
        MinLatency,   ///< Minimize critical path (maximize async overlap)
        Balanced,     ///< Weighted combination (default)
    };

}  // namespace miki::rg
