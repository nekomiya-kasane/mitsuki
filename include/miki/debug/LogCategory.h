/** @brief Log level and category enums for the miki structured logger.
 *
 * Separated into its own header to break circular dependencies — any header that needs log levels/categories includes
 * only this, not the full StructuredLogger.h.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace miki::debug {

    /// @brief Severity level for log messages.
    /// Ordered from least to most severe. Off disables all output.
    enum class LogLevel : uint8_t {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Fatal,
        Off
    };

    /// @brief Per-module log category.
    /// Each subsystem gets a unique category for independent filtering.
    enum class LogCategory : uint16_t {
        Core = 0,
        Rhi = 1,
        Render = 2,
        Resource = 3,
        Scene = 4,
        Shader = 5,
        Vgeo = 6,
        Hlr = 7,
        Kernel = 8,
        Platform = 9,
        Debug = 10,
        Test = 11,
        Demo = 12,
        Count_ = 13,  // Internal — number of categories
    };

    /// @brief Convert LogLevel to short display string.
    [[nodiscard]] constexpr auto ToString(LogLevel level) -> std::string_view {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info: return "INFO ";
            case LogLevel::Warn: return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            case LogLevel::Off: return "OFF  ";
        }
        return "?????";
    }

    /// @brief Convert LogCategory to short display string.
    [[nodiscard]] constexpr auto ToString(LogCategory cat) -> std::string_view {
        switch (cat) {
            case LogCategory::Core: return "Core    ";
            case LogCategory::Rhi: return "Rhi     ";
            case LogCategory::Render: return "Render  ";
            case LogCategory::Resource: return "Resource";
            case LogCategory::Scene: return "Scene   ";
            case LogCategory::Shader: return "Shader  ";
            case LogCategory::Vgeo: return "Vgeo    ";
            case LogCategory::Hlr: return "Hlr     ";
            case LogCategory::Kernel: return "Kernel  ";
            case LogCategory::Platform: return "Platform";
            case LogCategory::Debug: return "Debug   ";
            case LogCategory::Test: return "Test    ";
            case LogCategory::Demo: return "Demo    ";
            case LogCategory::Count_: return "Count_  ";
        }
        return "????????";
    }

}  // namespace miki::debug
