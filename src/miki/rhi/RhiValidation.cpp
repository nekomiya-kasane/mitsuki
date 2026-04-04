/** @file RhiValidation.cpp
 *  @brief RHI validation layer diagnostic reporting implementation.
 *
 *  Only compiled when MIKI_RHI_VALIDATION=1.
 *  Uses MIKI_LOG_WARN / MIKI_LOG_ERROR for diagnostic output.
 *
 *  Namespace: miki::rhi::validation
 */

#include "miki/rhi/validation/RhiValidation.h"

#if MIKI_RHI_VALIDATION

#    include "miki/debug/StructuredLogger.h"

namespace miki::rhi::validation {

    namespace {

        constexpr auto StrategyName(adaptation::Strategy s) -> const char* {
            switch (s) {
                case adaptation::Strategy::Native: return "Native";
                case adaptation::Strategy::ParameterFixup: return "ParameterFixup";
                case adaptation::Strategy::UboEmulation: return "UboEmulation";
                case adaptation::Strategy::EphemeralMap: return "EphemeralMap";
                case adaptation::Strategy::CallbackEmulation: return "CallbackEmulation";
                case adaptation::Strategy::ShadowBuffer: return "ShadowBuffer";
                case adaptation::Strategy::StagingCopy: return "StagingCopy";
                case adaptation::Strategy::LoopUnroll: return "LoopUnroll";
                case adaptation::Strategy::ShaderEmulation: return "ShaderEmulation";
                case adaptation::Strategy::Unsupported: return "Unsupported";
            }
            return "Unknown";
        }

        constexpr auto BackendName(BackendType b) -> const char* {
            switch (b) {
                case BackendType::Vulkan14: return "Vulkan14";
                case BackendType::D3D12: return "D3D12";
                case BackendType::VulkanCompat: return "VulkanCompat";
                case BackendType::WebGPU: return "WebGPU";
                case BackendType::OpenGL43: return "OpenGL43";
                case BackendType::Mock: return "Mock";
            }
            return "Unknown";
        }

    }  // anonymous namespace

    void ReportDiagnostic(BackendType backend, const Diagnostic& diag) {
        switch (diag.severity) {
            case Severity::Error:
                MIKI_LOG_ERROR(
                    ::miki::debug::LogCategory::Rhi, "[RHI Validation] {} | {} | strategy={}", BackendName(backend),
                    diag.message, StrategyName(diag.strategy)
                );
                break;
            case Severity::Warning:
                MIKI_LOG_WARN(
                    ::miki::debug::LogCategory::Rhi, "[RHI Validation] {} | {} | strategy={}", BackendName(backend),
                    diag.message, StrategyName(diag.strategy)
                );
                break;
            case Severity::Info:
                MIKI_LOG_INFO(
                    ::miki::debug::LogCategory::Rhi, "[RHI Validation] {} | {} | strategy={}", BackendName(backend),
                    diag.message, StrategyName(diag.strategy)
                );
                break;
        }
        MIKI_LOG_FLUSH();
    }

}  // namespace miki::rhi::validation

#endif  // MIKI_RHI_VALIDATION
