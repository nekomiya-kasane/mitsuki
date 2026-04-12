/** @file RenderGraphExtension.h
 *  @brief Plugin extension system for the render graph (Phase K, §13).
 *
 *  Provides:
 *    - IRenderGraphExtension: pure interface for injecting passes into the graph
 *    - ExtensionRegistry: priority-ordered registration and lifecycle management
 *    - PsoMissPolicy: async compile + fallback PSO strategy for pipeline misses
 *    - PassPsoConfig: per-pass PSO configuration with primary/fallback descriptors
 *
 *  Extensions declare resource dependencies via the same PassBuilder API.
 *  The compiler treats extension passes identically to built-in passes for
 *  scheduling, barriers, and aliasing — zero special-case logic required.
 *
 *  Thread safety:
 *    - ExtensionRegistry: single-writer, multi-reader (register/unregister on main thread only)
 *    - PsoCompilationQueue: thread-safe (lock-free submit, worker thread drain)
 *    - Extension::BuildPasses: called on render thread only
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "miki/core/Result.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Pipeline.h"

namespace miki::frame {
    struct FrameContext;
}

namespace miki::rhi {
    class PipelineCache;
}

namespace miki::rg {

    class RenderGraphBuilder;

    // =========================================================================
    // Extension capabilities — declared by extensions, filtered by registry
    // =========================================================================

    /// @brief Feature flags an extension may require from the GPU.
    enum class ExtensionCapability : uint32_t {
        None = 0,
        AsyncCompute = 1 << 0,   ///< Requires async compute queue
        MeshShader = 1 << 1,     ///< Requires mesh/task shader support
        RayTracing = 1 << 2,     ///< Requires ray tracing pipeline
        WorkGraphs = 1 << 3,     ///< Requires D3D12 work graphs (SM 6.8+)
        Int64Atomics = 1 << 4,   ///< Requires 64-bit shader atomics
        Multiview = 1 << 5,      ///< Requires VK_KHR_multiview / SV_ViewID
        VariableRate = 1 << 6,   ///< Requires VRS tier 1+
        SparseBinding = 1 << 7,  ///< Requires sparse resource binding
    };

    [[nodiscard]] constexpr auto operator|(ExtensionCapability a, ExtensionCapability b) noexcept
        -> ExtensionCapability {
        return static_cast<ExtensionCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    [[nodiscard]] constexpr auto operator&(ExtensionCapability a, ExtensionCapability b) noexcept
        -> ExtensionCapability {
        return static_cast<ExtensionCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    constexpr auto operator|=(ExtensionCapability& a, ExtensionCapability b) noexcept -> ExtensionCapability& {
        a = a | b;
        return a;
    }
    [[nodiscard]] constexpr auto HasCapability(ExtensionCapability set, ExtensionCapability flag) noexcept -> bool {
        return (set & flag) == flag;
    }

    // =========================================================================
    // Extension build context — passed to BuildPasses()
    // =========================================================================

    /// @brief Read-only context provided to extensions during pass injection.
    struct ExtensionBuildContext {
        const frame::FrameContext* frameContext = nullptr;  ///< Current frame info (dimensions, frame number)
        ExtensionCapability availableCapabilities = ExtensionCapability::None;  ///< GPU caps available
        uint32_t activeExtensionCount = 0;  ///< Number of active extensions this frame
        uint64_t frameNumber = 0;           ///< Monotonic frame counter
        float deltaTimeSeconds = 0.0f;      ///< Time since last frame (for animation)
    };

    // =========================================================================
    // IRenderGraphExtension — pure interface (K-1, §13.1)
    // =========================================================================

    /// @brief Interface for render graph plugins that inject passes into the graph.
    ///
    /// Extensions are invoked in priority order during the graph build phase.
    /// Lower priority values execute earlier (closer to the beginning of the graph).
    /// All resource declarations use the standard PassBuilder API — no special handling.
    ///
    /// Lifecycle:
    ///   1. RegisterExtension() -> OnRegistered() called
    ///   2. Each frame: BuildPasses() called if extension is enabled
    ///   3. UnregisterExtension() -> OnUnregistered() called
    ///   4. At shutdown: Shutdown() called for all remaining extensions
    class IRenderGraphExtension {
       public:
        virtual ~IRenderGraphExtension() = default;

        /// @brief Inject passes into the render graph for the current frame.
        /// Called once per frame in priority order. Use the builder to add passes,
        /// create/import resources, and declare dependencies exactly as in normal graph setup.
        /// @param builder The graph builder to inject passes into.
        /// @param ctx Build context with frame info and capability flags.
        virtual void BuildPasses(RenderGraphBuilder& builder, const ExtensionBuildContext& ctx) = 0;

        /// @brief Called when the extension is registered with the registry.
        /// Override to perform one-time initialization (allocate persistent resources, etc.).
        virtual void OnRegistered() {}

        /// @brief Called when the extension is unregistered from the registry.
        /// Override to release persistent resources acquired in OnRegistered().
        virtual void OnUnregistered() {}

        /// @brief Called at engine shutdown. Clean up all resources.
        /// Always called, even if OnUnregistered() was already called.
        virtual void Shutdown() {}

        /// @brief Unique name for this extension (debug/ordering/lookup).
        /// Must be a compile-time or persistent string (not freed during extension lifetime).
        [[nodiscard]] virtual auto GetName() const noexcept -> const char* = 0;

        /// @brief Execution priority (lower = earlier in graph). Default = 1000.
        /// Recommended ranges:
        ///   [0, 500)    — Pre-scene (input processing, scene updates)
        ///   [500, 1000) — Scene rendering (CAE viz, point cloud, etc.)
        ///   [1000, 1500) — Post-scene (post-processing, compositing)
        ///   [1500, 2000) — Overlay (debug viz, HUD, screenshots)
        [[nodiscard]] virtual auto GetPriority() const noexcept -> int32_t { return 1000; }

        /// @brief GPU capabilities required by this extension.
        /// If the current device doesn't support these, the extension is auto-disabled.
        [[nodiscard]] virtual auto GetRequiredCapabilities() const noexcept -> ExtensionCapability {
            return ExtensionCapability::None;
        }

        /// @brief Names of other extensions this extension depends on.
        /// Registry ensures dependencies are invoked before this extension.
        /// Empty span = no dependencies (default).
        [[nodiscard]] virtual auto GetDependencies() const noexcept -> std::span<const std::string_view> { return {}; }

        /// @brief Whether this extension should be active this frame.
        /// Override for conditional activation (e.g., only active in debug builds).
        /// Default: always active.
        [[nodiscard]] virtual auto IsActiveThisFrame(const ExtensionBuildContext& /*ctx*/) const -> bool {
            return true;
        }
    };

    // =========================================================================
    // ExtensionRegistry — priority-ordered management (K-2, §13.2)
    // =========================================================================

    /// @brief Manages registered extensions with priority ordering and lifecycle.
    ///
    /// Extensions are stored sorted by priority (stable sort on registration).
    /// During BuildPasses(), only extensions whose required capabilities are
    /// satisfied by the device and which report IsActiveThisFrame() == true
    /// are invoked.
    class ExtensionRegistry {
       public:
        ExtensionRegistry() = default;
        ~ExtensionRegistry();

        ExtensionRegistry(const ExtensionRegistry&) = delete;
        auto operator=(const ExtensionRegistry&) -> ExtensionRegistry& = delete;
        ExtensionRegistry(ExtensionRegistry&&) noexcept = default;
        auto operator=(ExtensionRegistry&&) noexcept -> ExtensionRegistry& = default;

        /// @brief Register an extension. Takes ownership. Calls OnRegistered().
        /// If an extension with the same name already exists, the old one is replaced.
        /// @return true if newly registered, false if replaced existing.
        auto RegisterExtension(std::unique_ptr<IRenderGraphExtension> ext) -> bool;

        /// @brief Unregister an extension by name. Calls OnUnregistered().
        /// @return true if found and removed, false if not found.
        auto UnregisterExtension(std::string_view name) -> bool;

        /// @brief Enable or disable an extension by name (runtime toggle).
        /// Disabled extensions are not invoked during BuildPasses() but remain registered.
        /// @return true if found, false if not found.
        auto SetEnabled(std::string_view name, bool enabled) -> bool;

        /// @brief Check if an extension is registered.
        [[nodiscard]] auto IsRegistered(std::string_view name) const -> bool;

        /// @brief Check if an extension is enabled (registered + not manually disabled).
        [[nodiscard]] auto IsEnabled(std::string_view name) const -> bool;

        /// @brief Get an extension by name (nullptr if not found).
        [[nodiscard]] auto FindExtension(std::string_view name) const -> IRenderGraphExtension*;

        /// @brief Set the available GPU capabilities (from device caps at init time).
        void SetAvailableCapabilities(ExtensionCapability caps) noexcept { availableCapabilities_ = caps; }

        /// @brief Invoke BuildPasses() on all active, capable extensions in priority order.
        /// Called by the graph build orchestration code before Build().
        /// @param builder The graph builder to inject passes into.
        /// @param ctx Build context with frame info.
        /// @return Number of extensions that were invoked.
        auto InvokeExtensions(RenderGraphBuilder& builder, const ExtensionBuildContext& ctx) -> uint32_t;

        /// @brief Shutdown all extensions. Called at engine teardown.
        void ShutdownAll();

        /// @brief Get the number of registered extensions.
        [[nodiscard]] auto GetExtensionCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(entries_.size());
        }

        /// @brief Get names of all registered extensions (for debug UI).
        [[nodiscard]] auto GetExtensionNames() const -> std::vector<const char*>;

        /// @brief Get statistics from the last InvokeExtensions() call.
        struct InvocationStats {
            uint32_t totalRegistered = 0;  ///< Total registered extensions
            uint32_t totalEnabled = 0;     ///< Registered and not manually disabled
            uint32_t totalCapable = 0;     ///< Enabled and caps satisfied
            uint32_t totalActive = 0;      ///< Capable and IsActiveThisFrame() == true
            uint32_t totalInvoked = 0;     ///< Actually invoked (= totalActive)
            uint32_t passesInjected = 0;   ///< Total passes added by all extensions
        };
        [[nodiscard]] auto GetLastInvocationStats() const noexcept -> const InvocationStats& { return lastStats_; }

       private:
        struct Entry {
            std::unique_ptr<IRenderGraphExtension> extension;
            bool manuallyDisabled = false;  ///< User-toggled disable (persists across frames)
        };

        void SortByPriority();

        std::vector<Entry> entries_;
        ExtensionCapability availableCapabilities_ = ExtensionCapability::None;
        InvocationStats lastStats_ = {};
        bool needsSort_ = false;
    };

    // =========================================================================
    // PSO miss handling (K-3, §13.4)
    // =========================================================================

    /// @brief Policy for handling PSO compilation misses at execution time.
    enum class PsoMissPolicy : uint8_t {
        Skip,      ///< Skip pass entirely (Tier A default for optional passes)
        Fallback,  ///< Use pre-compiled fallback PSO (Tier B for critical passes)
        Stall,     ///< Block until PSO compiles (last resort, debug only)
    };

    /// @brief Per-pass PSO configuration with primary and optional fallback.
    struct PassPsoConfig {
        rhi::PipelineHandle primaryPso;   ///< Full-featured PSO (may be pending async compile)
        rhi::PipelineHandle fallbackPso;  ///< Simplified PSO (always pre-compiled, invalid = no fallback)
        PsoMissPolicy missPolicy = PsoMissPolicy::Skip;  ///< What to do when primary is not ready
    };

    /// @brief Status of a PSO compilation request.
    enum class PsoCompileStatus : uint8_t {
        Ready,    ///< PSO is compiled and ready for use
        Pending,  ///< PSO is being compiled asynchronously
        Failed,   ///< PSO compilation failed (shader error, etc.)
        Stale,    ///< Source changed, recompilation in progress (previous Ready pipeline still usable)
    };

    /// @brief Result of querying a PSO's readiness.
    struct PsoQueryResult {
        PsoCompileStatus status = PsoCompileStatus::Pending;
        rhi::PipelineHandle handle;  ///< Valid only if status == Ready
    };

    /// @brief Callback type for async PSO compilation completion notification.
    using PsoCompileCallback = std::function<void(rhi::PipelineHandle handle, PsoCompileStatus status)>;

    /// @brief Per-pass PSO resolution result used by the executor.
    struct PsoResolution {
        rhi::PipelineHandle activePso;  ///< The PSO to actually bind (primary or fallback)
        bool isPrimary = true;          ///< True if activePso is the primary (full-featured)
        bool isSkipped = false;         ///< True if pass should be skipped entirely
        PsoMissPolicy appliedPolicy = PsoMissPolicy::Skip;
    };

    // =========================================================================
    // PsoMissHandler — executor integration for PSO miss handling (K-3)
    // =========================================================================

    /// @brief Resolves PSO readiness for each pass at execution time.
    ///
    /// For each pass in the compiled graph:
    ///   1. Check if primaryPso is ready (via user-supplied readiness query)
    ///   2. If ready: bind primary, mark pass as fully featured
    ///   3. If not ready:
    ///      a. If fallbackPso is valid: bind fallback (Tier B)
    ///      b. If missPolicy == Skip: mark pass as skipped (Tier A)
    ///      c. If missPolicy == Stall: block until ready (debug only)
    ///   4. For skipped passes: propagate "not produced" to output resources
    ///   5. Downstream passes consuming "not produced" resources:
    ///      - If they have a fallback input: use it
    ///      - Otherwise: transitively skip them (DCE propagation)
    ///
    /// The handler does NOT modify the compiled graph structure. It produces
    /// a per-pass PsoResolution array consumed by the executor.
    class PsoMissHandler {
       public:
        /// @brief User-supplied function to query PSO readiness.
        /// Returns PsoCompileStatus for a given pipeline handle.
        using ReadinessQueryFn = std::function<PsoCompileStatus(rhi::PipelineHandle)>;

        PsoMissHandler() = default;

        /// @brief Set the readiness query function (typically wraps PipelineCache).
        void SetReadinessQuery(ReadinessQueryFn fn) { readinessQuery_ = std::move(fn); }

        /// @brief Resolve PSO readiness for all passes in the compiled graph.
        /// Must be called after compilation, before execution.
        /// @param graph The compiled graph.
        /// @param configs Per-pass PSO configurations (indexed by compiled pass index).
        ///                If configs.size() < graph.passes.size(), missing entries default to Skip.
        /// @return Per-pass resolution results.
        [[nodiscard]] auto ResolveAll(const CompiledRenderGraph& graph, std::span<const PassPsoConfig> configs)
            -> std::vector<PsoResolution>;

        /// @brief Get the count of passes skipped due to PSO misses in the last ResolveAll().
        [[nodiscard]] auto GetSkippedPassCount() const noexcept -> uint32_t { return skippedCount_; }

        /// @brief Get the count of passes using fallback PSOs in the last ResolveAll().
        [[nodiscard]] auto GetFallbackPassCount() const noexcept -> uint32_t { return fallbackCount_; }

        /// @brief Get the count of passes with ready primary PSOs in the last ResolveAll().
        [[nodiscard]] auto GetReadyPassCount() const noexcept -> uint32_t { return readyCount_; }

        /// @brief Format a human-readable status string (for debug overlay).
        /// Example: "PSO: 85/88 ready, 2 fallback, 1 skipped"
        [[nodiscard]] auto FormatStatus() const -> std::string;

        struct Stats {
            uint32_t totalPasses = 0;
            uint32_t readyPasses = 0;      ///< Primary PSO ready
            uint32_t fallbackPasses = 0;   ///< Using fallback PSO
            uint32_t skippedPasses = 0;    ///< Skipped (Tier A)
            uint32_t stalledPasses = 0;    ///< Stalled waiting for compile (debug only)
            uint32_t transitiveSkips = 0;  ///< Skipped via transitive DCE propagation
            uint32_t failedPasses = 0;     ///< PSO compilation failed
        };
        [[nodiscard]] auto GetStats() const noexcept -> const Stats& { return stats_; }

       private:
        ReadinessQueryFn readinessQuery_;
        uint32_t skippedCount_ = 0;
        uint32_t fallbackCount_ = 0;
        uint32_t readyCount_ = 0;
        Stats stats_ = {};
    };

}  // namespace miki::rg
