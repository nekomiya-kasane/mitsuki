// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §10 Acceleration Structure tests (T1 only).
// Covers: BLAS/TLAS build size queries, BLAS/TLAS creation/destruction,
// AccelStructInstance layout, geometry descriptor validation.

#include <gtest/gtest.h>

#include "RhiTestFixture.h"

#include "miki/rhi/AccelerationStructure.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Resources.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// §10.0 AccelStructInstance layout — static assertions (compile-time)
// ============================================================================

static_assert(
    sizeof(AccelStructInstance) == 64,
    "AccelStructInstance must be 64 bytes (matches VkAccelerationStructureInstanceKHR)"
);

// ============================================================================
// §10.1 AccelStruct Geometry Descriptor — struct validation (pure CPU)
// ============================================================================

TEST(AccelStructGeometryDesc, DefaultsValid) {
    AccelStructGeometryDesc desc{};
    EXPECT_EQ(desc.type, AccelStructGeometryType::Triangles);
    EXPECT_EQ(desc.vertexFormat, Format::RGBA32_FLOAT);
    EXPECT_EQ(desc.indexType, IndexType::Uint32);
    EXPECT_EQ(desc.flags, AccelStructGeometryFlags::None);
}

TEST(BLASDesc, DefaultBuildFlags) {
    BLASDesc desc{};
    EXPECT_EQ(desc.flags, AccelStructBuildFlags::PreferFastTrace);
}

TEST(TLASDesc, DefaultBuildFlags) {
    TLASDesc desc{};
    EXPECT_EQ(desc.flags, AccelStructBuildFlags::PreferFastTrace);
    EXPECT_EQ(desc.instanceCount, 0u);
}

TEST(AccelStructBuildSizes, DefaultZero) {
    AccelStructBuildSizes sizes{};
    EXPECT_EQ(sizes.accelerationStructureSize, 0u);
    EXPECT_EQ(sizes.buildScratchSize, 0u);
    EXPECT_EQ(sizes.updateScratchSize, 0u);
}

TEST(DecompressBufferDesc, DefaultGDeflate) {
    DecompressBufferDesc desc{};
    EXPECT_EQ(desc.format, CompressionFormat::GDeflate);
}

// ============================================================================
// §10.2 BLAS Build Size Query (T1 only)
// ============================================================================

class RhiAccelStructTest : public RhiTest {};

TEST_P(RhiAccelStructTest, GetBLASBuildSizes) {
    RequireFeature(DeviceFeature::AccelerationStructure);

    // Create vertex + index buffers for geometry
    BufferDesc vbDesc{.size = 1024, .usage = BufferUsage::AccelStructInput, .memory = MemoryLocation::GpuOnly};
    BufferDesc ibDesc{.size = 512, .usage = BufferUsage::AccelStructInput, .memory = MemoryLocation::GpuOnly};
    auto vb = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(vbDesc); });
    auto ib = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(ibDesc); });
    ASSERT_TRUE(vb.has_value() && ib.has_value());

    AccelStructGeometryDesc geom{
        .type = AccelStructGeometryType::Triangles,
        .vertexBuffer = *vb,
        .vertexStride = 12,
        .vertexFormat = Format::RGB32_FLOAT,
        .vertexCount = 36,
        .indexBuffer = *ib,
        .indexType = IndexType::Uint32,
        .triangleCount = 12,
        .transformBuffer = {},
    };
    BLASDesc blasDesc{.geometries = std::span{&geom, 1}};

    auto sizes = Dev().Dispatch([&](auto& dev) { return dev.GetBLASBuildSizes(blasDesc); });
    EXPECT_GT(sizes.accelerationStructureSize, 0u);
    EXPECT_GT(sizes.buildScratchSize, 0u);

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyBuffer(*vb);
        dev.DestroyBuffer(*ib);
    });
}

// ============================================================================
// §10.3 TLAS Build Size Query (T1 only)
// ============================================================================

TEST_P(RhiAccelStructTest, GetTLASBuildSizes) {
    RequireFeature(DeviceFeature::AccelerationStructure);

    BufferDesc instBuf{.size = 64 * 10, .usage = BufferUsage::AccelStructInput, .memory = MemoryLocation::GpuOnly};
    auto ib = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(instBuf); });
    ASSERT_TRUE(ib.has_value());

    TLASDesc tlasDesc{.instanceBuffer = *ib, .instanceCount = 10};
    auto sizes = Dev().Dispatch([&](auto& dev) { return dev.GetTLASBuildSizes(tlasDesc); });
    EXPECT_GT(sizes.accelerationStructureSize, 0u);
    EXPECT_GT(sizes.buildScratchSize, 0u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*ib); });
}

// ============================================================================
// §10.4 BLAS/TLAS Create & Destroy (T1 only)
// ============================================================================

TEST_P(RhiAccelStructTest, CreateAndDestroyBLAS) {
    RequireFeature(DeviceFeature::AccelerationStructure);

    BufferDesc vbDesc{.size = 1024, .usage = BufferUsage::AccelStructInput, .memory = MemoryLocation::GpuOnly};
    BufferDesc ibDesc{.size = 512, .usage = BufferUsage::AccelStructInput, .memory = MemoryLocation::GpuOnly};
    auto vb = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(vbDesc); });
    auto ib = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(ibDesc); });
    ASSERT_TRUE(vb.has_value() && ib.has_value());

    AccelStructGeometryDesc geom{
        .type = AccelStructGeometryType::Triangles,
        .vertexBuffer = *vb,
        .vertexStride = 12,
        .vertexFormat = Format::RGB32_FLOAT,
        .vertexCount = 36,
        .indexBuffer = *ib,
        .indexType = IndexType::Uint32,
        .triangleCount = 12,
        .transformBuffer = {},
    };
    BLASDesc blasDesc{.geometries = std::span{&geom, 1}};

    auto blas = Dev().Dispatch([&](auto& dev) { return dev.CreateBLAS(blasDesc); });
    ASSERT_TRUE(blas.has_value());
    EXPECT_TRUE(blas->IsValid());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyAccelStruct(*blas);
        dev.DestroyBuffer(*vb);
        dev.DestroyBuffer(*ib);
    });
}

TEST_P(RhiAccelStructTest, CreateAndDestroyTLAS) {
    RequireFeature(DeviceFeature::AccelerationStructure);

    BufferDesc instBuf{.size = 64 * 4, .usage = BufferUsage::AccelStructInput, .memory = MemoryLocation::GpuOnly};
    auto ib = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(instBuf); });
    ASSERT_TRUE(ib.has_value());

    TLASDesc tlasDesc{.instanceBuffer = *ib, .instanceCount = 4};
    auto tlas = Dev().Dispatch([&](auto& dev) { return dev.CreateTLAS(tlasDesc); });
    ASSERT_TRUE(tlas.has_value());
    EXPECT_TRUE(tlas->IsValid());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyAccelStruct(*tlas);
        dev.DestroyBuffer(*ib);
    });
}

TEST_P(RhiAccelStructTest, DestroyInvalidAccelStructSilent) {
    RequireFeature(DeviceFeature::AccelerationStructure);
    AccelStructHandle invalid{};
    Dev().Dispatch([&](auto& dev) { dev.DestroyAccelStruct(invalid); });
}

INSTANTIATE_TEST_SUITE_P(Tier1, RhiAccelStructTest, ::testing::ValuesIn(GetTier1Backends()), BackendName);
