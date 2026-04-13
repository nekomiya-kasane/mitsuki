/** @file SyncScheduler.cpp
 *  @brief Cross-queue timeline semaphore dependency resolution.
 *  See: SyncScheduler.h, specs/03-sync.md §13
 */

#include "miki/frame/SyncScheduler.h"

#include <cassert>
#include <format>

#include "miki/debug/StructuredLogger.h"

#include "miki/core/EnumStrings.h"

namespace miki::frame {

    void SyncScheduler::Init(const rhi::QueueTimelines& timelines) {
        queues_[0].semaphore = timelines.graphics;
        queues_[1].semaphore = timelines.compute;
        queues_[2].semaphore = timelines.transfer;
        // Level A: asyncCompute has its own semaphore → independent slot 3
        // Level B/C/D: asyncCompute aliases compute → slot 1 (shared counter)
        if (timelines.asyncCompute.value != timelines.compute.value) {
            asyncComputeSlot_ = 3;
            queues_[3].semaphore = timelines.asyncCompute;
        } else {
            asyncComputeSlot_ = 1;  // QueueIndex(AsyncCompute) returns 1, sharing Compute's state
        }
        for (auto& q : queues_) {
            q.nextValue = 1;
            q.currentValue = 0;
            q.pendingWaits.clear();
        }
        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync] SyncScheduler::Init: graphics=[0x{:x}] compute=[0x{:x}] transfer=[0x{:x}] asyncSlot=[{}]",
            timelines.graphics.value, timelines.compute.value, timelines.transfer.value, asyncComputeSlot_
        );
    }

    auto SyncScheduler::AllocateSignal(rhi::QueueType queue) -> uint64_t {
        auto& state = queues_[QueueIndex(queue)];
        uint64_t allocated = state.nextValue++;
        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync] SyncScheduler::AllocateSignal: queue={} allocated=[{}] nextValue=[{}] sem=[0x{:x}]",
            rhi::ToString(queue), allocated, state.nextValue, state.semaphore.value
        );
        return allocated;
    }

    void SyncScheduler::AddDependency(
        rhi::QueueType waitQueue, rhi::QueueType signalQueue, uint64_t signalValue, rhi::PipelineStage waitStage
    ) {
        auto& waiter = queues_[QueueIndex(waitQueue)];
        auto signalSem = queues_[QueueIndex(signalQueue)].semaphore;
        waiter.pendingWaits.push_back({.semaphore = signalSem, .value = signalValue, .stageMask = waitStage});
    }

    auto SyncScheduler::GetPendingWaits(rhi::QueueType queue) const -> std::span<const SyncWaitEntry> {
        return queues_[QueueIndex(queue)].pendingWaits;
    }

    auto SyncScheduler::GetSignalValue(rhi::QueueType queue) const -> uint64_t {
        auto& state = queues_[QueueIndex(queue)];
        assert(state.nextValue > 1 && "AllocateSignal not called before GetSignalValue");
        return state.nextValue - 1;  // Last allocated value
    }

    auto SyncScheduler::GetSemaphore(rhi::QueueType queue) const -> rhi::SemaphoreHandle {
        return queues_[QueueIndex(queue)].semaphore;
    }

    auto SyncScheduler::GetCurrentValue(rhi::QueueType queue) const -> uint64_t {
        return queues_[QueueIndex(queue)].currentValue;
    }

    auto SyncScheduler::PeekNextSignal(rhi::QueueType queue) const -> uint64_t {
        return queues_[QueueIndex(queue)].nextValue;
    }

    void SyncScheduler::CommitSubmit(rhi::QueueType queue) {
        auto& state = queues_[QueueIndex(queue)];
        state.currentValue = state.nextValue - 1;  // Mark last allocated as committed
        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync] SyncScheduler::CommitSubmit: queue={} currentValue=[{}] nextValue=[{}]",
            rhi::ToString(queue), state.currentValue, state.nextValue
        );
        state.pendingWaits.clear();
    }

    void SyncScheduler::Reset() {
        for (auto& q : queues_) {
            q.nextValue = 1;
            q.currentValue = 0;
            q.pendingWaits.clear();
        }
    }

    // =========================================================================
    // Wait-Graph Diagnostic (specs/03-sync.md §12.4)
    // =========================================================================

    static constexpr const char* kQueueNames[4] = {"Graphics", "Compute", "Transfer", "AsyncCompute"};

    void SyncScheduler::DumpWaitGraph(FILE* iOut) const {
        std::fprintf(iOut, "=== SyncScheduler Wait-Graph Dump ===\n\nQueue States:\n");
        for (uint32_t i = 0; i < kQueueCount; ++i) {
            auto& q = queues_[i];
            std::fprintf(
                iOut, "  %s: current=%llu, pendingSignal=%llu, pendingWaits=[", kQueueNames[i],
                static_cast<unsigned long long>(q.currentValue), static_cast<unsigned long long>(q.nextValue - 1)
            );
            for (size_t w = 0; w < q.pendingWaits.size(); ++w) {
                if (w > 0) {
                    std::fprintf(iOut, ", ");
                }
                // Find which queue this semaphore belongs to
                const char* srcName = "?";
                for (uint32_t j = 0; j < kQueueCount; ++j) {
                    if (queues_[j].semaphore.value == q.pendingWaits[w].semaphore.value) {
                        srcName = kQueueNames[j];
                        break;
                    }
                }
                std::fprintf(iOut, "%s>=%llu", srcName, static_cast<unsigned long long>(q.pendingWaits[w].value));
            }
            std::fprintf(iOut, "]\n");
        }
        if (DetectDeadlock()) {
            std::fprintf(iOut, "\nDetected Issue: DEADLOCK CYCLE DETECTED!\n");
        }
    }

    void SyncScheduler::ExportWaitGraphDOT(std::string& oOut) const {
        oOut = "digraph WaitGraph {\n  rankdir=LR;\n  node [shape=record];\n\n";
        for (uint32_t i = 0; i < kQueueCount; ++i) {
            auto& q = queues_[i];
            oOut += std::format(
                "  {} [label=\"{}|cur={}|target={}\"];\n", kQueueNames[i][0], kQueueNames[i], q.currentValue,
                q.nextValue - 1
            );
        }
        oOut += "\n";
        for (uint32_t i = 0; i < kQueueCount; ++i) {
            for (auto& w : queues_[i].pendingWaits) {
                char srcChar = '?';
                for (uint32_t j = 0; j < kQueueCount; ++j) {
                    if (queues_[j].semaphore.value == w.semaphore.value) {
                        srcChar = kQueueNames[j][0];
                        break;
                    }
                }
                oOut += std::format(
                    "  {} -> {} [label=\"wait {}>={}\"];\n", srcChar, kQueueNames[i][0], srcChar, w.value
                );
            }
        }
        oOut += "}\n";
    }

    void SyncScheduler::ExportWaitGraphJSON(std::string& oOut) const {
        oOut = "{\"queues\":[";
        for (uint32_t i = 0; i < kQueueCount; ++i) {
            if (i > 0) {
                oOut += ",";
            }
            auto& q = queues_[i];
            oOut += std::format(
                "{{\"name\":\"{}\",\"current\":{},\"pendingSignal\":{},\"waits\":[", kQueueNames[i], q.currentValue,
                q.nextValue - 1
            );
            for (size_t w = 0; w < q.pendingWaits.size(); ++w) {
                if (w > 0) {
                    oOut += ",";
                }
                const char* srcName = "?";
                for (uint32_t j = 0; j < kQueueCount; ++j) {
                    if (queues_[j].semaphore.value == q.pendingWaits[w].semaphore.value) {
                        srcName = kQueueNames[j];
                        break;
                    }
                }
                oOut += std::format("{{\"queue\":\"{}\",\"value\":{}}}", srcName, q.pendingWaits[w].value);
            }
            oOut += "]}";
        }
        oOut += std::format("],\"deadlock\":{}}}", DetectDeadlock() ? "true" : "false");
    }

    auto SyncScheduler::DetectDeadlock() const -> bool {
        // Build adjacency: edge from waitQueue → signalQueue if waitQueue has pending wait on signalQueue
        // and signalQueue's current value < waited value (i.e., signalQueue hasn't reached it yet)
        bool adj[kQueueCount][kQueueCount] = {};
        for (uint32_t i = 0; i < kQueueCount; ++i) {
            for (auto& w : queues_[i].pendingWaits) {
                for (uint32_t j = 0; j < kQueueCount; ++j) {
                    if (queues_[j].semaphore.value == w.semaphore.value && queues_[j].currentValue < w.value) {
                        adj[i][j] = true;
                    }
                }
            }
        }
        // DFS cycle detection on 3-node graph
        // Color: 0=white, 1=gray, 2=black
        uint8_t color[kQueueCount] = {};
        auto dfs = [&](auto&& self, uint32_t u) -> bool {
            color[u] = 1;
            for (uint32_t v = 0; v < kQueueCount; ++v) {
                if (!adj[u][v]) {
                    continue;
                }
                if (color[v] == 1) {
                    return true;  // Back edge → cycle
                }
                if (color[v] == 0 && self(self, v)) {
                    return true;
                }
            }
            color[u] = 2;
            return false;
        };
        for (uint32_t i = 0; i < kQueueCount; ++i) {
            if (color[i] == 0 && dfs(dfs, i)) {
                return true;
            }
        }
        return false;
    }

}  // namespace miki::frame
