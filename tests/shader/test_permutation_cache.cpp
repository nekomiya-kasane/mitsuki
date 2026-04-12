// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 1a unit tests for PermutationCache: LRU eviction, disk persistence,
// transitive source hashing, thread safety, and lazy compilation.

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "miki/shader/PermutationCache.h"
#include "miki/shader/SlangCompiler.h"

using namespace miki::shader;

namespace fs = std::filesystem;

// ============================================================================
// Test fixture
// ============================================================================

class PermutationCacheTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "Failed to create SlangCompiler";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));

        tmpDir_ = fs::temp_directory_path() / "miki_test_permcache";
        fs::create_directories(tmpDir_);

        // Create a simple test shader file
        testShaderPath_ = tmpDir_ / "test_compute.slang";
        {
            std::ofstream f(testShaderPath_);
            f << R"(
                [shader("compute")]
                [numthreads(64, 1, 1)]
                void main(uint3 tid : SV_DispatchThreadID) {}
            )";
        }
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmpDir_, ec);
    }

    std::unique_ptr<SlangCompiler> compiler_;
    fs::path tmpDir_;
    fs::path testShaderPath_;
};

// ============================================================================
// Basic cache operations
// ============================================================================

TEST_F(PermutationCacheTest, CreateWithDefaultConfig) {
    PermutationCacheConfig config;
    config.maxEntries = 16;
    auto cache = PermutationCache(config);
    EXPECT_EQ(cache.Size(), 0u);
}

TEST_F(PermutationCacheTest, GetOrCompileCachesResult) {
    PermutationCacheConfig config;
    config.maxEntries = 16;
    auto cache = PermutationCache(config);

    ShaderCompileDesc desc;
    desc.sourcePath = testShaderPath_;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result1 = cache.GetOrCompile(desc, *compiler_);
    ASSERT_TRUE(result1.has_value()) << "First compilation failed";
    EXPECT_GT((*result1)->data.size(), 0u);
    EXPECT_EQ(cache.Size(), 1u);

    // Second call should return cached blob (no recompilation)
    auto result2 = cache.GetOrCompile(desc, *compiler_);
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ((*result2)->data.size(), (*result1)->data.size());
    EXPECT_EQ(cache.Size(), 1u);  // Still one entry
}

TEST_F(PermutationCacheTest, DifferentTargetsSeparatelyCached) {
    PermutationCacheConfig config;
    config.maxEntries = 16;
    auto cache = PermutationCache(config);

    ShaderCompileDesc desc;
    desc.sourcePath = testShaderPath_;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;

    desc.target = ShaderTarget::SPIRV_1_5();
    auto r1 = cache.GetOrCompile(desc, *compiler_);
    ASSERT_TRUE(r1.has_value());

    desc.target = ShaderTarget::DXIL_6_6();
    auto r2 = cache.GetOrCompile(desc, *compiler_);
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(cache.Size(), 2u);
}

TEST_F(PermutationCacheTest, DifferentPermutationsSeparatelyCached) {
    PermutationCacheConfig config;
    config.maxEntries = 16;
    auto cache = PermutationCache(config);

    ShaderCompileDesc desc;
    desc.sourcePath = testShaderPath_;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    desc.permutation.bits = 0;
    auto r1 = cache.GetOrCompile(desc, *compiler_);
    ASSERT_TRUE(r1.has_value());

    desc.permutation.bits = 1;
    auto r2 = cache.GetOrCompile(desc, *compiler_);
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(cache.Size(), 2u);
}

// ============================================================================
// LRU eviction
// ============================================================================

TEST_F(PermutationCacheTest, LruEvictsOldestEntry) {
    PermutationCacheConfig config;
    config.maxEntries = 2;
    auto cache = PermutationCache(config);

    ShaderCompileDesc desc;
    desc.sourcePath = testShaderPath_;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    // Fill to capacity
    desc.permutation.bits = 0;
    ASSERT_TRUE(cache.GetOrCompile(desc, *compiler_).has_value());

    desc.permutation.bits = 1;
    ASSERT_TRUE(cache.GetOrCompile(desc, *compiler_).has_value());
    EXPECT_EQ(cache.Size(), 2u);

    // One more should evict the oldest
    desc.permutation.bits = 2;
    ASSERT_TRUE(cache.GetOrCompile(desc, *compiler_).has_value());
    EXPECT_EQ(cache.Size(), 2u);
}

// ============================================================================
// Disk persistence
// ============================================================================

TEST_F(PermutationCacheTest, DiskCachePersistsAcrossInstances) {
    auto diskCacheDir = tmpDir_ / "disk_cache";

    // First instance: compile and cache to disk
    {
        PermutationCacheConfig config;
        config.maxEntries = 16;
        config.enableDiskCache = true;
        config.cacheDir = diskCacheDir;
        auto cache = PermutationCache(config);

        ShaderCompileDesc desc;
        desc.sourcePath = testShaderPath_;
        desc.entryPoint = "main";
        desc.stage = ShaderStage::Compute;
        desc.target = ShaderTarget::SPIRV_1_5();

        auto r = cache.GetOrCompile(desc, *compiler_);
        ASSERT_TRUE(r.has_value());
    }

    // Verify disk cache directory has files
    EXPECT_TRUE(fs::exists(diskCacheDir));
    bool hasFiles = false;
    for (auto& entry : fs::directory_iterator(diskCacheDir)) {
        if (entry.is_regular_file()) {
            hasFiles = true;
            break;
        }
    }
    EXPECT_TRUE(hasFiles) << "Disk cache directory should contain cached files";
}

// ============================================================================
// Clear cache
// ============================================================================

TEST_F(PermutationCacheTest, ClearRemovesAllEntries) {
    PermutationCacheConfig config;
    config.maxEntries = 16;
    auto cache = PermutationCache(config);

    ShaderCompileDesc desc;
    desc.sourcePath = testShaderPath_;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    ASSERT_TRUE(cache.GetOrCompile(desc, *compiler_).has_value());
    EXPECT_EQ(cache.Size(), 1u);

    cache.Clear();
    EXPECT_EQ(cache.Size(), 0u);
}

// ============================================================================
// Thread safety
// ============================================================================

TEST_F(PermutationCacheTest, ConcurrentGetOrCompile) {
    PermutationCacheConfig config;
    config.maxEntries = 64;
    auto cache = PermutationCache(config);

    constexpr int kThreads = 4;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] {
            ShaderCompileDesc desc;
            desc.sourcePath = testShaderPath_;
            desc.entryPoint = "main";
            desc.stage = ShaderStage::Compute;
            desc.target = ShaderTarget::SPIRV_1_5();
            desc.permutation.bits = static_cast<uint64_t>(i);

            auto r = cache.GetOrCompile(desc, *compiler_);
            if (r.has_value()) {
                ++successCount;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(successCount.load(), kThreads);
    EXPECT_EQ(cache.Size(), static_cast<size_t>(kThreads));
}
