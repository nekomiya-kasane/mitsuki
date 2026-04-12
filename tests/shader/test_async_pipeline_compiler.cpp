// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 2 tests: Async pipeline compilation (§16.1).
// Tests that PSO compilations complete asynchronously without blocking main thread.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "miki/shader/AsyncPipelineCompiler.h"
#include "miki/shader/SlangCompiler.h"

using namespace miki::shader;

class AsyncPipelineCompilerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "SlangCompiler::Create failed";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));
        compiler_->AddSearchPath(MIKI_SHADER_DIR);
        compiler_->AddSearchPath(MIKI_SHADER_TESTS_DIR);
    }

    void TearDown() override {
        if (asyncCompiler_) {
            asyncCompiler_->Shutdown();
        }
    }

    std::unique_ptr<SlangCompiler> compiler_;
    std::unique_ptr<AsyncPipelineCompiler> asyncCompiler_;
};

TEST_F(AsyncPipelineCompilerTest, CreateAndDestroy) {
    auto result = AsyncPipelineCompiler::Create(*compiler_, 2);
    ASSERT_TRUE(result.has_value()) << "AsyncPipelineCompiler::Create failed";
    asyncCompiler_ = std::make_unique<AsyncPipelineCompiler>(std::move(*result));
    EXPECT_EQ(asyncCompiler_->PendingCount(), 0u);
}

TEST_F(AsyncPipelineCompilerTest, SubmitAndComplete) {
    auto result = AsyncPipelineCompiler::Create(*compiler_, 2);
    ASSERT_TRUE(result.has_value());
    asyncCompiler_ = std::make_unique<AsyncPipelineCompiler>(std::move(*result));

    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    AsyncPSORequest req;
    req.desc.sourcePath = shaderPath;
    req.desc.entryPoint = "main";
    req.desc.stage = ShaderStage::Compute;
    req.desc.target = ShaderTarget::SPIRV_1_5();
    req.userData = 42;

    auto handle = asyncCompiler_->Submit(req);
    EXPECT_TRUE(handle.IsValid());

    // Wait for completion (bounded)
    asyncCompiler_->WaitAll();
    EXPECT_TRUE(asyncCompiler_->IsComplete(handle));

    auto completed = asyncCompiler_->DrainCompleted();
    ASSERT_EQ(completed.size(), 1u);
    EXPECT_TRUE(completed[0].success);
    EXPECT_EQ(completed[0].userData, 42u);
    EXPECT_GT(completed[0].blob.data.size(), 0u);
}

TEST_F(AsyncPipelineCompilerTest, BatchSubmission) {
    auto result = AsyncPipelineCompiler::Create(*compiler_, 2);
    ASSERT_TRUE(result.has_value());
    asyncCompiler_ = std::make_unique<AsyncPipelineCompiler>(std::move(*result));

    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";

    std::vector<AsyncPSORequest> requests;
    for (uint32_t i = 0; i < 4; ++i) {
        AsyncPSORequest req;
        req.desc.sourcePath = shaderPath;
        req.desc.entryPoint = "main";
        req.desc.stage = ShaderStage::Compute;
        req.desc.target = ShaderTarget::SPIRV_1_5();
        req.userData = i;
        requests.push_back(std::move(req));
    }

    auto handles = asyncCompiler_->SubmitBatch(requests);
    EXPECT_EQ(handles.size(), 4u);

    asyncCompiler_->WaitAll();

    auto completed = asyncCompiler_->DrainCompleted();
    EXPECT_EQ(completed.size(), 4u);
    for (auto const& c : completed) {
        EXPECT_TRUE(c.success) << "Batch compilation " << c.userData << " failed";
    }
}

TEST_F(AsyncPipelineCompilerTest, NonBlockingSubmit) {
    auto result = AsyncPipelineCompiler::Create(*compiler_, 1);
    ASSERT_TRUE(result.has_value());
    asyncCompiler_ = std::make_unique<AsyncPipelineCompiler>(std::move(*result));

    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    AsyncPSORequest req;
    req.desc.sourcePath = shaderPath;
    req.desc.entryPoint = "main";
    req.desc.stage = ShaderStage::Compute;
    req.desc.target = ShaderTarget::SPIRV_1_5();

    auto t0 = std::chrono::steady_clock::now();
    auto handle = asyncCompiler_->Submit(req);
    auto t1 = std::chrono::steady_clock::now();
    auto submitMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Submit should be near-instant (< 5ms)
    EXPECT_LT(submitMs, 5.0) << "Submit took " << submitMs << "ms, should be non-blocking";
    EXPECT_TRUE(handle.IsValid());

    asyncCompiler_->WaitAll();
    EXPECT_TRUE(asyncCompiler_->IsComplete(handle));
}

TEST_F(AsyncPipelineCompilerTest, PendingCountTracking) {
    auto result = AsyncPipelineCompiler::Create(*compiler_, 1);
    ASSERT_TRUE(result.has_value());
    asyncCompiler_ = std::make_unique<AsyncPipelineCompiler>(std::move(*result));

    EXPECT_EQ(asyncCompiler_->PendingCount(), 0u);

    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    AsyncPSORequest req;
    req.desc.sourcePath = shaderPath;
    req.desc.entryPoint = "main";
    req.desc.stage = ShaderStage::Compute;
    req.desc.target = ShaderTarget::SPIRV_1_5();

    [[maybe_unused]] auto handle = asyncCompiler_->Submit(req);
    // Pending count should be >= 0 (may already be completed by the time we check)
    // But we know at least 1 was submitted
    asyncCompiler_->WaitAll();
    EXPECT_EQ(asyncCompiler_->PendingCount(), 0u);
}
