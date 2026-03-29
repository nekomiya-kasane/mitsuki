// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §12 Query Pool tests.
// Covers: QueryPool creation/destruction for Timestamp, Occlusion,
// PipelineStatistics types, GetTimestampPeriod, descriptor validation.

#include <gtest/gtest.h>

#include "RhiTestFixture.h"

#include "miki/rhi/Device.h"
#include "miki/rhi/Query.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// §12.0 QueryPoolDesc — struct validation (pure CPU)
// ============================================================================

TEST(QueryPoolDesc, DefaultsValid) {
    QueryPoolDesc desc{};
    EXPECT_EQ(desc.type, QueryType::Timestamp);
    EXPECT_EQ(desc.count, 0u);
}

// ============================================================================
// §12.1 QueryPool Creation
// ============================================================================

class RhiQueryPoolTest : public RhiTest {};

TEST_P(RhiQueryPoolTest, CreateTimestampQueryPool) {
    QueryPoolDesc desc{.type = QueryType::Timestamp, .count = 64};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateQueryPool(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyQueryPool(*result); });
}

TEST_P(RhiQueryPoolTest, CreateOcclusionQueryPool) {
    QueryPoolDesc desc{.type = QueryType::Occlusion, .count = 16};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateQueryPool(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyQueryPool(*result); });
}

TEST_P(RhiQueryPoolTest, CreatePipelineStatisticsQueryPool) {
    RequireFeature(DeviceFeature::PipelineStatistics);

    QueryPoolDesc desc{.type = QueryType::PipelineStatistics, .count = 8};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateQueryPool(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyQueryPool(*result); });
}

TEST_P(RhiQueryPoolTest, CreateZeroCountQueryPoolFails) {
    QueryPoolDesc desc{.type = QueryType::Timestamp, .count = 0};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateQueryPool(desc); });
    EXPECT_FALSE(result.has_value());
}

TEST_P(RhiQueryPoolTest, DestroyInvalidQueryPoolSilent) {
    QueryPoolHandle invalid{};
    Dev().Dispatch([&](auto& dev) { dev.DestroyQueryPool(invalid); });
}

// ============================================================================
// §12.2 Timestamp Period
// ============================================================================

TEST_P(RhiQueryPoolTest, GetTimestampPeriodNonZero) {
    auto period = Dev().Dispatch([&](auto& dev) { return dev.GetTimestampPeriod(); });
    EXPECT_GT(period, 0.0) << "Timestamp period must be > 0 nanoseconds per tick";
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiQueryPoolTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
