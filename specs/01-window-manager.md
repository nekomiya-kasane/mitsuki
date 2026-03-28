# Multi-Window Management System Design

> **Status**: Design blueprint
> **Scope**: Tree-structured window lifecycle, event dispatch, GPU surface management
> **Namespace**: `miki::platform` (window/event), `miki::rhi` (surface/frame)
> **Companion**: `specs/rhi-design.md` §11 (Swapchain/RenderSurface)
> **Supersedes**: `miki::rhi::MultiWindowManager` (flat-list design, deprecated)

---

## 1. Design Goals

| #   | Goal                                       | Rationale                                                                                                              |
| --- | ------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------- |
| G1  | **Tree-structured parent–child hierarchy** | CAD/CAE apps need dockable panels, floating tool windows, property editors — all logically owned by a main window      |
| G2  | **Cascade destruction with GPU safety**    | Destroying a parent must recursively destroy children; GPU work must drain before any surface teardown                 |
| G3  | **Three-concern separation**               | Window creation (OS), event dispatch (input), GPU surface (RHI) are independent subsystems; user explicitly wires them |
| G4  | **Cross-window GPU resource sharing**      | All windows share a single `DeviceHandle`; textures, buffers, pipelines created once, used in any window's render pass |
| G5  | **Zero technical debt**                    | No `void*` leaks, no implicit lifetime coupling, no hidden state machines                                              |
| G6  | **Backend-agnostic**                       | GLFW, SDL3, Qt, Win32 — `IWindowBackend` abstracts all platform specifics                                              |

### 1.1 Non-Goals

- **Window layout / docking framework**: That is application-level (ImGui docking, custom dock manager). This system manages OS windows only.
- **Render scheduling**: RenderGraph decides pass ordering. WindowManager does not know about rendering.
- **Input focus policy**: Application decides which window receives keyboard focus. WindowManager reports focus events.

---

## 2. Architecture Overview

### 2.1 Three-Concern Separation

```
┌──────────────────────────────────────────────────────────────────┐
│                        Application / Demo                         │
│  (wires the three subsystems together, owns the main loop)        │
├──────────────┬───────────────────┬───────────────────────────────┤
│  WindowManager │   Event System    │   SurfaceManager              │
│  (OS windows)  │   (input dispatch)│   (GPU surfaces + frames)     │
│  miki::platform│   miki::platform  │   miki::rhi                   │
├──────────────┴───────────────────┴───────────────────────────────┤
│                      IWindowBackend                               │
│  (GLFW / SDL3 / Qt / Win32 — platform abstraction)               │
└──────────────────────────────────────────────────────────────────┘
```

**Key invariant**: `WindowManager` never touches `RenderSurface`. `SurfaceManager` never creates OS windows. Events flow through `WindowManager` but are consumed by application code.

### 2.2 Layer Diagram with RHI

```
Application
    │
    ├── WindowManager          ← OS window tree (create/destroy/query)
    │       │
    │       └── IWindowBackend ← GLFW / SDL3 / Qt
    │
    ├── SurfaceManager         ← per-window RenderSurface + FrameManager
    │       │
    │       ├── RenderSurface  ← Vulkan VkSwapchainKHR / D3D12 IDXGISwapChain / ...
    │       └── FrameManager   ← fence pacing, acquire/present
    │
    └── DeviceHandle (shared)   ← single GPU device, all windows share
```

---

## 3. Window Tree Model

### 3.1 Design Rationale

| Approach                    | Used By                            | Pros                                    | Cons                                |
| --------------------------- | ---------------------------------- | --------------------------------------- | ----------------------------------- |
| **Flat list**               | Filament, old `MultiWindowManager` | Simple                                  | No parent–child, no cascade destroy |
| **N-ary tree**              | Qt QObject, Win32 HWND             | Natural ownership, cascade destroy      | Slightly more complex iteration     |
| **Forest (multiple roots)** | ImGui multi-viewport               | Multiple independent window hierarchies | Needs special "root" handling       |

**miki choice**: **Forest of N-ary trees**. Each root window is independent. Child windows are owned by their parent. This matches Qt's `QObject` tree model and Win32's owner-window semantics.

### 3.2 WindowHandle

```cpp
namespace miki::platform {

/// Stable, monotonic, generation-counted window identifier.
/// Never reused within a process lifetime (generation prevents ABA).
struct WindowHandle {
    uint32_t id         = 0;   // Slot index (0 = invalid)
    uint16_t generation = 0;   // Incremented on reuse

    [[nodiscard]] constexpr auto IsValid() const noexcept -> bool {
        return id != 0;
    }
    constexpr auto operator==(const WindowHandle&) const noexcept -> bool = default;
};

}  // namespace miki::platform
```

**Change from current**: Current `WindowHandle` has `uint32_t id` only. Adding `generation` prevents stale-handle bugs (same pattern as RHI resource handles in `rhi-design.md` §3).

### 3.3 WindowDesc

```cpp
struct WindowDesc {
    std::string_view title  = "miki";
    uint32_t         width  = 1280;
    uint32_t         height = 720;
    WindowHandle     parent = {};       // {} = root window (no parent)
    WindowFlags      flags  = WindowFlags::None;
};

enum class WindowFlags : uint32_t {
    None        = 0,
    Borderless  = 1 << 0,   // No title bar / frame
    AlwaysOnTop = 1 << 1,   // Stay above parent (tool window)
    NoResize    = 1 << 2,   // Fixed size
    Hidden      = 1 << 3,   // Created hidden, shown explicitly
};
```

**Change from current**: `WindowDesc` gains `parent` field and `flags`. `surfaceConfig` removed — surface creation is now in `SurfaceManager`, not `WindowManager`.

### 3.4 WindowNode (Internal)

```cpp
// Internal to WindowManager::Impl — not exposed in public API
struct WindowNode {
    WindowHandle        handle;
    WindowHandle        parent;             // {} if root
    std::vector<WindowHandle> children;     // Ordered by creation time
    void*               nativeToken;        // Backend-opaque (e.g. GLFWwindow*)
    rhi::NativeWindowHandle nativeWindow;   // Typed platform handle
    rhi::Extent2D       extent;
    bool                alive = true;
    bool                minimized = false;
};
```

The tree is stored as a `ChunkedSlotMap<WindowNode, 16>` — a chunked slot array with free-list and generation counters. Each chunk holds 16 nodes in contiguous memory; new chunks are allocated on demand when all existing slots are occupied. This gives O(1) lookup by slot index, cache-friendly iteration within chunks, and **no hardcoded capacity limit**.

> **Cross-ref**: `specs/03-sync.md` §6 describes the deferred destruction protocol for GPU resources associated with these windows.

### 3.5 Tree Invariants

| Invariant                                                    | Enforcement                                                                       |
| ------------------------------------------------------------ | --------------------------------------------------------------------------------- |
| A child's parent must be alive at child creation time        | `CreateWindow` validates `parent.IsValid()` and `parent.alive`                    |
| Destroying a parent cascades to all descendants (post-order) | `DestroyWindow` recursively destroys children before self                         |
| A root window has `parent = {}`                              | Checked in tree queries                                                           |
| No cycles                                                    | Parent must have `id < child.id` (monotonic allocation) — structurally impossible |
| Max depth = 4                                                | Prevents pathological nesting; CAD apps rarely need >3 levels                     |

---

## 4. WindowManager API

```cpp
namespace miki::platform {

class WindowManager {
public:
    static constexpr uint32_t kDefaultChunkSize = 16;  // ChunkedSlotMap chunk granularity (dynamic, no hard cap)
    static constexpr uint32_t kMaxDepth         = 4;

    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    auto operator=(const WindowManager&) -> WindowManager& = delete;
    WindowManager(WindowManager&&) noexcept;
    auto operator=(WindowManager&&) noexcept -> WindowManager&;

    /// @brief Create a WindowManager with a platform backend.
    [[nodiscard]] static auto Create(std::unique_ptr<IWindowBackend> iBackend)
        -> miki::core::Result<WindowManager>;

    // ── Window lifecycle ────────────────────────────────────────

    /// @brief Create a window. If desc.parent is valid, the new window
    ///        becomes a child of that parent.
    [[nodiscard]] auto CreateWindow(const WindowDesc& iDesc)
        -> miki::core::Result<WindowHandle>;

    /// @brief Destroy a window and all its descendants (post-order cascade).
    ///        Returns the list of destroyed handles (deepest-first).
    ///        Caller is responsible for tearing down GPU surfaces BEFORE calling this.
    [[nodiscard]] auto DestroyWindow(WindowHandle iHandle)
        -> miki::core::Result<std::vector<WindowHandle>>;

    /// @brief Show a hidden window (created with WindowFlags::Hidden).
    auto ShowWindow(WindowHandle iHandle) -> void;

    /// @brief Hide a window without destroying it.
    auto HideWindow(WindowHandle iHandle) -> void;

    // ── Window state operations ──────────────────────────────────

    /// @brief Programmatically resize a window's client area.
    ///        On Emscripten: may be limited by CSS layout constraints.
    auto ResizeWindow(WindowHandle iHandle, uint32_t iWidth, uint32_t iHeight) -> void;

    /// @brief Move a window to a new screen position (top-left corner).
    ///        On Emscripten: no-op (canvas position is CSS-controlled).
    auto SetWindowPosition(WindowHandle iHandle, int32_t iX, int32_t iY) -> void;

    /// @brief Get the current window position (top-left corner).
    ///        On Emscripten: returns (0, 0).
    [[nodiscard]] auto GetWindowPosition(WindowHandle iHandle) const -> std::pair<int32_t, int32_t>;

    /// @brief Minimize (iconify) a window.
    ///        On Emscripten: no-op (browsers have no minimize).
    auto MinimizeWindow(WindowHandle iHandle) -> void;

    /// @brief Maximize a window to fill the screen/work area.
    ///        On Emscripten: requests fullscreen (requires user gesture).
    auto MaximizeWindow(WindowHandle iHandle) -> void;

    /// @brief Restore a minimized or maximized window to normal state.
    ///        On Emscripten: exits fullscreen if active.
    auto RestoreWindow(WindowHandle iHandle) -> void;

    /// @brief Request input focus for a window.
    ///        May be ignored by the OS if the application is not in foreground.
    auto FocusWindow(WindowHandle iHandle) -> void;

    /// @brief Change the window title at runtime.
    ///        On Emscripten: sets document.title for the first window.
    auto SetWindowTitle(WindowHandle iHandle, std::string_view iTitle) -> void;

    // ── Tree queries ────────────────────────────────────────────

    [[nodiscard]] auto GetParent(WindowHandle iHandle) const -> WindowHandle;
    [[nodiscard]] auto GetChildren(WindowHandle iHandle) const
        -> std::span<const WindowHandle>;
    [[nodiscard]] auto GetRoot(WindowHandle iHandle) const -> WindowHandle;
    [[nodiscard]] auto GetDepth(WindowHandle iHandle) const -> uint32_t;

    /// @brief Enumerate all descendants in post-order (leaves first).
    ///        Used by SurfaceManager for safe cascade teardown.
    [[nodiscard]] auto GetDescendantsPostOrder(WindowHandle iHandle) const
        -> std::vector<WindowHandle>;

    // ── State queries ───────────────────────────────────────────

    [[nodiscard]] auto GetWindowInfo(WindowHandle iHandle) const -> WindowInfo;
    [[nodiscard]] auto GetNativeHandle(WindowHandle iHandle) const
        -> rhi::NativeWindowHandle;
    [[nodiscard]] auto GetNativeToken(WindowHandle iHandle) const -> void*;

    // ── Enumeration ─────────────────────────────────────────────

    [[nodiscard]] auto GetWindowCount() const noexcept -> uint32_t;
    [[nodiscard]] auto GetAllWindows() const -> std::vector<WindowHandle>;
    [[nodiscard]] auto GetRootWindows() const -> std::vector<WindowHandle>;
    [[nodiscard]] auto GetActiveWindows() -> std::vector<WindowHandle>;

    // ── Event polling ───────────────────────────────────────────

    /// @brief Poll OS events. Returns per-window event stream.
    ///        Updates alive/minimized state. Does NOT auto-destroy closed windows
    ///        (caller decides policy via CloseRequested event).
    auto PollEvents() -> std::span<const WindowEvent>;

    /// @brief Check if ALL root windows have been closed.
    [[nodiscard]] auto ShouldClose() const -> bool;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WindowManager(std::unique_ptr<Impl> iImpl);
};

}  // namespace miki::platform
```

### 4.1 Key Changes from Current Design

| Aspect          | Old (`MultiWindowManager`)            | New (`WindowManager`)                                           |
| --------------- | ------------------------------------- | --------------------------------------------------------------- |
| Namespace       | `miki::rhi`                           | `miki::platform`                                                |
| Tree structure  | Flat list                             | N-ary forest                                                    |
| GPU coupling    | Owns `RenderSurface` + `FrameManager` | Pure OS windows, no GPU                                         |
| Cascade destroy | None                                  | Post-order recursive                                            |
| Handle          | `uint32_t id` only                    | `id` + `generation`                                             |
| Close policy    | `ProcessWindowEvents()` auto-destroys | `CloseRequested` event, user decides                            |
| Max windows     | 8 (hard limit)                        | No hard limit (ChunkedSlotMap, 16 per chunk, dynamic expansion) |
| `WindowFlags`   | None                                  | Borderless, AlwaysOnTop, NoResize, Hidden                       |

### 4.2 IWindowBackend Changes

```cpp
class IWindowBackend {
public:
    virtual ~IWindowBackend() = default;

    /// @brief Create an OS window with optional parent.
    /// @param iParentToken  Native token of parent window (nullptr = root).
    [[nodiscard]] virtual auto CreateNativeWindow(
        const WindowDesc& iDesc,
        void*             iParentToken,   // NEW: parent for child windows
        void*&            oNativeToken
    ) -> miki::core::Result<rhi::NativeWindowHandle> = 0;

    virtual auto DestroyNativeWindow(void* iNativeToken) -> void = 0;
    virtual auto PollEvents(std::vector<WindowEvent>& ioEvents) -> void = 0;
    [[nodiscard]] virtual auto ShouldClose(void* iNativeToken) -> bool = 0;
    [[nodiscard]] virtual auto GetFramebufferSize(void* iNativeToken) -> rhi::Extent2D = 0;
    [[nodiscard]] virtual auto IsMinimized(void* iNativeToken) -> bool = 0;

    /// @brief Show a hidden window.
    virtual auto ShowWindow(void* iNativeToken) -> void = 0;

    /// @brief Hide a window.
    virtual auto HideWindow(void* iNativeToken) -> void = 0;

    // ── Window state operations ──────────────────────────────────

    /// @brief Resize window client area.
    virtual auto ResizeWindow(void* iNativeToken, uint32_t iWidth, uint32_t iHeight) -> void = 0;

    /// @brief Set window position (top-left corner in screen coordinates).
    virtual auto SetWindowPosition(void* iNativeToken, int32_t iX, int32_t iY) -> void = 0;

    /// @brief Get window position (top-left corner in screen coordinates).
    [[nodiscard]] virtual auto GetWindowPosition(void* iNativeToken) const -> std::pair<int32_t, int32_t> = 0;

    /// @brief Minimize (iconify) a window.
    virtual auto MinimizeWindow(void* iNativeToken) -> void = 0;

    /// @brief Maximize a window.
    virtual auto MaximizeWindow(void* iNativeToken) -> void = 0;

    /// @brief Restore a minimized or maximized window to normal state.
    virtual auto RestoreWindow(void* iNativeToken) -> void = 0;

    /// @brief Request input focus for a window.
    virtual auto FocusWindow(void* iNativeToken) -> void = 0;

    /// @brief Change the window title.
    virtual auto SetWindowTitle(void* iNativeToken, std::string_view iTitle) -> void = 0;

    virtual auto SetWindowHandle(void* iNativeToken, WindowHandle iHandle) -> void {
        (void)iNativeToken; (void)iHandle;
    }
};
```

**Key change**: `CreateNativeWindow` takes `iParentToken` for OS-level parent-child relationship.

**`iParentToken` semantics are backend-defined** — the backend interprets the opaque `void*` according to its platform model. The public RHI/WindowManager API is unaware of the distinction; all parent-child logic above the backend is purely handle-based.

| Backend              | `iParentToken` interpretation                                           | GPU-level effect                                                      |
| -------------------- | ----------------------------------------------------------------------- | --------------------------------------------------------------------- |
| GLFW (OpenGL)        | `GLFWwindow*` of parent — passed as share context to `glfwCreateWindow` | Shared GL context (textures, buffers, shaders shared across windows)  |
| GLFW (Vulkan/WebGPU) | `GLFWwindow*` of parent — stored for logical tree tracking              | None (Vulkan surfaces are independent; no shared context concept)     |
| GLFW (Emscripten)    | `GLFWwindow*` of parent — logical tree only (no OS owner window)        | None (browser manages canvas z-order; parent-child is purely logical) |
| Win32                | `HWND` of parent — passed as owner to `CreateWindowEx`                  | None (owner window controls lifetime/z-order, not GPU resources)      |

GLFW backend mapping:

| IWindowBackend                               | GLFW                                                                                                                                              |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CreateNativeWindow(desc, parentToken, ...)` | `glfwCreateWindow(w, h, title, nullptr, parentGlfw)` — GL: share context; Vulkan: logical parent only (no GPU-level sharing, stored for tree ops) |
| `ShowWindow`                                 | `glfwShowWindow`                                                                                                                                  |
| `HideWindow`                                 | `glfwHideWindow`                                                                                                                                  |
| `ResizeWindow`                               | `glfwSetWindowSize`                                                                                                                               |
| `SetWindowPosition`                          | `glfwSetWindowPos`                                                                                                                                |
| `GetWindowPosition`                          | `glfwGetWindowPos`                                                                                                                                |
| `MinimizeWindow`                             | `glfwIconifyWindow`                                                                                                                               |
| `MaximizeWindow`                             | `glfwMaximizeWindow`                                                                                                                              |
| `RestoreWindow`                              | `glfwRestoreWindow`                                                                                                                               |
| `FocusWindow`                                | `glfwFocusWindow`                                                                                                                                 |
| `SetWindowTitle`                             | `glfwSetWindowTitle`                                                                                                                              |

Emscripten (GLFW `contrib.glfw3`) backend mapping:

| IWindowBackend                               | Emscripten GLFW (`contrib.glfw3`)                                                                                                                                                       |
| -------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CreateNativeWindow(desc, parentToken, ...)` | `emscripten::glfw3::SetNextWindowCanvasSelector(selector)` then `glfwCreateWindow(w, h, title, nullptr, nullptr)` — each window maps to one `<canvas>`. `iParentToken` is logical only. |
| `ShowWindow`                                 | `glfwShowWindow` (maps to CSS `display` property on the canvas)                                                                                                                         |
| `HideWindow`                                 | `glfwHideWindow` (maps to CSS `display:none`)                                                                                                                                           |
| `IsMinimized`                                | Always `false` (browsers have no minimize); optionally check `document.hidden` for tab-away                                                                                             |
| `GetFramebufferSize`                         | `glfwGetFramebufferSize` — Hi DPI aware (respects `GLFW_SCALE_FRAMEBUFFER`)                                                                                                             |
| `ShouldClose`                                | `glfwWindowShouldClose` — triggered by application logic (no OS close button on canvas)                                                                                                 |
| `NativeWindowHandle`                         | Returns `WebWindow{canvasSelector}` — the CSS selector string (e.g. `"#canvas1"`)                                                                                                       |
| `ResizeWindow`                               | `glfwSetWindowSize` — may be constrained by CSS layout; canvas size changes but container may override                                                                                  |
| `SetWindowPosition`                          | No-op — canvas position is CSS-controlled                                                                                                                                               |
| `GetWindowPosition`                          | Returns `(0, 0)` — position is not meaningful for embedded canvases                                                                                                                     |
| `MinimizeWindow`                             | No-op — browsers have no minimize concept                                                                                                                                               |
| `MaximizeWindow`                             | `emscripten::glfw3::RequestFullscreen(window, false, true)` — requires user gesture                                                                                                     |
| `RestoreWindow`                              | `emscripten_exit_fullscreen()` if in fullscreen; otherwise no-op                                                                                                                        |
| `FocusWindow`                                | `glfwFocusWindow` — focuses the canvas element                                                                                                                                          |
| `SetWindowTitle`                             | `glfwSetWindowTitle` — sets `document.title` for first window, custom attribute for others                                                                                              |

Win32 backend mapping:

| IWindowBackend                               | Win32                                                                            |
| -------------------------------------------- | -------------------------------------------------------------------------------- |
| `CreateNativeWindow(desc, parentToken, ...)` | `CreateWindowEx(WS_EX_TOOLWINDOW, ..., parentHwnd, ...)` — OS-level owner window |
| Cascade close                                | Win32 automatically sends `WM_CLOSE` to owned windows when owner closes          |

---

## 5. SurfaceManager — GPU Surface Lifecycle

`SurfaceManager` owns the per-window `RenderSurface` and `FrameManager` instances. It is **separate from `WindowManager`** — the user explicitly binds a GPU surface to an OS window.

### 5.1 RenderSurfaceConfig

```cpp
namespace miki::rhi {

/// Presentation mode — maps to VkPresentModeKHR / DXGI swap effect.
enum class PresentMode : uint8_t {
    Fifo,            // VSync ON — guaranteed by all backends (default)
    FifoRelaxed,     // VSync ON, but late frames present immediately (Vulkan only)
    Mailbox,         // Triple-buffer, lowest-latency VSync — T1 Vulkan / D3D12
    Immediate,       // No VSync — tearing allowed (DXGI_PRESENT_ALLOW_TEARING / VK_PRESENT_MODE_IMMEDIATE_KHR)
};

/// Color space for swapchain output.
enum class SurfaceColorSpace : uint8_t {
    SRGB,            // sRGB transfer, Rec.709 gamut (default, universally supported)
    HDR10_ST2084,    // PQ transfer, Rec.2020 gamut (VK_COLOR_SPACE_HDR10_ST2084_EXT / DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
    scRGBLinear,     // Linear FP16 (VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT / DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) — D3D12/Vulkan T1
};

/// Variable Refresh Rate mode.
/// All VRR modes use the same underlying API (DXGI_PRESENT_ALLOW_TEARING / VK_PRESENT_MODE_*).
/// G-Sync Compatible monitors work via AdaptiveSync; proprietary G-Sync modules also use this path.
enum class VRRMode : uint8_t {
    Off,            // VSync on, no tearing, fixed refresh
    AdaptiveSync,   // VESA AdaptiveSync / AMD FreeSync / NVIDIA G-Sync Compatible
    GSync,          // NVIDIA G-Sync (same API path as AdaptiveSync, explicit opt-in for G-Sync-specific features)
};

/// Swapchain image count hint.
/// Backend may adjust based on PresentMode constraints (e.g. Mailbox requires >= 3).
enum class ImageCountHint : uint8_t {
    Auto,           // Backend chooses optimal (typically 3 for Mailbox, 2 for Fifo)
    Minimal,        // Minimize VRAM (2 images, may increase latency)
    Triple,         // Force triple buffering (3 images)
};

/// Per-window surface configuration. Passed to AttachSurface; can be changed via detach-reattach.
struct RenderSurfaceConfig {
    PresentMode         presentMode     = PresentMode::Fifo;
    SurfaceColorSpace   colorSpace      = SurfaceColorSpace::SRGB;
    Format              preferredFormat = Format::BGRA8_SRGB;
    VRRMode             vrrMode         = VRRMode::Off;
    ImageCountHint      imageCount      = ImageCountHint::Auto;

    // Note: Backend may override preferredFormat based on colorSpace.
    // e.g. scRGBLinear → R16G16B16A16_SFLOAT on T1 (Vulkan/D3D12).
};

}  // namespace miki::rhi
```

**Backend mapping**:

| Field         | Vulkan T1                                                                  | D3D12 T1                                                            | WebGPU T3                                                  | OpenGL T4                |
| ------------- | -------------------------------------------------------------------------- | ------------------------------------------------------------------- | ---------------------------------------------------------- | ------------------------ |
| `presentMode` | `VkPresentModeKHR` — query via `vkGetPhysicalDeviceSurfacePresentModesKHR` | `DXGI_SWAP_EFFECT_FLIP_DISCARD` + sync interval                     | `GPUCanvasAlphaMode` (limited: Fifo only on most browsers) | `glfwSwapInterval(0\|1)` |
| `colorSpace`  | `VkColorSpaceKHR` via `VK_EXT_swapchain_colorspace`                        | `IDXGISwapChain3::SetColorSpace1`                                   | sRGB only (no HDR support as of 2026)                      | sRGB only                |
| `vrrMode`     | `VK_PRESENT_MODE_FIFO_RELAXED_KHR` or `MAILBOX` + display VRR              | `DXGI_PRESENT_ALLOW_TEARING` + `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` | N/A                                                        | `glfwSwapInterval(0)`    |
| `imageCount`  | `VkSwapchainCreateInfoKHR::minImageCount`                                  | `DXGI_SWAP_CHAIN_DESC1::BufferCount`                                | Fixed (browser-controlled)                                 | Double-buffer only       |
| HDR10 format  | `VK_FORMAT_A2B10G10R10_UNORM_PACK32` or `R16G16B16A16_SFLOAT`              | `DXGI_FORMAT_R10G10B10A2_UNORM` or `R16G16B16A16_FLOAT`             | N/A                                                        | N/A                      |

**VRR (Variable Refresh Rate)**: Enabled when `vrrMode != Off` and the display supports the requested mode. Backend queries OS/driver capabilities at `AttachSurface` time via `DXGI_OUTPUT_DESC1::SupportsVRR` (D3D12) or display property queries (Vulkan). If VRR is unavailable, the mode is silently downgraded to fixed-rate with a warning logged. G-Sync and AdaptiveSync use the same API path; the enum distinction allows future G-Sync-specific optimizations (e.g., pulsar mode, reflex integration).

### 5.2 API

```cpp
namespace miki::rhi {

class SurfaceManager {
public:
    ~SurfaceManager();

    SurfaceManager(const SurfaceManager&) = delete;
    auto operator=(const SurfaceManager&) -> SurfaceManager& = delete;
    SurfaceManager(SurfaceManager&&) noexcept;
    auto operator=(SurfaceManager&&) noexcept -> SurfaceManager&;

    /// @brief Create a SurfaceManager bound to a shared device.
    [[nodiscard]] static auto Create(DeviceHandle iDevice)
        -> miki::core::Result<SurfaceManager>;

    // ── Surface lifecycle ───────────────────────────────────────

    /// @brief Attach a RenderSurface to a window.
    ///        Creates swapchain + sync objects using the shared DeviceHandle.
    [[nodiscard]] auto AttachSurface(
        platform::WindowHandle iWindow,
        NativeWindowHandle     iNativeWindow,
        const RenderSurfaceConfig& iConfig = {}
    ) -> miki::core::Result<void>;

    /// @brief Detach and destroy the surface for a window.
    ///        Waits for THIS surface's in-flight frames only (per-surface timeline wait).
    ///        Other windows continue rendering uninterrupted.
    ///        See specs/03-sync.md §9 for the timeline wait protocol.
    [[nodiscard]] auto DetachSurface(platform::WindowHandle iWindow)
        -> miki::core::Result<void>;

    /// @brief Detach surfaces for multiple windows (batch, post-order safe).
    ///        Each surface is waited on individually (per-surface timeline wait).
    ///        No device->WaitIdle() — other windows are unaffected.
    [[nodiscard]] auto DetachSurfaces(std::span<const platform::WindowHandle> iWindows)
        -> miki::core::Result<void>;

    /// @brief Check if a window has an attached surface.
    [[nodiscard]] auto HasSurface(platform::WindowHandle iWindow) const -> bool;

    // ── Per-window access ───────────────────────────────────────

    [[nodiscard]] auto GetRenderSurface(platform::WindowHandle iWindow) -> RenderSurface*;
    [[nodiscard]] auto GetFrameManager(platform::WindowHandle iWindow) -> FrameManager*;

    // ── Frame operations ────────────────────────────────────────

    /// @brief Begin frame for a specific window's surface.
    [[nodiscard]] auto BeginFrame(platform::WindowHandle iWindow)
        -> miki::core::Result<FrameContext>;

    /// @brief End frame for a specific window's surface.
    [[nodiscard]] auto EndFrame(platform::WindowHandle iWindow, CommandListHandle& iCmd)
        -> miki::core::Result<void>;

    /// @brief Resize a window's surface (typically after WindowEvent::Resize).
    [[nodiscard]] auto ResizeSurface(
        platform::WindowHandle iWindow,
        uint32_t iWidth, uint32_t iHeight
    ) -> miki::core::Result<void>;

    // ── Dynamic present configuration ───────────────────────────

    /// @brief Change present mode at runtime (triggers swapchain recreation).
    ///        Waits for this surface's in-flight frames before recreating.
    [[nodiscard]] auto SetPresentMode(platform::WindowHandle iWindow, PresentMode iMode)
        -> miki::core::Result<void>;

    /// @brief Change color space at runtime (triggers swapchain recreation).
    ///        Returns error if the display/backend does not support the requested color space.
    [[nodiscard]] auto SetColorSpace(platform::WindowHandle iWindow, SurfaceColorSpace iSpace)
        -> miki::core::Result<void>;

    /// @brief Query supported present modes for a window's display.
    [[nodiscard]] auto GetSupportedPresentModes(platform::WindowHandle iWindow) const
        -> std::vector<PresentMode>;

    /// @brief Query supported color spaces for a window's display.
    [[nodiscard]] auto GetSupportedColorSpaces(platform::WindowHandle iWindow) const
        -> std::vector<SurfaceColorSpace>;

    // ── Bulk operations ─────────────────────────────────────

    /// @brief Wait for all surfaces' GPU work to complete.
    auto WaitAll() -> void;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit SurfaceManager(std::unique_ptr<Impl> iImpl);
};

}  // namespace miki::rhi
```

### 5.3 GPU Resource Sharing Model

All windows share a single `DeviceHandle`. This is the standard approach used by Filament, Diligent, and Vulkan best practices:

```
┌─ DeviceHandle ─────────────────────────────────────────────┐
│                                                        │
│  ┌─ Window A ──────┐  ┌─ Window B ──────┐            │
│  │ RenderSurface   │  │ RenderSurface   │            │
│  │ FrameManager    │  │ FrameManager    │            │
│  │ Swapchain imgs  │  │ Swapchain imgs  │            │
│  │ Sync primitives │  │ Sync primitives │            │
│  └─────────────────┘  └─────────────────┘            │
│                                                        │
│  ┌─ Shared GPU Resources ────────────────────────┐    │
│  │ Textures, Buffers, Pipelines, Samplers,       │    │
│  │ DescriptorSets, AccelStructures               │    │
│  │ (created once, used in any window's passes)    │    │
│  └───────────────────────────────────────────────┘    │
└────────────────────────────────────────────────────────┘
```

**What is per-window**: `RenderSurface` (swapchain images), `FrameManager` (fences/semaphores), sync primitives.

**What is shared**: Everything else. A texture created for Window A can be sampled in Window B's render pass without copies.

#### 5.3.1 Cross-Window Content Sharing Rule (Invariant)

> **No render pass may directly read another window's swapchain image.**

Swapchain images are owned by the OS presentation engine; their layout state (`VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`) and lifetime are outside application control. Directly sampling them as SRV from another window's render pass violates Vulkan/D3D12 best practice and can trigger synchronization deadlocks.

**Correct pattern**: If Window B needs to display Window A's rendered content (e.g., a thumbnail preview panel), Window A renders to a shared `OffscreenTarget` (a normal `VkImage`/`ID3D12Resource`), then both windows' compositor passes blit from that `OffscreenTarget` to their own swapchain.

```
Window A pipeline:
  ... geometry → resolve → post → write to OffscreenTarget_A (shared VkImage)
  Compositor: blit OffscreenTarget_A → Swapchain_A

Window B pipeline (thumbnail panel):
  Compositor: blit OffscreenTarget_A → Swapchain_B (downscaled)
```

This ensures:

- Swapchain images never appear as SRV in any descriptor set
- No cross-window synchronization required beyond normal shared-resource barriers
- `DetachSurface(WindowA)` does not affect Window B's access to `OffscreenTarget_A` (the offscreen target outlives the swapchain)

> **Cross-ref**: `specs/rendering-pipeline-architecture.md` Pass #66 (Offscreen Render) uses this pattern for hi-res tile-based rendering.

Backend mapping:

| Backend | Shared                                                                          | Per-Window                                                          |
| ------- | ------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| Vulkan  | `VkDevice`, `VkPhysicalDevice`, descriptor pools, pipeline cache, VMA allocator | `VkSwapchainKHR`, `VkSurfaceKHR`, per-frame `VkSemaphore`/`VkFence` |
| D3D12   | `ID3D12Device`, `ID3D12CommandQueue`, descriptor heaps, PSO cache, D3D12MA      | `IDXGISwapChain4`, per-frame `ID3D12Fence`                          |
| WebGPU  | `wgpu::Device`, bind group layouts, pipeline cache                              | `wgpu::Surface`, per-frame texture views                            |
| OpenGL  | Single shared context (all state)                                               | FBO per window (or `wglMakeCurrent` context switch)                 |

**OpenGL special case**: GL requires context-per-window or `wglShareLists`. The backend handles this internally — `GlfwWindowBackend` passes the first window's `GLFWwindow*` as share context to subsequent `glfwCreateWindow` calls. This is already implemented in `GlfwWindowBackend::glShareContext_`.

---

## 6. Cascade Destruction Protocol

The most critical safety requirement. Destroying a window must:

1. First drain GPU work for each surface (**per-surface**, not global WaitIdle)
2. Destroy GPU surfaces (leaf-to-root)
3. Destroy OS windows (leaf-to-root)

> **Cross-ref**: `specs/03-sync.md` §9 defines the detailed timeline semaphore wait protocol used here.

### 6.1 Destruction Sequence

```
User calls: DestroyWindowCascade(parentHandle)
                    │
                    ▼
    ┌─ WindowManager::GetDescendantsPostOrder(parent) ─┐
    │   Returns: [grandchild2, grandchild1, child1,     │
    │             child2, parent]  (leaves first)        │
    └────────────────────────────────────────────────────┘
                    │
                    ▼
    ┌─ SurfaceManager::DetachSurfaces(postOrderList) ───┐
    │   For each handle in post-order:                   │
    │      a. FrameManager::WaitAll()                    │
    │         T1: CPU wait on THIS surface's timeline    │
    │             semaphore values (per-surface, surgical)│
    │         T2: CPU wait on THIS surface's VkFences    │
    │         *** NO device->WaitIdle() ***              │
    │      b. DeferredDestructor::DrainAll() for surface │
    │      c. FrameManager destroyed                     │
    │      d. RenderSurface destroyed                    │
    │         (internally: vkDestroySwapchainKHR, etc.)  │
    └────────────────────────────────────────────────────┘
                    │
                    ▼
    ┌─ WindowManager::DestroyWindow(parent) ────────────┐
    │   For each handle in post-order:                   │
    │      IWindowBackend::DestroyNativeWindow(token)    │
    │      Remove from tree, increment generation        │
    └────────────────────────────────────────────────────┘
```

**Key difference from previous design**: `DetachSurfaces` uses **per-surface `FrameManager::WaitAll()`** (which waits only on that surface's in-flight timeline values), NOT `device->WaitIdle()`. Other windows continue rendering uninterrupted during cascade destruction.

**Why per-surface wait is safe**: The cross-window content sharing rule (§5.3.1) guarantees no render pass reads another window's swapchain image. Shared resources (textures, buffers, pipelines) are not destroyed during surface detach — only swapchain images and per-surface sync primitives are released. Therefore, waiting for only the target surface's in-flight frames is sufficient.

### 6.2 Helper: DestroyWindowCascade

This is an application-level helper that orchestrates the two managers:

```cpp
/// @brief Cascade-destroy a window and all children with GPU safety.
///        This is NOT a method on WindowManager — it orchestrates both managers.
///        Other windows continue rendering uninterrupted (no global WaitIdle).
[[nodiscard]] inline auto DestroyWindowCascade(
    platform::WindowManager& iWm,
    rhi::SurfaceManager&     iSm,
    platform::WindowHandle   iHandle
) -> miki::core::Result<void>
{
    // 1. Get all descendants + self in post-order (leaves first)
    auto victims = iWm.GetDescendantsPostOrder(iHandle);
    victims.push_back(iHandle);  // self last

    // 2. Detach all GPU surfaces (per-surface timeline wait, NOT WaitIdle)
    auto detachResult = iSm.DetachSurfaces(victims);
    if (!detachResult) return detachResult;

    // 3. Destroy OS windows (post-order)
    auto destroyResult = iWm.DestroyWindow(iHandle);
    if (!destroyResult) return miki::core::Result<void>{destroyResult.error()};

    return {};
}
```

### 6.3 Why Not Automatic Cascade in WindowManager?

| Design                                                           | Pros                                     | Cons                                                                 |
| ---------------------------------------------------------------- | ---------------------------------------- | -------------------------------------------------------------------- |
| Auto cascade (WindowManager owns surfaces)                       | Single call                              | Violates G3 (three-concern separation); WindowManager depends on RHI |
| **Manual cascade (user orchestrates)**                           | Clean separation; testable independently | User writes 3 lines of glue code                                     |
| Callback-based (WindowManager fires "about to destroy" callback) | Separation preserved                     | Hidden control flow; harder to debug                                 |

**Choice**: Manual orchestration + helper function. The helper is trivial (shown above) and keeps the architecture clean. `DestroyWindowCascade` is provided in `<mitsuki/platform/WindowManagerUtils.h>`.

> **Note**: While manual orchestration requires the caller to remember to use the helper, this is enforced at runtime: in debug builds, `WindowManager::DestroyWindow(h)` asserts that `SurfaceManager::HasSurface(h) == false`; in release builds, it returns `ErrorCode::PreconditionViolated`. This makes the "wrong path" immediately visible.

---

## 7. Event System Integration

### 7.1 Event Flow

```
OS (GLFW/SDL/Win32)
    │  callbacks
    ▼
IWindowBackend::PollEvents()
    │  appends to buffer
    ▼
WindowManager::PollEvents()
    │  returns span<const WindowEvent>
    ▼
Application main loop
    │  dispatches per-window
    ▼
┌─────────────────────────────────────────┐
│  match event.window → route to handler  │
│  CloseRequested → user decides policy   │
│  Resize → SurfaceManager::ResizeSurface │
│  KeyDown/MouseMove → input system       │
└─────────────────────────────────────────┘
```

### 7.2 WindowEvent (unchanged from current)

```cpp
struct WindowEvent {
    WindowHandle          window;
    neko::platform::Event event;   // std::variant<CloseRequested, Resize, ...>
};
```

### 7.3 Close Policy

Current `MultiWindowManager::ProcessWindowEvents()` automatically destroys windows on close request. This is **removed**. The new design returns `CloseRequested` as an event and lets the application decide:

```cpp
// Application decides close policy
for (auto& ev : windowManager.PollEvents()) {
    std::visit(overloaded{
        [&](neko::platform::CloseRequested) {
            // Option A: immediate cascade destroy
            DestroyWindowCascade(windowManager, surfaceManager, ev.window);

            // Option B: prompt user "Save changes?"
            // showSaveDialog(ev.window);

            // Option C: hide instead of destroy (tool window)
            // windowManager.HideWindow(ev.window);
        },
        [&](neko::platform::Resize r) {
            surfaceManager.ResizeSurface(ev.window, r.width, r.height);
        },
        [&](auto&&) { /* forward to input system */ }
    }, ev.event);
}
```

---

## 8. Typical Usage — Main Loop

```cpp
int main() {
    // 1. Create platform backend
    auto backend = std::make_unique<miki::demo::GlfwWindowBackend>(
        rhi::BackendType::Vulkan, /*visible=*/true
    );

    // 2. Create WindowManager (OS windows only)
    auto wm = platform::WindowManager::Create(std::move(backend)).value();

    // 3. Create main window (root)
    auto mainWin = wm.CreateWindow({
        .title = "miki — Main", .width = 1920, .height = 1080
    }).value();

    // 4. Create GPU device
    DeviceDesc desc{
        .backend = rhi::BackendType::Vulkan14,
        .adapterIndex = 0,
        .enableValidation = true,
        .enableGpuCapture = false,
        .requiredExtensions = {},
    };
    auto device = CreateDevice(desc).value();

    // 5. Create SurfaceManager (GPU surfaces)
    // Note: DeviceHandle is the type-erased facade for multi-window sharing
    DeviceHandle deviceHandle{device.get()};  // Wrap concrete device
    auto sm = rhi::SurfaceManager::Create(deviceHandle).value();
    sm.AttachSurface(mainWin, wm.GetNativeHandle(mainWin)).value();

    // 6. Create child windows
    auto toolWin = wm.CreateWindow({
        .title = "Properties", .width = 400, .height = 600,
        .parent = mainWin, .flags = platform::WindowFlags::AlwaysOnTop
    }).value();
    sm.AttachSurface(toolWin, wm.GetNativeHandle(toolWin)).value();

    auto viewportWin = wm.CreateWindow({
        .title = "3D Viewport", .width = 800, .height = 600,
        .parent = mainWin
    }).value();
    sm.AttachSurface(viewportWin, wm.GetNativeHandle(viewportWin)).value();

    // 7. Shared GPU resources (available to ALL windows)
    auto sharedTexture = device->CreateTexture({...}).value();
    auto sharedPipeline = device->CreateGraphicsPipeline({...}).value();

    // 8. Main loop
    while (!wm.ShouldClose()) {
        auto events = wm.PollEvents();
        // ... handle events (see §7.3) ...

        for (auto win : wm.GetActiveWindows()) {
            auto ctx = sm.BeginFrame(win).value();
            auto cmd = rhi::CreateCommandBuffer({.type = rhi::QueueType::Graphics}).value();
            // ... record using sharedTexture, sharedPipeline ...
            sm.EndFrame(win, cmd).value();
        }
    }

    // 9. Shutdown: cascade from roots
    for (auto root : wm.GetRootWindows()) {
        DestroyWindowCascade(wm, sm, root).value();
    }
    // device destroyed last (outlives all surfaces)
}
```

---

## 9. Backend-Specific Considerations

### 9.1 Vulkan Multi-Window

- **Single `VkDevice`**: One device, one graphics queue. All windows share it.
- **Per-window `VkSurfaceKHR` + `VkSwapchainKHR`**: Created in `RenderSurface::Create`.
- **Present**: Each window presents independently. Semaphore wait/signal per swapchain.
- **Queue family**: `vkGetPhysicalDeviceSurfaceSupportKHR` must be checked for each surface. In practice, the graphics queue supports all surfaces on desktop GPUs.
- **Destruction order**: `vkDestroySwapchainKHR` → `vkDestroySurfaceKHR`. Device outlives all.

### 9.2 D3D12 Multi-Window

- **Single `ID3D12Device` + `ID3D12CommandQueue`**: Shared.
- **Per-window `IDXGISwapChain4`**: Created via `IDXGIFactory::CreateSwapChainForHwnd`.
- **Present**: `IDXGISwapChain::Present(syncInterval, flags)` per window.
- **Tearing**: `DXGI_PRESENT_ALLOW_TEARING` for Immediate mode (per-window).

### 9.3 WebGPU Multi-Window

- **Single `wgpu::Device`**: Shared.
- **Per-window `wgpu::Surface`**: `instance.CreateSurface(surfaceDesc)`.
- **Limitation**: Some browsers restrict to one surface per device. Fallback: render all windows to offscreen textures, blit to single surface.

### 9.4 OpenGL Multi-Window

- **Shared context**: `glfwCreateWindow(w, h, title, nullptr, shareContext)` where `shareContext` is the first window's `GLFWwindow*`.
- **Context switching**: `glfwMakeContextCurrent(targetWindow)` before each window's draw calls.
- **Resource sharing**: All textures, buffers, programs are shared across contexts via `wglShareLists` / `glXShareLists` (handled by GLFW internally).
- **Already implemented**: `GlfwWindowBackend::glShareContext_` field exists.

### 9.5 Emscripten/WASM Adaptation

On the web platform, there are no OS windows — each GLFW "window" maps to an HTML `<canvas>` element. The adaptation uses `contrib.glfw3` (pongasoft/emscripten-glfw), a full C++ reimplementation of GLFW 3.4 for Emscripten that supports multiple canvases, Hi DPI, keyboard/mouse, gamepad, clipboard, and fullscreen.

#### 9.5.1 Core Mapping: Window = Canvas

| Desktop Concept      | Emscripten Equivalent                                                                                                           |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| OS window            | `<canvas>` element in the HTML DOM, identified by CSS selector (e.g. `"#canvas1"`)                                              |
| `NativeWindowHandle` | `WebWindow{canvasSelector}` — stored as `const char*` pointing to a backend-owned string                                        |
| `nativeToken`        | `GLFWwindow*` — same as desktop GLFW, but backed by a canvas instead of an OS window                                            |
| Window title         | Set via `glfwSetWindowTitle` — maps to `document.title` for the first window, or a custom attribute for subsequent ones         |
| Window position      | Not meaningful — canvas position is controlled by CSS layout                                                                    |
| Framebuffer size     | `glfwGetFramebufferSize` — respects `GLFW_SCALE_FRAMEBUFFER` for Hi DPI (canvas pixel size ≠ CSS size on 4K displays)           |
| Minimize / Maximize  | Not applicable — `IsMinimized` always returns `false`. Tab visibility via `document.hidden` emits `Focus{false}` events instead |
| Close button         | No OS chrome — `CloseRequested` must be triggered programmatically by the application                                           |

#### 9.5.2 Multi-Canvas Setup

Each `WindowManager::CreateWindow` call creates a GLFW window bound to a distinct canvas:

```cpp
// Backend internally calls:
emscripten::glfw3::SetNextWindowCanvasSelector(canvasSelector); // e.g. "#canvas1"
GLFWwindow* w = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
```

**Canvas selector derivation**: The backend generates selectors from `WindowDesc::title` (sanitized) or from an explicit `canvasSelector` field in `WindowDesc` (Emscripten-specific extension, `#ifdef __EMSCRIPTEN__`).

**HTML template requirement**: The host page must contain `<canvas id="canvas1">` etc. elements. The backend does NOT create DOM elements — it binds to existing ones. A minimal shell template is provided in `demos/shell/miki_shell.html`.

#### 9.5.3 WindowFlags Mapping

| Flag          | Desktop Effect           | Emscripten Effect                                                       |
| ------------- | ------------------------ | ----------------------------------------------------------------------- |
| `Borderless`  | Remove window decoration | No-op (canvas has no decoration)                                        |
| `AlwaysOnTop` | Pin above other windows  | No-op (canvas z-order is CSS-controlled)                                |
| `NoResize`    | Disable user resize      | Skip `MakeCanvasResizable` call — canvas stays at initial size          |
| `Hidden`      | Create window hidden     | `glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE)` — canvas gets `display:none` |

#### 9.5.4 Event Loop Integration

Emscripten requires `emscripten_set_main_loop` or `requestAnimationFrame` instead of a blocking `while(!shouldClose)` loop. The adaptation:

1. `WindowManager::PollEvents()` calls `glfwPollEvents()` as on desktop — this works because `contrib.glfw3` hooks browser events internally.
2. The **application** (not WindowManager) must use `emscripten_set_main_loop_arg` to drive the frame loop. WindowManager is loop-agnostic.
3. `ShouldClose()` still works — returns `true` when all root windows are destroyed by application code.

#### 9.5.5 WebGPU Surface from Canvas

`SurfaceManager::AttachSurface` on Emscripten extracts the canvas selector from `WebWindow::canvasSelector` and creates a WebGPU surface:

```cpp
wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
canvasDesc.selector = nativeHandle.canvasSelector;  // e.g. "#canvas1"
wgpu::SurfaceDescriptor surfDesc{ .nextInChain = &canvasDesc };
wgpu::Surface surface = instance.CreateSurface(&surfDesc);
```

#### 9.5.6 Parent-Child on Emscripten

- `iParentToken` is stored for logical tree tracking only — no OS-level owner window exists.
- Cascade destruction still works identically: post-order traversal destroys children before parent.
- The only difference is visual: there is no OS-enforced z-order or lifetime coupling between canvases.

#### 9.5.7 Resize and Fullscreen

- **Resizable canvas**: Backend calls `emscripten::glfw3::MakeCanvasResizable(window, containerSelector)` unless `NoResize` flag is set.
- **Full-window canvas**: Use `"window"` as `containerSelector` — canvas fills browser viewport.
- **Fullscreen**: `emscripten::glfw3::RequestFullscreen(window, lockPointer, resizeCanvas)` — must be triggered from a user gesture (browser security).

#### 9.5.8 Build Configuration

```cmake
if(EMSCRIPTEN)
    # Use contrib.glfw3 port (pongasoft/emscripten-glfw)
    target_compile_options(miki_core PUBLIC "--use-port=contrib.glfw3")
    target_link_options(miki_core PUBLIC "--use-port=contrib.glfw3" "-sUSE_WEBGPU=1")
    # GlfwWindowBackend compiles on Emscripten with #ifdef __EMSCRIPTEN__ adaptations
endif()
```

#### 9.5.9 Limitations and Divergences

| Feature             | Desktop                             | Emscripten                                                                |
| ------------------- | ----------------------------------- | ------------------------------------------------------------------------- |
| Window count        | Unlimited (OS-limited)              | Unlimited canvases, but each needs a pre-existing DOM element             |
| Minimize / Maximize | OS-managed                          | Not available — `IsMinimized` always `false`                              |
| Window position     | `glfwSetWindowPos`                  | No-op (CSS layout controls position)                                      |
| Native drag / move  | OS title bar                        | Not available (canvas is embedded in page flow)                           |
| Multi-monitor       | `glfwGetMonitors` + per-monitor DPI | Single "monitor" (browser viewport); DPI from `devicePixelRatio`          |
| Clipboard           | OS clipboard                        | Async Clipboard API (requires user gesture); supported by `contrib.glfw3` |
| File drag-and-drop  | `glfwSetDropCallback`               | HTML5 drag-and-drop — requires JS interop (not in contrib.glfw3 scope)    |
| Threads             | Multi-threaded rendering            | Single-threaded main; SharedArrayBuffer for pthreads (requires COOP/COEP) |
| GPU backend         | Vulkan T1 / D3D12 T1 / OpenGL T4    | WebGPU T3 only                                                            |

---

## 10. Thread Safety Model

| Operation                                | Thread Safety                      |
| ---------------------------------------- | ---------------------------------- |
| `WindowManager` all operations           | Main thread only                   |
| `SurfaceManager` all operations          | Main thread only                   |
| `IWindowBackend` all operations          | Main thread only                   |
| GPU resource creation via `DeviceHandle` | Thread-safe (§17 of rhi-design.md) |
| Command buffer recording                 | Per-cmd-buffer single-thread       |

**Rationale**: OS window operations (create, destroy, event poll) are inherently main-thread-only on all platforms (Win32, X11, Cocoa, GLFW, SDL). Forcing thread-safety here adds complexity with no benefit.

**Future (Phase 13)**: Coca coroutines may add `co_await`-based event processing on the main thread, but the single-thread constraint remains.

---

## 11. Migration Plan from Current Code

### 11.1 Files to Modify

| File                                              | Action                                                                                |
| ------------------------------------------------- | ------------------------------------------------------------------------------------- |
| `include/miki/rhi/MultiWindowManager.h`           | **Deprecated**, replaced by `platform/WindowManager.h` + `rhi/SurfaceManager.h`       |
| `include/miki/platform/WindowManager.h`           | **Refactored**: add tree structure, generation handle, `GetDescendantsPostOrder`      |
| `include/miki/rhi/RenderSurface.h`                | **Unchanged** — still the per-window surface abstraction                              |
| `include/miki/rhi/FrameManager.h`                 | **Unchanged** — still per-window frame pacing                                         |
| `demos/framework/glfw/GlfwWindowBackend.h`        | **Updated**: `CreateNativeWindow` gains `iParentToken`, add `ShowWindow`/`HideWindow` |
| NEW: `include/miki/rhi/SurfaceManager.h`          | New file: GPU surface lifecycle manager                                               |
| NEW: `include/miki/platform/WindowManagerUtils.h` | Helper: `DestroyWindowCascade`                                                        |

### 11.2 Backward Compatibility

`MultiWindowManager` is deprecated but not immediately removed. It can be reimplemented as a thin wrapper:

```cpp
// Deprecated compatibility shim
class MultiWindowManager {
    platform::WindowManager wm_;
    rhi::SurfaceManager     sm_;
public:
    // Delegates to wm_ + sm_ internally
    auto CreateWindow(const WindowDesc&) -> Result<WindowHandle>;
    auto DestroyWindow(WindowHandle) -> Result<void>;
    // ...
};
```

---

## 12. Compatibility with rhi-design.md

### 12.1 §11 Swapchain Alignment

`rhi-design.md` §11 defines low-level swapchain API (`CreateSwapchain`, `DestroySwapchain`, `AcquireNextImage`, `Present`). The relationship:

```
rhi-design.md §11          This document
─────────────────          ──────────────
SwapchainDesc              RenderSurfaceConfig (intent → resolved params)
CreateSwapchain            RenderSurface::Create (wraps)
DestroySwapchain           RenderSurface::~RenderSurface (wraps)
AcquireNextImage           RenderSurface::AcquireNextImage (wraps)
Present                    RenderSurface::Present (wraps)
SwapchainHandle            Internal to RenderSurface::Impl
SurfaceColorSpace          Shared enum (both levels use it)
```

`RenderSurfaceConfig` is a **declarative intent** (what the application wants). `SwapchainDesc` is **resolved parameters** (what the backend receives). `RenderSurface::Create` converts between them:

| `RenderSurfaceConfig` field   | Conversion                                            | `SwapchainDesc` field   |
| ----------------------------- | ----------------------------------------------------- | ----------------------- |
| `presentMode`                 | direct passthrough                                    | `presentMode`           |
| `colorSpace`                  | direct passthrough                                    | `colorSpace`            |
| `preferredFormat`             | intersect with backend support                        | `preferredFormat`       |
| `imageCount` (ImageCountHint) | resolve: Auto→backend optimal, Minimal→2, Triple→3    | `imageCount` (uint32_t) |
| `vrrMode`                     | does NOT affect SwapchainDesc (affects Present flags) | —                       |
| —                             | from `AttachSurface(nativeWindow)`                    | `surface`               |
| —                             | from `GetFramebufferSize()`                           | `width`, `height`       |

`RenderSurface` is a higher-level wrapper around the raw swapchain API. `SurfaceManager` manages the lifecycle of `RenderSurface` instances.

### 12.2 §1.4 Runtime Backend Switching

The `SurfaceManager` + `WindowManager` separation makes runtime backend switching straightforward:

```
1. SurfaceManager::WaitAll()           — drain all surfaces
2. For each window: sm.DetachSurface() — destroy all swapchains
3. SurfaceManager destroyed
4. deviceHandle.Destroy()        — tear down the current device and all internal state
5. deviceHandle = CreateDevice(newBackendDesc)  — new backend
6. SurfaceManager = SurfaceManager::Create(*device)
7. For each window: sm.AttachSurface() — recreate swapchains
```

`WindowManager` is **untouched** during backend switching — OS windows remain alive.

### 12.3 §5.6 Transient Resources

Transient resources are per-device, not per-window. `SurfaceManager` does not manage transient pools — that is `RenderGraph`'s responsibility.

### 12.4 §17 Thread Safety

Both documents agree: window/swapchain operations are main-thread-only. GPU resource creation is thread-safe.

---

## 13. Testing Strategy

> All tests serve as **behavioral specifications** and **feature definitions** (TDD). Each item below defines a single observable behavior that the implementation must exhibit. Tests are grouped by concern.

### WindowManager Creation & Destruction

- `WindowManager::Create` with a valid `IWindowBackend` returns a valid `WindowManager` in the success channel of `Result`
- `WindowManager::Create` with a `nullptr` backend returns `ErrorCode::InvalidArgument`
- A default-constructed `WindowManager` (moved-from) has zero windows and all queries return errors
- `WindowManager` destructor is safe to call on a moved-from instance (no double-free, no crash)
- Two `WindowManager` instances with separate backends can coexist without interfering with each other's state

### WindowHandle Identity & Generation

- A freshly created window returns a `WindowHandle` with `id != 0` (valid) and `generation >= 1`
- Creating N windows returns N handles with pairwise-distinct `id` values
- After `DestroyWindow(h)`, the slot occupied by `h.id` has its generation incremented by exactly 1
- A stale handle (correct `id`, old `generation`) is rejected by every query and mutation (`GetParent`, `GetChildren`, `ShowWindow`, `DestroyWindow`, etc.) with `ErrorCode::InvalidArgument`
- After destroying a window and creating a new one, if the allocator reuses the same slot index, the new handle's `generation` differs from the old one — old handle remains invalid
- `WindowHandle{0, 0}` (the default / null handle) is rejected by all WindowManager operations
- `WindowHandle::operator==` considers both `id` and `generation` — same `id` but different `generation` compares unequal
- Creating and destroying windows 65536+ times in a loop does not cause undefined behavior; generation wraps or saturates without producing a handle that aliases a live window
- In debug builds, passing a stale handle triggers an assertion; in release builds, it returns a well-defined error code

### Window Tree — Topology

- A window created with `desc.parent = {}` (default) becomes a root; `GetParent` returns `WindowHandle{}`, `GetDepth` returns 1
- A window created with `desc.parent = validRoot` becomes a child of that root; `GetParent` returns `validRoot`, `GetDepth` returns 2
- Creating a chain root → child → grandchild → great-grandchild yields depths 1, 2, 3, 4 respectively; `GetRoot` on any node returns the root
- Attempting to create a window at depth 5 (i.e. parent already at depth 4, exceeding `kMaxDepth`) fails with `ErrorCode::InvalidArgument`
- Creating a child with a parent handle that refers to a destroyed window fails with `ErrorCode::InvalidArgument`
- `GetChildren(parent)` returns a `span` whose elements are in creation-time order (earliest first)
- Creating 3 children under the same parent, then calling `GetChildren`, returns exactly those 3 handles in creation order
- Multiple root windows form independent trees; `GetRootWindows()` returns all live roots; `GetRoot(childOfRoot2)` returns `root2`, not `root1`
- `GetAllWindows()` returns every live window across all trees; count matches `GetWindowCount()`
- The tree is structurally acyclic by construction (monotonic slot allocation guarantees `parent.id < child.id`)

### Window Tree — Post-Order Traversal

- `GetDescendantsPostOrder(leaf)` returns an empty vector (a leaf has no descendants)
- `GetDescendantsPostOrder(parent_with_one_child)` returns `[child]`
- For root → {A, B} where A → {A1, A2} and B → {B1}, `GetDescendantsPostOrder(root)` returns `[A1, A2, A, B1, B]` — every node appears after all its descendants (leaves first)
- `GetDescendantsPostOrder` does not include the handle passed as argument (only descendants)
- Post-order output is deterministic: calling it twice on the same unchanged tree returns the same sequence

### Window Lifecycle Operations

- `CreateWindow` with default `WindowDesc` creates a visible, resizable, non-borderless root window
- `CreateWindow` with `WindowFlags::Hidden` creates a window that `IWindowBackend::GetFramebufferSize` can query but that is not shown to the user
- `ShowWindow` on a hidden window makes it visible; subsequent `ShowWindow` is a no-op (no error, no state change)
- `HideWindow` on a visible window makes it hidden; subsequent `HideWindow` is a no-op
- `GetWindowInfo` returns correct `title`, `width`, `height`, `flags`, `alive`, and `minimized` state for a live window
- `GetNativeHandle` returns a non-null `NativeWindowHandle` for a live window
- `GetNativeToken` returns the same opaque pointer that `IWindowBackend::CreateNativeWindow` produced
- Creating 16 windows (one chunk) succeeds; creating the 17th triggers a new chunk allocation and also succeeds (ChunkedSlotMap dynamic expansion)
- Creating 256 windows (16 chunks) succeeds without error — no hardcoded capacity limit
- Destroying a window and then creating a new one succeeds (slot recycled within its chunk)

### Window State Operations

- `ResizeWindow(h, 800, 600)` changes the window's client area; subsequent `GetWindowInfo(h).extent` returns `{800, 600}`
- `ResizeWindow` with `(0, 0)` is handled gracefully — either no-op or sets to minimum size (backend-defined)
- `ResizeWindow` on a minimized window stores the size for when the window is restored
- `SetWindowPosition(h, 100, 200)` moves the window; `GetWindowPosition(h)` returns `(100, 200)` (within OS tolerance)
- `SetWindowPosition` with negative coordinates is valid (window partially off-screen)
- `GetWindowPosition` on a newly created window returns the OS-assigned initial position
- `MinimizeWindow(h)` iconifies the window; `GetWindowInfo(h).minimized == true`; `IsMinimized` backend call returns `true`
- `MinimizeWindow` on an already-minimized window is a no-op (no error)
- `MaximizeWindow(h)` maximizes the window; `GetWindowInfo(h).maximized == true` (if tracked)
- `MaximizeWindow` on an already-maximized window is a no-op
- `RestoreWindow(h)` on a minimized window restores it; `GetWindowInfo(h).minimized == false`
- `RestoreWindow` on a maximized window restores to normal size
- `RestoreWindow` on a normal window is a no-op
- `FocusWindow(h)` requests focus; if the application is in foreground, the window receives focus
- `FocusWindow` on a hidden window shows it first (GLFW behavior), then focuses
- `FocusWindow` on a minimized window restores it first, then focuses
- `SetWindowTitle(h, "New Title")` changes the title; the backend receives the new title string
- `SetWindowTitle` with an empty string is valid (clears the title)
- `SetWindowTitle` with UTF-8 characters (e.g., "日本語") is correctly passed to the backend
- All window state operations on a stale handle return silently (no crash) or log a warning in debug builds
- On Emscripten: `SetWindowPosition` is a no-op; `GetWindowPosition` returns `(0, 0)`
- On Emscripten: `MinimizeWindow` is a no-op; `MaximizeWindow` requests fullscreen (may require user gesture)

### Cascade Destruction

- `DestroyWindow(leaf)` destroys only that leaf; the returned vector contains exactly `[leaf]`
- `DestroyWindow(parent)` destroys parent and all descendants; the returned vector is in post-order (deepest leaves first, parent last)
- After `DestroyWindow(parent)`, all handles in the returned vector are stale (any operation on them fails)
- After `DestroyWindow(parent)`, siblings of `parent` and their subtrees remain alive and queryable
- `DestroyWindow` on the sole root of a tree destroys the entire tree; `GetWindowCount()` decreases by the tree size
- `DestroyWindow` on an already-destroyed handle returns `ErrorCode::InvalidArgument`
- `DestroyWindow` on `WindowHandle{}` (null) returns `ErrorCode::InvalidArgument`
- `DestroyWindowCascade(wm, sm, parent)` executes in the correct sequence: (1) `GetDescendantsPostOrder` + self, (2) `DetachSurfaces` batch (per-surface timeline wait, no global WaitIdle), (3) `DestroyWindow` — surfaces are torn down before OS windows
- During `DetachSurfaces`, other windows' `BeginFrame`/`EndFrame` continue executing without stall (per-surface wait, see `specs/03-sync.md` §9)
- Destroying multiple independent roots in sequence works; each cascade is independent
- Destroying a parent while a child is hidden still destroys the hidden child
- Destroying a child window does **not** affect the parent: parent remains alive, `GetWindowInfo(parent).alive == true`, parent's surface (if any) continues rendering normally
- After destroying child C of parent P, `GetChildren(P)` no longer contains C; the remaining siblings' order is preserved
- After destroying child C (which has a surface), calling `BeginFrame` / `EndFrame` on the parent's surface succeeds — parent rendering is uninterrupted
- In debug builds, calling `DestroyWindow(h)` while `SurfaceManager::HasSurface(h) == true` triggers an assertion (`"DetachSurface before DestroyWindow"`); in release builds, `DestroyWindow` returns `ErrorCode::PreconditionViolated`

### IWindowBackend Contract

- `IWindowBackend::CreateNativeWindow` is called with the parent's `nativeToken` (or `nullptr` for root); on GLFW this means the share context is set
- `IWindowBackend::DestroyNativeWindow` is called exactly once per window, during `DestroyWindow`, with the token from creation
- `IWindowBackend::PollEvents` appends events into the provided vector; `WindowManager::PollEvents` returns them as a `span`
- `IWindowBackend::ShouldClose` returning `true` for a specific token causes `WindowManager::ShouldClose` to return `true` only when all roots have `ShouldClose == true`
- `IWindowBackend::GetFramebufferSize` returns the OS-level framebuffer dimensions (which may differ from `WindowDesc.width/height` on HiDPI)
- `IWindowBackend::IsMinimized` returning `true` causes `WindowInfo.minimized == true` for that window
- `IWindowBackend::ShowWindow` / `HideWindow` are forwarded by `WindowManager::ShowWindow` / `HideWindow` with the correct token
- A `MockWindowBackend` (no OS windows) supports all `IWindowBackend` methods and can run the full test suite headlessly

### Window Event Dispatch

- `PollEvents()` with no pending OS events returns an empty `span`
- After the backend injects a `CloseRequested` event for window `h`, `PollEvents()` returns a `WindowEvent{h, CloseRequested{}}` — the window remains alive (not auto-destroyed)
- After the backend injects a `Resize{newW, newH}` event, the returned `WindowEvent` carries the correct `newW` and `newH` dimensions
- `FocusGained` and `FocusLost` events carry the correct `WindowHandle` — switching focus from window A to B produces `FocusLost{A}` then `FocusGained{B}`
- `Minimized` and `Restored` events are emitted for the correct window; after `Minimized`, `GetWindowInfo(h).minimized == true`; after `Restored`, `minimized == false`
- Multiple events from a single `PollEvents()` call are returned in chronological order (oldest first)
- Events for destroyed windows are not returned (backend events for stale tokens are filtered out)
- `PollEvents()` with zero live windows returns an empty span (not an error)

### WindowFlags Behavior

- `WindowFlags::Borderless` — the backend receives a `WindowDesc` with `Borderless` set; the resulting window has no title bar (backend-verifiable via `GetFramebufferSize == GetWindowSize`)
- `WindowFlags::AlwaysOnTop` — child window created with this flag stays above parent (behavior is OS-level; test verifies the flag is forwarded to backend)
- `WindowFlags::NoResize` — the backend receives the flag; after creation, programmatic `ResizeSurface` still works (surface resize is independent of OS resize lock), but the OS prevents user-initiated resize
- `WindowFlags::Hidden` — window is created but not shown; `ShowWindow` makes it visible
- Combining `Borderless | AlwaysOnTop` forwards both flags to the backend
- Flags are stored in `WindowInfo` and `GetWindowInfo` returns them correctly after creation

### SurfaceManager — Attach / Detach / Query

- `SurfaceManager::Create(device)` with a valid `DeviceHandle` succeeds; with a null device it returns an error
- `AttachSurface(window, nativeHandle, config)` for a valid window creates a `RenderSurface` and `FrameManager`; `HasSurface(window)` returns `true`
- `AttachSurface` on a window that already has a surface returns an error (no double-attach)
- `AttachSurface` with an invalid (stale/null) `WindowHandle` returns an error
- `DetachSurface(window)` destroys the surface and frame manager; `HasSurface(window)` returns `false`; `GetRenderSurface(window)` returns `nullptr`
- `DetachSurface` on a window without a surface returns an error
- `DetachSurfaces(batch)` detaches all listed windows in one call; each surface is waited on individually via `FrameManager::WaitAll()` (per-surface timeline wait, no device-wide `WaitIdle`). This is safe because §5.3.1 guarantees swapchain images are never referenced by other windows' command buffers
- `DetachSurfaces` with an empty span is a no-op (not an error)
- `DetachSurfaces` with a mix of valid and invalid handles: the behavior is either all-or-nothing error, or partial detach with error reported (define which in impl)
- After `DetachSurface`, immediately calling `AttachSurface` on the same window succeeds (re-attach)
- `DetachSurface(child)` while parent's surface is mid-frame (`BeginFrame` called but `EndFrame` not yet): only the child's in-flight frames are waited on (per-surface timeline wait); parent's rendering is unaffected and continues without stall
- After `DetachSurface(child)`, shared GPU resources (textures, buffers, pipelines) that were used by child's render passes remain valid and usable by other windows' surfaces

### SurfaceManager — Frame Operations

- `BeginFrame(window)` returns a `FrameContext` for a window with an attached surface
- `BeginFrame` on a window without a surface returns an error
- `BeginFrame` on a minimized window returns an error or a sentinel `FrameContext` indicating skip (define which)
- `EndFrame(window, cmd)` submits the command buffer and presents; after `EndFrame`, the surface is ready for the next `BeginFrame`
- `BeginFrame` / `EndFrame` on two different windows can be interleaved: `BeginFrame(A)`, `BeginFrame(B)`, `EndFrame(A)`, `EndFrame(B)` — frames are per-window independent
- `ResizeSurface(window, newW, newH)` recreates the swapchain with the new dimensions; subsequent `BeginFrame` uses the new size
- `ResizeSurface` with `(0, 0)` (minimized) is handled gracefully — either no-op or marks surface as dormant
- `WaitAll()` blocks until all in-flight GPU work across all surfaces completes

### SurfaceManager — Dynamic Present Configuration

- `GetSupportedPresentModes(window)` returns a non-empty vector containing at least `PresentMode::Fifo` (universally required)
- `GetSupportedColorSpaces(window)` returns a non-empty vector containing at least `SurfaceColorSpace::SRGB`
- `SetPresentMode(window, Mailbox)` on a T1 Vulkan surface succeeds; subsequent `BeginFrame`/`EndFrame` cycle renders correctly with the new mode
- `SetPresentMode(window, Immediate)` with `allowTearing == true` on a VRR-capable display succeeds
- `SetPresentMode` on a window without an attached surface returns an error
- `SetPresentMode` with an unsupported mode (not in `GetSupportedPresentModes`) returns an error; existing surface is unaffected
- `SetColorSpace(window, HDR10_ST2084)` on a T1 backend with HDR display succeeds; swapchain format may change to `R10G10B10A2_UNORM` or `R16G16B16A16_SFLOAT`
- `SetColorSpace(window, HDR10_ST2084)` on a WebGPU/OpenGL backend returns an error (HDR unsupported); existing surface is unaffected
- `SetColorSpace` / `SetPresentMode` during mid-frame (`BeginFrame` called but `EndFrame` not yet) returns an error (no swapchain recreation while in-flight)
- Calling `SetPresentMode` 10 times in a row (without rendering) succeeds each time — no accumulated resource leaks (swapchain count stable)

### Multi-Window GPU Resource Sharing

- A texture created via `DeviceHandle` can be sampled in render passes of two different windows' surfaces without duplication
- A vertex buffer created once is bindable in command buffers targeting any window's surface
- A pipeline (PSO) created once is usable in any window's render pass
- Destroying one window's surface does not invalidate shared resources used by other windows
- After destroying all windows and surfaces, shared resources remain valid until explicitly destroyed via `DeviceHandle`
- `DeviceHandle` outlives all `SurfaceManager` and `WindowManager` instances; destroying managers first, then device, is the correct order

### Backend Switching (§12.2 Protocol)

- After `SurfaceManager::WaitAll()` → `DetachSurface` all → destroy `SurfaceManager` → destroy old device → create new device → create new `SurfaceManager` → `AttachSurface` all: all windows resume rendering
- `WindowManager` state (tree structure, handles, window count) is completely unaffected by backend switching
- `WindowHandle` values obtained before the switch remain valid after the switch
- The switch protocol completes within a bounded time (no indefinite stall)

### Error Handling & Robustness

- Every public API that returns `Result<T>` never throws exceptions; all errors are communicated via `ErrorCode`
- Passing a stale `WindowHandle` to any `WindowManager` or `SurfaceManager` method returns `ErrorCode::InvalidArgument` (not UB, not crash)
- ChunkedSlotMap allocation failure (system OOM) returns `ErrorCode::ResourceExhausted`
- Creating a child whose parent exceeds `kMaxDepth` returns `ErrorCode::InvalidArgument`
- `DestroyWindow` called twice on the same handle: first call succeeds, second returns `ErrorCode::InvalidArgument`
- After an error from `CreateWindow`, the system state is unchanged (strong exception safety analog)
- After an error from `AttachSurface`, no partial surface exists — `HasSurface` returns `false`

### Thread Safety (§10)

- All `WindowManager` and `SurfaceManager` operations from the main thread succeed
- Calling `WindowManager::CreateWindow` from a non-main thread triggers an assertion in debug builds and returns an error in release builds
- Calling `SurfaceManager::BeginFrame` from a non-main thread triggers an assertion in debug builds
- Calling `PollEvents` from a non-main thread triggers an assertion in debug builds
- GPU resource creation via `DeviceHandle` from worker threads does not conflict with main-thread surface operations
- Thread-safety checks impose negligible overhead on the main thread hot path (< 10ns per check via `std::this_thread::get_id()` comparison)

---

## 13.1 Comprehensive Integration Tests

### Full-Lifecycle Cascade Stress

Create a forest of 3 root windows, each with 4 children (total 15 windows, fitting in a single 16-slot chunk). Attach surfaces to all 15 via `SurfaceManager`. Call `BeginFrame`/`EndFrame` on each window for 100 iterations to exercise frame pacing. Then cascade-destroy the first root via `DestroyWindowCascade` — verify post-order destruction of 5 windows, surfaces detached before OS destroy, and the remaining 10 windows continue rendering without interruption. Destroy remaining roots. Verify `GetWindowCount() == 0`, no leaked handles, and all surfaces detached.

### Handle Generation Wraparound

In a loop, create a single root window, destroy it, and repeat 70000 times (exceeding `uint16_t` range). After each destroy, verify the old handle is rejected. After every 1000 iterations, create 3 windows simultaneously and verify all have distinct handles. At the end, verify the system operates normally with no performance degradation or aliased handles.

### Rapid Resize Flood

Create a window, attach a surface, then call `ResizeSurface` 50 times in rapid succession with random dimensions between (1, 1) and (3840, 2160). After each resize, call `BeginFrame`/`EndFrame` to verify the surface is functional. Verify no swapchain recreation failures, no GPU validation errors, and final surface dimensions match the last resize call.

### Event Ordering Under Concurrent Window Operations

Using a `MockWindowBackend`, inject 500 events across 8 windows in a deterministic sequence. Between every 50 events, create or destroy a window. Verify: (1) events for live windows are returned in injection order, (2) events for destroyed windows are filtered, (3) `PollEvents` span length equals expected live-window event count, (4) no event is duplicated or lost.

### Three-Concern Separation Verification

Verify that `WindowManager` has zero compile-time dependency on `SurfaceManager` or any RHI type — `WindowManager.h` does not include any header from `miki::rhi`. Verify that `SurfaceManager` does not include `WindowManager.h` — it only uses `platform::WindowHandle` (a POD struct). Verify that `IWindowBackend` does not reference `RenderSurface`, `FrameManager`, or `DeviceHandle`. This can be tested via a static analysis pass or a dedicated compilation unit that includes each header in isolation.

### Max-Depth Tree Operations

Build the deepest allowed tree: root (depth 1) → child (2) → grandchild (3) → great-grandchild (4). Verify `GetDepth` returns 1–4. Verify `GetRoot` from great-grandchild returns root. Verify `GetDescendantsPostOrder(root)` returns `[great-grandchild, grandchild, child]`. Attempt to create a child of great-grandchild — must fail. Destroy grandchild — great-grandchild is also destroyed (cascade), child survives, root survives, tree depth is now 2.

### Surface Detach-Reattach Cycle

For a single window: attach surface → `BeginFrame`/`EndFrame` → detach surface → verify `HasSurface == false` → reattach with different `RenderSurfaceConfig` (e.g. different present mode) → `BeginFrame`/`EndFrame` → verify rendering works with new config. Repeat 20 times. Verify no resource leaks (VRAM usage stable within 5% of initial).

### Child Destruction Isolation

Create root R with children A, B, C (each with an attached surface). Run 10 frames of `BeginFrame`/`EndFrame` on all four windows. Then `DestroyWindowCascade(A)` — detach A's surface, destroy A's OS window. Verify: (1) R, B, C are alive with `GetWindowInfo(...).alive == true`, (2) `GetChildren(R)` returns exactly `[B, C]` in original creation order, (3) `BeginFrame`/`EndFrame` on R, B, C succeed for another 10 frames, (4) A's handle is stale (rejected by all APIs), (5) shared GPU resources (textures/pipelines) used by A's render passes remain valid for R/B/C. Then destroy B — verify `GetChildren(R) == [C]`. Destroy C — verify `GetChildren(R)` is empty. R continues rendering alone.

### Cross-Window Cascade with Shared Resources

Create root R → child A → grandchild A1. Attach surfaces to all three. Create a shared texture T via `DeviceHandle` and sample T in render passes of all three windows for 5 frames. Then `DestroyWindowCascade(A)` — this destroys A1 first, then A (post-order). Verify: (1) per-surface `FrameManager::WaitAll()` is called for each destroyed surface (no device-wide `WaitIdle`), (2) A1's surface is detached before A's surface, (3) R's surface is untouched and continues rendering using shared texture T, (4) T is not invalidated — `BeginFrame`/`EndFrame` on R using T succeeds for another 10 frames, (5) no Vulkan validation errors or D3D12 debug layer warnings throughout the sequence.

### Dangling Surface Debug Assert

Create window W, attach surface. In debug builds, call `wm.DestroyWindow(W)` **without** first calling `sm.DetachSurface(W)`. Verify an assertion fires (or in release builds, `DestroyWindow` returns `ErrorCode::PreconditionViolated`). Then properly detach the surface and retry `DestroyWindow` — verify it succeeds. This tests the safety net that prevents OS window destruction while GPU resources are still attached.

### Error Recovery Chain

(1) Create window, attach surface, deliberately pass an invalid `NativeWindowHandle` to a second `AttachSurface` — verify error, verify first surface unaffected. (2) `DetachSurface` on a window that has no surface — verify error, verify other surfaces unaffected. (3) `DestroyWindow` with a null handle — verify error, verify all other windows alive. (4) Create 16 windows, attempt 17th — verify `ChunkedSlotMap` dynamically expands and all 17 windows are operational. After all error injections, perform a clean shutdown: cascade-destroy all roots, verify zero windows, zero surfaces.

---

## 14. Design Decisions Log

| Decision                                                       | Rationale                                                                                                                                                              | Alternatives Considered                                                                                                                          |
| -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Forest of N-ary trees                                          | Matches Win32 owner-window and Qt QObject tree models; natural for CAD multi-panel apps                                                                                | Flat list (too simple), DAG (overcomplicated, no use case)                                                                                       |
| Manual cascade orchestration                                   | Preserves three-concern separation; easy to test each manager independently                                                                                            | Auto cascade in WindowManager (violates separation), callback-based (hidden control flow)                                                        |
| Generation-counted handles                                     | Prevents ABA/use-after-free; consistent with RHI handle design                                                                                                         | Raw pointers (unsafe), `std::shared_ptr` (overhead, ref-counting in hot path)                                                                    |
| `SurfaceManager` as separate class                             | Decouples GPU lifecycle from OS window lifecycle; enables backend switching without touching windows                                                                   | Embed in `WindowManager` (coupling), embed in `RenderGraph` (wrong level)                                                                        |
| Post-order destruction                                         | GPU surfaces must be destroyed before OS windows; leaves-first ensures children's GPU work is drained before parent                                                    | Pre-order (would destroy parent surface while children still rendering), arbitrary order (unsafe)                                                |
| `CloseRequested` as event, not auto-destroy                    | Application may want to prompt "save changes?" or hide instead of destroy                                                                                              | Auto-destroy on close (inflexible, old design)                                                                                                   |
| Single `DeviceHandle` for all windows                          | Industry standard (Filament, Diligent, Vulkan best practice); maximizes resource sharing                                                                               | Device-per-window (wasteful, no resource sharing, higher VRAM)                                                                                   |
| `kChunkSize = 16` (no hard capacity limit)                     | ChunkedSlotMap allocates 16-slot chunks; typical CAD apps fit in 1 chunk (no heap churn). Grows dynamically for atypical workloads (256+ windows tested)               | Per-window heap alloc (fragmentation, cache-unfriendly), fixed-size array (hard limit, wasteful if unused)                                       |
| `DestroyWindow` rejects windows with attached surfaces         | Prevents GPU resource leaks and use-after-free of swapchain images referencing destroyed OS windows. Debug assert + release error code.                                | Auto-detach in DestroyWindow (violates separation, hides GPU lifecycle from caller), silent UB (unacceptable)                                    |
| `DetachSurfaces` uses per-surface `FrameManager::WaitAll()`    | §5.3.1 guarantees swapchain images are never cross-referenced; per-surface timeline wait is sufficient and avoids stalling unrelated windows                           | Device-wide `WaitIdle` (stalls all windows unnecessarily), no wait (undefined behavior)                                                          |
| `RenderSurfaceConfig` with `PresentMode` + `SurfaceColorSpace` | Explicit per-window control over VSync/VRR/HDR at attach time; maps directly to `VkPresentModeKHR` / `DXGI_SWAP_EFFECT` / `VkColorSpaceKHR`                            | Global present mode (inflexible — CAD main viewport wants Mailbox while tool windows want Fifo), no HDR support (unacceptable for 2026 displays) |
| `SetPresentMode` / `SetColorSpace` as runtime APIs             | Enables in-app settings toggle (VSync on/off, HDR on/off) without full detach-reattach cycle. Internally waits per-surface then recreates swapchain                    | Detach + reattach only (heavier, loses surface state), immutable config (no runtime toggle)                                                      |
| Emscripten: single `GlfwWindowBackend` with `#ifdef`           | Maximizes code sharing between desktop and web — only canvas-selector setup, flags mapping, and `IsMinimized` differ. Avoids maintaining two separate backend classes. | Separate `EmscriptenWindowBackend` class (duplication), `#ifdef` in WindowManager (violates separation)                                          |
| Canvas selector from `WindowDesc::title` or explicit field     | Pragmatic default: most apps have one canvas named after the window title. Explicit `canvasSelector` covers multi-canvas scenarios without breaking the desktop API.   | Always require explicit selector (breaks single-window simplicity), auto-create DOM elements (too invasive, breaks host page layout)             |
