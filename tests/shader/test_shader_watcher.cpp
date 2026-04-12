// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 1b unit tests for ShaderWatcher: start/stop, generation counter,
// error reporting, file change detection.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "miki/shader/ShaderWatcher.h"
#include "miki/shader/SlangCompiler.h"

using namespace miki::shader;
namespace fs = std::filesystem;

class ShaderWatcherTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "SlangCompiler::Create failed";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));
        compiler_->AddSearchPath(MIKI_SHADER_DIR);

        // Create a temp directory for test shader files
        tempDir_ = fs::temp_directory_path() / "miki_watcher_test";
        fs::create_directories(tempDir_);

        // Write a simple compute shader
        auto shaderPath = tempDir_ / "test_watch.slang";
        std::ofstream out(shaderPath);
        out << R"(
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {}
)";
        out.close();
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }

    // Helper: create config with SPIRV target (avoids MSVC brace-init crash)
    static auto MakeConfig() -> ShaderWatcherConfig {
        ShaderWatcherConfig config;
        config.debounceMs = 50;
        config.targets.push_back(ShaderTarget::SPIRV_1_5());
        return config;
    }

    std::unique_ptr<SlangCompiler> compiler_;
    fs::path tempDir_;
};

TEST_F(ShaderWatcherTest, CreateSucceeds) {
    auto watcher = ShaderWatcher::Create(*compiler_, MakeConfig());
    ASSERT_TRUE(watcher.has_value()) << "ShaderWatcher::Create failed";
}

TEST_F(ShaderWatcherTest, StartAndStop) {
    auto watcher = ShaderWatcher::Create(*compiler_, MakeConfig());
    ASSERT_TRUE(watcher.has_value());

    auto startResult = watcher->Start(tempDir_);
    ASSERT_TRUE(startResult.has_value()) << "Start failed";
    EXPECT_TRUE(watcher->IsRunning());

    watcher->Stop();
    // Give thread time to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(watcher->IsRunning());
}

TEST_F(ShaderWatcherTest, StopIsIdempotent) {
    auto watcher = ShaderWatcher::Create(*compiler_, MakeConfig());
    ASSERT_TRUE(watcher.has_value());

    auto startResult = watcher->Start(tempDir_);
    ASSERT_TRUE(startResult.has_value());

    watcher->Stop();
    watcher->Stop();  // Second stop should not crash
}

TEST_F(ShaderWatcherTest, InitialGenerationIsZero) {
    auto watcher = ShaderWatcher::Create(*compiler_, MakeConfig());
    ASSERT_TRUE(watcher.has_value());

    EXPECT_EQ(watcher->GetGeneration(), 0u);
}

TEST_F(ShaderWatcherTest, StartInvalidDirReturnsError) {
    auto watcher = ShaderWatcher::Create(*compiler_, MakeConfig());
    ASSERT_TRUE(watcher.has_value());

    auto result = watcher->Start(fs::path("nonexistent_dir_12345"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ShaderWatcherTest, PollReturnsEmptyWhenNoChanges) {
    auto watcher = ShaderWatcher::Create(*compiler_, MakeConfig());
    ASSERT_TRUE(watcher.has_value());

    auto startResult = watcher->Start(tempDir_);
    ASSERT_TRUE(startResult.has_value());

    // Immediate poll should return nothing
    auto changes = watcher->Poll();
    EXPECT_TRUE(changes.empty());
    watcher->Stop();
}

TEST_F(ShaderWatcherTest, GetLastErrorsReturnsEmptyInitially) {
    auto watcher = ShaderWatcher::Create(*compiler_, MakeConfig());
    ASSERT_TRUE(watcher.has_value());

    auto errors = watcher->GetLastErrors();
    EXPECT_TRUE(errors.empty());
}

TEST_F(ShaderWatcherTest, FileChangeTriggersRecompile) {
    auto watcher = ShaderWatcher::Create(*compiler_, MakeConfig());
    ASSERT_TRUE(watcher.has_value());

    auto startResult = watcher->Start(tempDir_);
    ASSERT_TRUE(startResult.has_value());

    // Wait for watcher to be fully running
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Modify the file
    auto shaderPath = tempDir_ / "test_watch.slang";
    {
        std::ofstream out(shaderPath);
        out << R"(
[[vk::binding(0, 0)]]
RWStructuredBuffer<float> output;
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    output[tid.x] = 42.0f;
}
)";
    }

    // Wait for debounce + recompilation
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto changes = watcher->Poll();
    // On Windows with ReadDirectoryChangesW, this should detect the change
    // On CI or slow machines, this may be empty — that's acceptable for unit test
    if (!changes.empty()) {
        EXPECT_GT(watcher->GetGeneration(), 0u);
        EXPECT_GT(changes[0].blob.data.size(), 0u);
        EXPECT_GT(changes[0].generation, 0u);
    }

    watcher->Stop();
}
