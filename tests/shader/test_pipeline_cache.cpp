// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 1a unit tests for PipelineCache: header validation, load/save,
// graceful handling of missing or corrupted cache files.
//
// These tests use mock/null device handles since PipelineCache gracefully
// falls back to empty cache for non-Vulkan/D3D12 backends.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>

#include "miki/rhi/PipelineCache.h"

using namespace miki::rhi;

namespace fs = std::filesystem;

// ============================================================================
// PipelineCacheHeader structural tests
// ============================================================================

TEST(PipelineCacheHeader, DefaultMagicAndVersion) {
    PipelineCacheHeader header;
    EXPECT_EQ(header.magic, 0x4D4B5043u);  // "MKPC"
    EXPECT_EQ(header.version, 1u);
    EXPECT_EQ(header.driverVersion, 0u);
    EXPECT_EQ(header.deviceId, 0u);
    EXPECT_EQ(header.dataSize, 0u);
}

TEST(PipelineCacheHeader, SizeIsStable) {
    // Header must be a fixed size for binary compatibility
    EXPECT_EQ(sizeof(PipelineCacheHeader), 24u);
}

// ============================================================================
// Load from nonexistent file
// ============================================================================

TEST(PipelineCache, LoadNonexistentFileCreatesEmptyCache) {
    DeviceHandle nullDevice{};  // Default/null device handle
    auto result = PipelineCache::Load(nullDevice, "nonexistent_cache_12345.bin");
    ASSERT_TRUE(result.has_value()) << "Load should succeed with empty cache on missing file";
    EXPECT_TRUE(result->IsValid());
}

// ============================================================================
// Save and reload cycle (no-op backend)
// ============================================================================

TEST(PipelineCache, SaveAndReloadNoOp) {
    auto tmpDir = fs::temp_directory_path() / "miki_test_pipeline_cache";
    fs::create_directories(tmpDir);
    auto cachePath = tmpDir / "test_cache.bin";

    DeviceHandle nullDevice{};

    // Load (create empty)
    auto loadResult = PipelineCache::Load(nullDevice, cachePath);
    ASSERT_TRUE(loadResult.has_value());

    // Save
    auto saveResult = loadResult->Save(cachePath);
    EXPECT_TRUE(saveResult.has_value()) << "Save should succeed (no-op for non-Vulkan/D3D12)";

    // Cleanup
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
}

// ============================================================================
// Corrupted file handling
// ============================================================================

TEST(PipelineCache, LoadCorruptedFileCreatesEmptyCache) {
    auto tmpDir = fs::temp_directory_path() / "miki_test_pipeline_cache_corrupt";
    fs::create_directories(tmpDir);
    auto cachePath = tmpDir / "corrupt_cache.bin";

    // Write garbage data
    {
        std::ofstream f(cachePath, std::ios::binary);
        const char garbage[] = "THIS IS NOT A VALID PIPELINE CACHE FILE";
        f.write(garbage, sizeof(garbage));
    }

    DeviceHandle nullDevice{};
    auto result = PipelineCache::Load(nullDevice, cachePath);
    ASSERT_TRUE(result.has_value()) << "Load should gracefully handle corruption";
    EXPECT_TRUE(result->IsValid());

    // Cleanup
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
}

TEST(PipelineCache, LoadWrongMagicCreatesEmptyCache) {
    auto tmpDir = fs::temp_directory_path() / "miki_test_pipeline_cache_magic";
    fs::create_directories(tmpDir);
    auto cachePath = tmpDir / "wrong_magic.bin";

    // Write header with wrong magic
    {
        PipelineCacheHeader header;
        header.magic = 0xDEADBEEF;  // Wrong magic
        header.dataSize = 0;

        std::ofstream f(cachePath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }

    DeviceHandle nullDevice{};
    auto result = PipelineCache::Load(nullDevice, cachePath);
    ASSERT_TRUE(result.has_value()) << "Load should gracefully handle wrong magic";
    EXPECT_TRUE(result->IsValid());

    // Cleanup
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
}
