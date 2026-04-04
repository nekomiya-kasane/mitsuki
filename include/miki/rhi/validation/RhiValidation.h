/** @file RhiValidation.h
 *  @brief Compile-time optional RHI validation layer.
 *
 *  Validates resource creation parameters before forwarding to backend Impl.
 *  Enabled by MIKI_RHI_VALIDATION=1 (default in Debug, off in Release).
 *  When disabled, all validation functions are empty inlines — zero overhead.
 *
 *  Integrates with the adaptation layer (§20b) to determine severity:
 *  - Feature has adaptation strategy -> Warning (will be emulated)
 *  - Feature unsupported and no adaptation -> Error
 *
 *  Namespace: miki::rhi::validation
 *  Spec reference: rendering-pipeline-architecture.md §20a
 */
#pragma once

#include <cstdint>

#include "miki/rhi/Format.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/adaptation/AdaptationQuery.h"

#ifndef MIKI_RHI_VALIDATION
#    ifdef NDEBUG
#        define MIKI_RHI_VALIDATION 0
#    else
#        define MIKI_RHI_VALIDATION 1
#    endif
#endif

namespace miki::rhi::validation {

    // =========================================================================
    // Diagnostic severity
    // =========================================================================

    enum class Severity : uint8_t {
        Info,     ///< Informational (adaptation is transparent)
        Warning,  ///< Performance warning (adaptation with overhead)
        Error,    ///< Invalid configuration (no adaptation possible)
    };

    // =========================================================================
    // Validation diagnostic (runtime, debug only)
    // =========================================================================

    struct Diagnostic {
        Severity severity;
        const char* message;
        adaptation::Feature feature;
        adaptation::Strategy strategy;
    };

    // =========================================================================
    // Validation logging (implemented in RhiValidation.cpp)
    // Only compiled when MIKI_RHI_VALIDATION=1.
    // =========================================================================

    void ReportDiagnostic(BackendType backend, const Diagnostic& diag);

    // =========================================================================
    // Buffer validation
    // =========================================================================

    /// Validate BufferDesc against backend constraints.
    /// Returns true if valid (possibly with warnings). Returns false if invalid.
    inline auto ValidateBufferDesc(BackendType backend, const BufferDesc& desc) -> bool {
#if MIKI_RHI_VALIDATION
        bool valid = true;

        if (desc.size == 0) {
            ReportDiagnostic(
                backend, {.severity = Severity::Error,
                          .message = "Buffer size must be > 0",
                          .feature = adaptation::Feature::BufferMapWriteWithUsage,
                          .strategy = adaptation::Strategy::Unsupported}
            );
            valid = false;
        }

        // Check CpuToGpu + usage flags conflict (WebGPU MapWrite restriction)
        if (desc.memory == MemoryLocation::CpuToGpu) {
            bool hasGpuUsage
                = (static_cast<uint32_t>(desc.usage)
                   & (static_cast<uint32_t>(BufferUsage::Vertex) | static_cast<uint32_t>(BufferUsage::Index)
                      | static_cast<uint32_t>(BufferUsage::Uniform) | static_cast<uint32_t>(BufferUsage::Storage)))
                  != 0;
            if (hasGpuUsage) {
                auto strategy = adaptation::QueryStrategy(backend, adaptation::Feature::BufferMapWriteWithUsage);
                if (strategy == adaptation::Strategy::Unsupported) {
                    ReportDiagnostic(
                        backend,
                        {.severity = Severity::Error,
                         .message
                         = "CpuToGpu buffer with Vertex/Index/Uniform/Storage usage not supported on this backend",
                         .feature = adaptation::Feature::BufferMapWriteWithUsage,
                         .strategy = strategy}
                    );
                    valid = false;
                } else if (strategy != adaptation::Strategy::Native) {
                    ReportDiagnostic(
                        backend, {.severity = Severity::Warning,
                                  .message = "CpuToGpu buffer with GPU usage requires adaptation",
                                  .feature = adaptation::Feature::BufferMapWriteWithUsage,
                                  .strategy = strategy}
                    );
                }
            }
        }

        return valid;
#else
        (void)backend;
        (void)desc;
        return true;
#endif
    }

    // =========================================================================
    // Texture validation
    // =========================================================================

    inline auto ValidateTextureDesc(BackendType backend, const TextureDesc& desc) -> bool {
#if MIKI_RHI_VALIDATION
        bool valid = true;

        if (desc.width == 0 || desc.height == 0) {
            ReportDiagnostic(
                backend, {.severity = Severity::Error,
                          .message = "Texture dimensions must be > 0",
                          .feature = adaptation::Feature::CmdClearTexture,
                          .strategy = adaptation::Strategy::Unsupported}
            );
            valid = false;
        }

        if (desc.mipLevels == 0) {
            ReportDiagnostic(
                backend, {.severity = Severity::Error,
                          .message = "Texture mipLevels must be > 0",
                          .feature = adaptation::Feature::CmdClearTexture,
                          .strategy = adaptation::Strategy::Unsupported}
            );
            valid = false;
        }

        return valid;
#else
        (void)backend;
        (void)desc;
        return true;
#endif
    }

    // =========================================================================
    // Depth-only format stencil validation
    // =========================================================================

    /// Check if a format is depth-only (no stencil component).
    /// Used by command buffers to apply ParameterFixup adaptation.
    constexpr auto IsDepthOnlyFormat(Format fmt) -> bool {
        auto info = FormatInfo(fmt);
        return info.isDepth && !info.isStencil;
    }

    // =========================================================================
    // Adaptation-aware feature check (for use in backend Impl code)
    // =========================================================================

    /// Check feature availability with diagnostic reporting.
    /// Returns the strategy to use. If Unsupported, also reports an error.
    inline auto CheckFeature(BackendType backend, adaptation::Feature feature) -> adaptation::Strategy {
#if MIKI_RHI_VALIDATION
        auto strategy = adaptation::QueryStrategy(backend, feature);
        if (strategy == adaptation::Strategy::Unsupported) {
            ReportDiagnostic(
                backend, {.severity = Severity::Error,
                          .message = "Feature not supported on this backend",
                          .feature = feature,
                          .strategy = strategy}
            );
        } else if (adaptation::HasCpuOverhead(strategy) || adaptation::HasGpuOverhead(strategy)) {
            ReportDiagnostic(
                backend, {.severity = Severity::Info,
                          .message = "Feature adapted with overhead",
                          .feature = feature,
                          .strategy = strategy}
            );
        }
        return strategy;
#else
        return adaptation::QueryStrategy(backend, feature);
#endif
    }

}  // namespace miki::rhi::validation
