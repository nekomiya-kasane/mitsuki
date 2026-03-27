// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <miki/core/Result.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace miki::test {

    /// @brief Image comparison result.
    struct ImageCompareResult {
        double psnr;     // Peak Signal-to-Noise Ratio (dB). Higher = more similar. >40dB typically indistinguishable.
        double rmse;     // Root Mean Square Error. Lower = more similar.
        double maxDiff;  // Maximum per-channel difference [0, 1].
        uint32_t diffPixels;   // Number of pixels with any difference.
        uint32_t totalPixels;  // Total pixel count.
        bool passed;           // True if within threshold.
    };

    /// @brief Image data for comparison.
    struct ImageData {
        const uint8_t* pixels;  // RGBA8 pixel data (4 bytes per pixel)
        uint32_t width;
        uint32_t height;
        uint32_t stride;  // Bytes per row (0 = width * 4)

        [[nodiscard]] auto GetStride() const -> uint32_t { return stride != 0 ? stride : width * 4; }
        [[nodiscard]] auto GetPixelCount() const -> uint32_t { return width * height; }
    };

    /// @brief Visual regression test configuration.
    struct VisualRegressionConfig {
        double psnrThreshold = 40.0;    // Minimum PSNR (dB) to pass. 40dB = visually identical.
        double rmseThreshold = 0.01;    // Maximum RMSE to pass.
        double maxDiffThreshold = 0.1;  // Maximum per-channel difference to pass.
        bool generateDiffImage = true;  // Generate difference visualization.
        bool failOnMissing = true;      // Fail if golden image doesn't exist.
    };

    /// @brief Calculate PSNR between two images.
    /// @param actual Rendered image.
    /// @param expected Golden/reference image.
    /// @return PSNR in dB. Returns infinity if images are identical.
    [[nodiscard]] inline auto CalculatePsnr(const ImageData& actual, const ImageData& expected) -> double {
        if (actual.width != expected.width || actual.height != expected.height) {
            return 0.0;  // Dimension mismatch
        }

        double mse = 0.0;
        const uint32_t pixelCount = actual.GetPixelCount();

        for (uint32_t y = 0; y < actual.height; ++y) {
            const auto* rowA = actual.pixels + y * actual.GetStride();
            const auto* rowE = expected.pixels + y * expected.GetStride();

            for (uint32_t x = 0; x < actual.width; ++x) {
                for (uint32_t c = 0; c < 4; ++c) {  // RGBA
                    double diff = static_cast<double>(rowA[x * 4 + c]) - static_cast<double>(rowE[x * 4 + c]);
                    mse += diff * diff;
                }
            }
        }

        mse /= static_cast<double>(pixelCount * 4);

        if (mse < 1e-10) {
            return std::numeric_limits<double>::infinity();  // Identical images
        }

        // PSNR = 20 * log10(MAX) - 10 * log10(MSE)
        // For 8-bit images, MAX = 255
        constexpr double kMaxValue = 255.0;
        return 20.0 * std::log10(kMaxValue) - 10.0 * std::log10(mse);
    }

    /// @brief Calculate RMSE between two images (normalized to [0, 1]).
    /// @param actual Rendered image.
    /// @param expected Golden/reference image.
    /// @return RMSE in range [0, 1]. 0 = identical.
    [[nodiscard]] inline auto CalculateRmse(const ImageData& actual, const ImageData& expected) -> double {
        if (actual.width != expected.width || actual.height != expected.height) {
            return 1.0;  // Dimension mismatch = maximum error
        }

        double sumSqDiff = 0.0;
        const uint32_t pixelCount = actual.GetPixelCount();

        for (uint32_t y = 0; y < actual.height; ++y) {
            const auto* rowA = actual.pixels + y * actual.GetStride();
            const auto* rowE = expected.pixels + y * expected.GetStride();

            for (uint32_t x = 0; x < actual.width; ++x) {
                for (uint32_t c = 0; c < 4; ++c) {
                    double diff = (static_cast<double>(rowA[x * 4 + c]) - static_cast<double>(rowE[x * 4 + c])) / 255.0;
                    sumSqDiff += diff * diff;
                }
            }
        }

        return std::sqrt(sumSqDiff / static_cast<double>(pixelCount * 4));
    }

    /// @brief Compare two images and return detailed metrics.
    /// @param actual Rendered image.
    /// @param expected Golden/reference image.
    /// @param config Comparison configuration.
    /// @return Comparison result with metrics and pass/fail status.
    [[nodiscard]] inline auto CompareImages(
        const ImageData& actual, const ImageData& expected, const VisualRegressionConfig& config = {}
    ) -> ImageCompareResult {
        ImageCompareResult result{};

        if (actual.width != expected.width || actual.height != expected.height) {
            result.passed = false;
            result.psnr = 0.0;
            result.rmse = 1.0;
            result.maxDiff = 1.0;
            result.diffPixels = actual.GetPixelCount();
            result.totalPixels = actual.GetPixelCount();
            return result;
        }

        result.totalPixels = actual.GetPixelCount();
        result.psnr = CalculatePsnr(actual, expected);
        result.rmse = CalculateRmse(actual, expected);

        // Calculate max diff and count different pixels
        result.maxDiff = 0.0;
        result.diffPixels = 0;

        for (uint32_t y = 0; y < actual.height; ++y) {
            const auto* rowA = actual.pixels + y * actual.GetStride();
            const auto* rowE = expected.pixels + y * expected.GetStride();

            for (uint32_t x = 0; x < actual.width; ++x) {
                bool pixelDiffers = false;
                for (uint32_t c = 0; c < 4; ++c) {
                    double diff
                        = std::abs(static_cast<double>(rowA[x * 4 + c]) - static_cast<double>(rowE[x * 4 + c])) / 255.0;
                    result.maxDiff = std::max(result.maxDiff, diff);
                    if (diff > 0.0) {
                        pixelDiffers = true;
                    }
                }
                if (pixelDiffers) {
                    ++result.diffPixels;
                }
            }
        }

        // Determine pass/fail
        result.passed = (result.psnr >= config.psnrThreshold || std::isinf(result.psnr))
                        && result.rmse <= config.rmseThreshold && result.maxDiff <= config.maxDiffThreshold;

        return result;
    }

    /// @brief Generate a difference visualization image.
    /// @param actual Rendered image.
    /// @param expected Golden/reference image.
    /// @param output Output buffer (must be same size as input, RGBA8).
    /// @param amplify Amplification factor for differences (default 10x).
    inline void GenerateDiffImage(
        const ImageData& actual, const ImageData& expected, uint8_t* output, float amplify = 10.0f
    ) {
        if (actual.width != expected.width || actual.height != expected.height) {
            return;
        }

        for (uint32_t y = 0; y < actual.height; ++y) {
            const auto* rowA = actual.pixels + y * actual.GetStride();
            const auto* rowE = expected.pixels + y * expected.GetStride();
            auto* rowO = output + y * actual.width * 4;

            for (uint32_t x = 0; x < actual.width; ++x) {
                for (uint32_t c = 0; c < 3; ++c) {  // RGB only
                    int diff = static_cast<int>(rowA[x * 4 + c]) - static_cast<int>(rowE[x * 4 + c]);
                    int amplified = static_cast<int>(std::abs(diff) * amplify);
                    rowO[x * 4 + c] = static_cast<uint8_t>(std::min(amplified, 255));
                }
                rowO[x * 4 + 3] = 255;  // Alpha = opaque
            }
        }
    }

    /// @brief Visual regression test runner.
    class VisualRegressionTest {
       public:
        explicit VisualRegressionTest(std::filesystem::path goldenDir) : goldenDir_(std::move(goldenDir)) {}

        /// @brief Set configuration for all tests.
        void SetConfig(const VisualRegressionConfig& config) { config_ = config; }

        /// @brief Compare rendered image against golden image.
        /// @param testName Test name (used for golden image filename).
        /// @param actual Rendered image data.
        /// @return Comparison result.
        [[nodiscard]] auto Compare(std::string_view testName, const ImageData& actual) const -> ImageCompareResult;

        /// @brief Update golden image with new reference.
        /// @param testName Test name.
        /// @param image New golden image data.
        /// @return Ok on success.
        auto UpdateGolden(std::string_view testName, const ImageData& image) const -> core::Result<void>;

        /// @brief Get path to golden image for a test.
        [[nodiscard]] auto GetGoldenPath(std::string_view testName) const -> std::filesystem::path {
            return goldenDir_ / (std::string(testName) + ".png");
        }

        /// @brief Get path to diff image for a test.
        [[nodiscard]] auto GetDiffPath(std::string_view testName) const -> std::filesystem::path {
            return goldenDir_ / (std::string(testName) + "_diff.png");
        }

       private:
        std::filesystem::path goldenDir_;
        VisualRegressionConfig config_;
    };

}  // namespace miki::test
