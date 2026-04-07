/** @file RenderGraphTypes.h
 *  @brief Core data types for the miki Render Graph system.
 *
 *  Defines handles, descriptors, resource access flags, barrier mapping,
 *  and all fundamental types used across Builder, Compiler, and Executor.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <cassert>

#include "miki/core/Hash.h"
#include "miki/core/TypeTraits.h"
#include "miki/rhi/Format.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"

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
    // Lambda type aliases
    // =========================================================================

    // Note: std::move_only_function is C++23 but not yet available in all libc++ versions.
    // Fall back to std::function for now. This allows move-only captures via shared_ptr if needed.
    using PassSetupFn = std::function<void(PassBuilder&)>;
    using PassExecuteFn = std::function<void(RenderPassContext&)>;
    using ConditionFn = std::function<bool()>;

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

        // Resource accesses recorded during setup phase
        std::vector<RGResourceAccess> reads;
        std::vector<RGResourceAccess> writes;
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

        // Resource lifetime intervals for aliasing
        struct ResourceLifetime {
            uint16_t resourceIndex = 0;
            uint32_t firstPass = 0;
            uint32_t lastPass = 0;
        };
        std::vector<ResourceLifetime> lifetimes;

        // Structural hash for caching
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
