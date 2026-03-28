/** @brief Universal error type for the miki renderer.
 *
 * All error codes are non-zero. Module ranges are defined in .windsurfrules 5.6.
 * ErrorCode is the root error type used by Result<T> throughout the project.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace miki::core {

    /** @brief Universal error code enum.
     *
     * Ranges per module (from .windsurfrules 5.6):
     * - Common:          0x0001 - 0x00FF
     * - Core Rendering:  0x1000 - 0x1FFF
     * - Mesh Shader:     0x2000 - 0x2FFF
     * - GPU HLR:         0x3000 - 0x3FFF
     * - Ray Tracing Pick:0x4000 - 0x4FFF
     * - Resource Mgmt:   0x5000 - 0x5FFF
     * - Coca Coroutine:  0x6000 - 0x6FFF
     * - GPU Device:      0xF000 - 0xFFFF
     */
    enum class ErrorCode : uint32_t {
        // --- Success ---
        Ok = 0,

        // --- Common (0x0001 - 0x00FF) ---
        InvalidArgument = 0x0001,
        OutOfMemory = 0x0002,
        NotSupported = 0x0003,
        NotImplemented = 0x0004,
        InvalidState = 0x0005,
        IoError = 0x0006,
        Timeout = 0x0007,
        ResourceExhausted = 0x0008,
        PreconditionViolated = 0x0009,

        // --- Core Rendering (0x1000 - 0x1FFF) ---
        PipelineCreationFailed = 0x1000,
        ShaderCompilationFailed = 0x1001,
        RenderPassInvalid = 0x1002,
        GraphCycleDetected = 0x1003,
        UnresolvedResource = 0x1004,

        // --- Mesh Shader (0x2000 - 0x2FFF) ---
        MeshletOverflow = 0x2000,

        // --- GPU HLR (0x3000 - 0x3FFF) ---
        HlrBufferOverflow = 0x3000,

        // --- Ray Tracing Pick (0x4000 - 0x4FFF) ---
        RayMiss = 0x4000,

        // --- Resource Mgmt (0x5000 - 0x5FFF) ---
        ResourceNotFound = 0x5000,
        ResourceCorrupted = 0x5001,

        // --- Coca Coroutine (0x6000 - 0x6FFF) ---
        TaskCancelled = 0x6000,

        // --- Kernel (0x7000 - 0x7FFF) ---
        ImportFailed = 0x7000,
        TessellationFailed = 0x7001,
        ExportFailed = 0x7002,

        // --- Debug Infrastructure (0xD000 - 0xDFFF) ---
        LogSinkFull = 0xD000,
        LogFileOpenFailed = 0xD001,
        CrashHandlerInstallFailed = 0xD002,
        EmergencyPathInvalid = 0xD003,
        BreadcrumbBufferFull = 0xD004,
        ShaderPrintfOverflow = 0xD005,
        CaptureToolNotAvailable = 0xD006,
        ProfilerNotInitialized = 0xD007,
        TraceExportFailed = 0xD008,

        // --- GPU Device (0xF000 - 0xFFFF) ---
        DeviceLost = 0xF000,
        DeviceNotReady = 0xF001,
        SwapchainOutOfDate = 0xF002,
        SurfaceLost = 0xF003,
    };

    /** @brief Convert an ErrorCode to a human-readable string.
     *  @param iCode The error code to convert.
     *  @return A string_view naming the error code.
     */
    [[nodiscard]] constexpr auto ToString(ErrorCode iCode) -> std::string_view {
        switch (iCode) {
            case ErrorCode::Ok: return "Ok";
            case ErrorCode::InvalidArgument: return "InvalidArgument";
            case ErrorCode::OutOfMemory: return "OutOfMemory";
            case ErrorCode::NotSupported: return "NotSupported";
            case ErrorCode::NotImplemented: return "NotImplemented";
            case ErrorCode::InvalidState: return "InvalidState";
            case ErrorCode::IoError: return "IoError";
            case ErrorCode::Timeout: return "Timeout";
            case ErrorCode::ResourceExhausted: return "ResourceExhausted";
            case ErrorCode::PreconditionViolated: return "PreconditionViolated";
            case ErrorCode::PipelineCreationFailed: return "PipelineCreationFailed";
            case ErrorCode::ShaderCompilationFailed: return "ShaderCompilationFailed";
            case ErrorCode::RenderPassInvalid: return "RenderPassInvalid";
            case ErrorCode::GraphCycleDetected: return "GraphCycleDetected";
            case ErrorCode::UnresolvedResource: return "UnresolvedResource";
            case ErrorCode::MeshletOverflow: return "MeshletOverflow";
            case ErrorCode::HlrBufferOverflow: return "HlrBufferOverflow";
            case ErrorCode::RayMiss: return "RayMiss";
            case ErrorCode::ResourceNotFound: return "ResourceNotFound";
            case ErrorCode::ResourceCorrupted: return "ResourceCorrupted";
            case ErrorCode::TaskCancelled: return "TaskCancelled";
            case ErrorCode::ImportFailed: return "ImportFailed";
            case ErrorCode::TessellationFailed: return "TessellationFailed";
            case ErrorCode::ExportFailed: return "ExportFailed";
            case ErrorCode::LogSinkFull: return "LogSinkFull";
            case ErrorCode::LogFileOpenFailed: return "LogFileOpenFailed";
            case ErrorCode::CrashHandlerInstallFailed: return "CrashHandlerInstallFailed";
            case ErrorCode::EmergencyPathInvalid: return "EmergencyPathInvalid";
            case ErrorCode::BreadcrumbBufferFull: return "BreadcrumbBufferFull";
            case ErrorCode::ShaderPrintfOverflow: return "ShaderPrintfOverflow";
            case ErrorCode::CaptureToolNotAvailable: return "CaptureToolNotAvailable";
            case ErrorCode::ProfilerNotInitialized: return "ProfilerNotInitialized";
            case ErrorCode::TraceExportFailed: return "TraceExportFailed";
            case ErrorCode::DeviceLost: return "DeviceLost";
            case ErrorCode::DeviceNotReady: return "DeviceNotReady";
            case ErrorCode::SwapchainOutOfDate: return "SwapchainOutOfDate";
            case ErrorCode::SurfaceLost: return "SurfaceLost";
        }
        return "Unknown";
    }

}  // namespace miki::core
