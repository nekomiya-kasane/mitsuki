/** @brief Interactive demo for miki::debug::StructuredLogger.
 *
 * Demonstrates:
 *   - Multi-sink setup (console + file + JSON + callback)
 *   - Multi-threaded logging (4 worker threads)
 *   - Per-category level filtering
 *   - ANSI-colored console output
 *   - Flush guarantee
 *   - Dropped message counting
 */

#include "miki/debug/StructuredLogger.h"

#include <cstdio>
#include <iostream>
#include <string>

#ifndef __EMSCRIPTEN__
#    include <filesystem>
#    include <thread>
#    include <vector>
#endif

using namespace miki::debug;

#ifndef __EMSCRIPTEN__
namespace {

    auto RunWorkerThreads(int threadCount, int msgsPerThread) -> void {
        std::vector<std::thread> threads;
        threads.reserve(threadCount);

        for (int t = 0; t < threadCount; ++t) {
            threads.emplace_back([t, msgsPerThread] {
                for (int i = 0; i < msgsPerThread; ++i) {
                    auto cat = static_cast<LogCategory>(t % static_cast<int>(LogCategory::Count_));

                    if (i % 5 == 0) {
                        MIKI_LOG_TRACE(cat, "[T{}] trace msg #{}", t, i);
                    } else if (i % 5 == 1) {
                        MIKI_LOG_DEBUG(cat, "[T{}] debug msg #{}", t, i);
                    } else if (i % 5 == 2) {
                        MIKI_LOG_INFO(cat, "[T{}] info msg #{}", t, i);
                    } else if (i % 5 == 3) {
                        MIKI_LOG_WARN(cat, "[T{}] warn msg #{}", t, i);
                    } else {
                        MIKI_LOG_ERROR(cat, "[T{}] error msg #{}", t, i);
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }

}  // anonymous namespace
#endif  // !__EMSCRIPTEN__

auto main() -> int {
    auto& logger = StructuredLogger::Instance();

    // ---- Setup sinks ----
    std::printf("=== miki StructuredLogger Demo ===\n\n");

    // 1) Console sink (colored stderr)
    logger.AddSink(ConsoleSink{});

#ifndef __EMSCRIPTEN__
    // 2) File sink (not available in WASM)
    auto logDir = std::filesystem::temp_directory_path();
#endif

    auto logPath = logDir / "miki_demo.log";
    logger.AddSink(FileSink(logPath));

    // 3) JSON sink (not available in WASM)
    auto jsonPath = logDir / "miki_demo.ndjson";
    logger.AddSink(JsonSink(jsonPath));

    // 4) Callback sink (counts messages)
    int callbackCount = 0;
    logger.AddSink(CallbackSink([&](const LogEntry&) { ++callbackCount; }));

    // ---- Start drain thread (not available in WASM — Flush() works synchronously) ----
    logger.StartDrainThread();

    // ---- Demo 1: Basic logging at all levels ----
    std::printf("--- Demo 1: All log levels ---\n\n");

    MIKI_LOG_TRACE(LogCategory::Demo, "Trace: verbose detail for deep debugging");
    MIKI_LOG_DEBUG(LogCategory::Demo, "Debug: RenderGraph compiled with {} passes", 42);
    MIKI_LOG_INFO(LogCategory::Rhi, "Info: Device created: NVIDIA RTX 4070 (Vk 1.3)");
    MIKI_LOG_WARN(LogCategory::Resource, "Warn: VRAM budget {}% -- approaching limit", 85);
    MIKI_LOG_ERROR(LogCategory::Shader, "Error: Compilation failed: pbr_material.slang:42");
    MIKI_LOG_FATAL(LogCategory::Rhi, "Fatal: VK_ERROR_DEVICE_LOST -- breadcrumbs dumped");

    logger.Flush();
    std::printf("\n");

    // ---- Demo 2: Category filtering ----
    std::printf("--- Demo 2: Category filtering ---\n");
    std::printf("  Setting Render category to Warn level.\n");
    std::printf("  Info messages from Render will be filtered.\n\n");

    logger.SetCategoryLevel(LogCategory::Render, LogLevel::Warn);

    MIKI_LOG_INFO(LogCategory::Render, "This INFO should be FILTERED (invisible)");
    MIKI_LOG_WARN(LogCategory::Render, "This WARN should be VISIBLE");
    MIKI_LOG_INFO(LogCategory::Core, "Core INFO is still visible (unfiltered)");

    logger.Flush();
    std::printf("\n");

    // Reset for next demo
    logger.SetCategoryLevel(LogCategory::Render, LogLevel::Trace);

#ifndef __EMSCRIPTEN__
    // ---- Demo 3: Multi-threaded logging ----
    std::printf("--- Demo 3: 4 threads x 50 messages ---\n\n");

    RunWorkerThreads(4, 50);
    logger.Flush();
    std::printf("\n");

    // ---- Demo 4: Stress test with drop counting ----
    std::printf("--- Demo 4: Ring buffer stress test ---\n");
    std::printf("  Sending 5000 messages rapidly...\n\n");

    logger.ResetDroppedCount();

    for (int i = 0; i < 5000; ++i) {
        MIKI_LOG_DEBUG(LogCategory::Core, "stress #{}: padding data here", i);
    }

    logger.Flush();

    auto dropped = logger.GetDroppedCount();
    std::printf("\n  Callback received: %d messages\n", callbackCount);
    std::printf("  Dropped (ring overflow): %llu messages\n", static_cast<unsigned long long>(dropped));
#else
    // WASM: single-threaded demo, no file sinks
    std::printf("--- Demo 3: Skipped (no threads in WASM) ---\n\n");
    std::printf("  Callback received: %d messages\n", callbackCount);
#endif

    // ---- Summary ----
    std::printf("\n=== Demo Complete ===\n");
#ifndef __EMSCRIPTEN__
    std::printf("  Log file: %s\n", logPath.string().c_str());
    std::printf("  JSON file: %s\n", jsonPath.string().c_str());
#endif

    logger.Shutdown();
    return 0;
}
