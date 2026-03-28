/** @file AccelerationStructure.h
 *  @brief BLAS/TLAS descriptors and instance data for ray tracing (T1 only).
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <span>

#include "miki/rhi/Format.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"

namespace miki::rhi {

    // =========================================================================
    // Geometry descriptor (for BLAS)
    // =========================================================================

    struct AccelStructGeometryDesc {
        AccelStructGeometryType type = AccelStructGeometryType::Triangles;
        BufferHandle vertexBuffer;
        uint64_t vertexOffset = 0;
        uint32_t vertexStride = 0;
        Format vertexFormat = Format::RGBA32_FLOAT;
        uint32_t vertexCount = 0;
        BufferHandle indexBuffer;
        uint64_t indexOffset = 0;
        IndexType indexType = IndexType::Uint32;
        uint32_t triangleCount = 0;
        BufferHandle transformBuffer;  ///< Optional 3x4 row-major
        uint64_t transformOffset = 0;
        AccelStructGeometryFlags flags = AccelStructGeometryFlags::None;
    };

    // =========================================================================
    // BLAS descriptor
    // =========================================================================

    struct BLASDesc {
        std::span<const AccelStructGeometryDesc> geometries;
        AccelStructBuildFlags flags = AccelStructBuildFlags::PreferFastTrace;
    };

    // =========================================================================
    // Instance data (matches VkAccelerationStructureInstanceKHR layout)
    // =========================================================================

    struct AccelStructInstance {
        float transform[3][4];  ///< 3x4 row-major affine transform
        uint32_t instanceCustomIndex : 24;
        uint32_t mask : 8;
        uint32_t sbtRecordOffset : 24;
        uint32_t flags : 8;                       ///< AccelStructInstanceFlags
        uint64_t accelerationStructureReference;  ///< BDA or GPU handle of BLAS
    };
    static_assert(sizeof(AccelStructInstance) == 64);

    // =========================================================================
    // TLAS descriptor
    // =========================================================================

    struct TLASDesc {
        BufferHandle instanceBuffer;  ///< Array of AccelStructInstance (64B each)
        uint32_t instanceCount = 0;
        AccelStructBuildFlags flags = AccelStructBuildFlags::PreferFastTrace;
    };

    // =========================================================================
    // Build sizes (pre-query before building)
    // =========================================================================

    struct AccelStructBuildSizes {
        uint64_t accelerationStructureSize = 0;
        uint64_t buildScratchSize = 0;
        uint64_t updateScratchSize = 0;  ///< 0 if !AllowUpdate
    };

    // =========================================================================
    // Decompression (future: CmdDecompressBuffer)
    // =========================================================================

    struct DecompressBufferDesc {
        BufferHandle srcBuffer;
        uint64_t srcOffset = 0;
        uint64_t srcSize = 0;
        BufferHandle dstBuffer;
        uint64_t dstOffset = 0;
        uint64_t dstSize = 0;
        CompressionFormat format = CompressionFormat::GDeflate;
    };

}  // namespace miki::rhi
