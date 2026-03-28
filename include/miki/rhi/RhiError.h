/** @file RhiError.h
 *  @brief RHI error codes and Result type alias.
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <expected>

namespace miki::rhi {

    enum class RhiError : uint32_t {
        OutOfDeviceMemory,
        OutOfHostMemory,
        DeviceLost,
        SurfaceLost,
        SwapchainOutOfDate,
        FormatNotSupported,
        FeatureNotSupported,
        InvalidHandle,
        InvalidParameter,
        ShaderCompilationFailed,
        PipelineCreationFailed,
        TooManyObjects,
        NotImplemented,
    };

    template <typename T>
    using RhiResult = std::expected<T, RhiError>;

}  // namespace miki::rhi
